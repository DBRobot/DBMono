# DBcan

DBcan is an open-source CAN FD protocol using both standard (11-bit) and
extended (29-bit) identifiers, plus a vendor-agnostic transport layer that
carries it over real hardware.

---

## Protocol Specification

DBcan uses both standard (11-bit) and extended (29-bit) CAN FD frames. The
frame format encodes the addressing model:

- **11-bit frames are broadcast.** The ID identifies the sender. There is no
  receiver field — every frame is physically broadcast, and consumption is a
  receiver-side filtering decision.
- **29-bit frames are point-to-point.** The ID identifies both endpoints.

### One Shared Arbitration Space

CAN arbitration compares the first 11 bits on the wire — the full ID of a
standard frame, bits 28–18 of an extended frame — bit for bit; lower value
wins. An extended frame loses to a standard frame only on an exact tie of
those 11 bits (its SRR bit is recessive).

Neither frame type inherently outranks the other. The top 11 bits of all
frames therefore form **one shared arbitration space**, and ID allocation
must be planned globally across both frame types. How broadcast and PTP
traffic interleave is set entirely by the prio plan (open question).

### Priority Field

The first 3 bits transmitted — bits 10–8 of a broadcast frame, bits 28–26 of
a PTP frame — occupy the same arbitration position and form a single **prio
field** shared by both frame types. It partitions the arbitration space into
8 priority levels.

A prio level is a property of a *message class*, assigned at allocation time — not
a per-frame knob the sender turns. A node cannot transmit traffic at a
priority for which it holds no allocated ID.

```
prio 0   emergency        faults, e-stop
prio 1   NMT + SYNC       network control, cycle sync
prio 2   cyclic control   commands + feedback
prio 3   events           limits, mode changes, warnings
prio 4   telemetry        periodic status, heartbeat
prio 5   service          register access, request/response
prio 6   debug            developer traffic
prio 7   bulk             firmware, segmented transfers
```

Prio levels rank by consequence-of-delay, not importance. Prio 7 is a pure
scavenger class: bulk transfers can never interfere with traffic above them,
by construction. ID allocation within each prio level is TBD.

### 11-Bit Frames (Broadcast)

```
 10  9  8   7  6  5  4  3  2  1  0
[--PRIO--] [-------SRC UID-------]
```

- **Bits 10–8 — Prio.** Priority level (see above).
- **Bits 7–0 — Source UID.** 8-bit unique ID of the sending node.

### 29-Bit Frames (Point-to-Point)

```
 28 27 26  25   24 ...... 16   15 ........ 8   7 ......... 0
[--PRIO--] [R] [----TBD-----] [--SENDER UID--] [-RECEIVER UID-]
```

- **Bits 28–26 — Prio.** Priority level, arbitration-aligned with the
  broadcast prio field.
- **Bit 25 — Role.** `0` = response, `1` = request. Response is dominant, so a
  completing transaction wins arbitration against a new request at the same
  prio, freeing requester state under load. Sits above the sender UID so the
  request/response split holds regardless of who is talking, and lets a
  receiver hardware-filter requests and responses into separate RX FIFOs.
- **Bits 24–16 — Reserved (9 bits).** Transmit as zero. Bits 24–18 are
  arbitration-visible, so any future allocation must account for its effect
  on within-prio ordering. Candidate future use: random discriminator for
  anonymous ID-claim frames.
- **Bits 15–8 — Sender UID.** 8-bit unique ID of the sending node.
- **Bits 7–0 — Receiver UID.** 8-bit unique ID of the intended recipient.

### ID Uniqueness

Every frame ID embeds its sender's UID, so no two nodes ever transmit the
same ID. Broadcast IDs are unique per (bits 10–8, sender); PTP IDs are
unique per (bits 28–16, sender, receiver). Arbitration therefore always
resolves without error, and worst-case latency is computable per ID.

### Addressing

Node UIDs are 8-bit values and must be unique on the bus. Reserved UID
values (e.g. 0x00, 0xFF) are an open question.

### Data Payload

There are two payload shapes, one per frame type. Both are little-endian
(LSB first) and packed with no padding. Neither carries a length or a type
tag: register widths are fixed by the register map, and both ends share that
map (see *Register Map & Numbering* below).

#### Service payload (29-bit / PTP)

Register access — read, write, command — as a request/response transaction.
See `core/inc/header.h`.

```
byte 0        header
byte 1..2     register id, 16-bit          (request frames only)
byte 3..N     value, register-width bits    (writes and read replies)
```

Header byte:

```
 7    6   5   4   3   2   1   0
[-] [A/E] [S] [------REQ_ID------]
```

- **Bit 7 — Reserved**, must be 0.
- **Bit 6 — A/E.** Role comes from the CAN ID, not here. On a request
  (ROLE=1) it is **A**: ack requested — a reply is wanted. On a response
  (ROLE=0) it is **E**: this is an error, and the payload carries a
  structured `{code, name}` instead of a value.
- **Bit 5 — S.** Segmented: this is part of a multi-frame value.
- **Bits 4–0 — REQ_ID.** Matches a reply to its request; scoped per peer
  pair, so 32 outstanding is ample. Rejects a stale reply that arrives after
  a timeout-and-retry. Ignored when A=0 (fire-and-forget); send 0. The
  receiver also remembers the last REQ_ID it acted on per peer, so a retried
  command is re-acked but not re-executed.

**The operation is implied, not encoded.** The target register's permission
class is resolved first:

- **CMD register** → command (value ignored)
- else **0 value bytes** → read
- else **N value bytes** → write

A reply omits the register id — REQ_ID already identifies what it answers.

```
op           ROLE  A/E  S   bytes
read   req    1     1   0   [hdr][id]
read   rsp    0     0   0   [hdr][value]
write  req    1    0/1  0   [hdr][id][value]
write  ack    0     0   0   [hdr]
cmd    req    1    0/1  0   [hdr][id]
cmd    ack    0     0   0   [hdr]
error  rsp    0     1   0   [hdr][code]
```

**Multi-frame** (S=1): segments of one transfer share a CAN ID, so they
arrive in send order — no sequence number needed. Both ends know the total
width from the map, so there is no length field either: completion is when
the assembled bytes reach the declared register width. A short count at
timeout means retransmit the whole transfer. (Bulk/firmware transfers at
prio 7 are a separate scheme with its own sequencing and flow control.)

#### Data payload (11-bit / broadcast)

Cyclic process data — a node publishing its own state. There is **no payload
header**: the 11-bit ID (prio + sender) is the entire identity, and the rest
of the frame is packed register values back to back.

The frame is the **broadcast projection of the register map**. A node's
*publish mapping* lists which of its registers go into its frame at a given
prio; the values are packed in that order. A subscriber's *subscribe mapping*
lists which source UIDs it consumes (this list is also the hardware
acceptance filter), decodes each frame against the publisher's layout, and
mirrors the values into local registers. Both mappings are runtime-
configurable registers (PDO-style), expressed in register numbers.

- One broadcast frame per node per prio (the ID has no spare bits for a
  sub-index), capped at 64 bytes — the compiler can enforce the ceiling.
- Broadcast **rate** is a register. Broadcast **phase alignment** across
  nodes — everyone sampling/actuating on one edge — requires a shared SYNC
  and is only needed for coordinated multi-node control.
- A **setpoint** is just a register write on the service plane; it needs no
  special data-plane mechanism. Broadcasting setpoints is only an
  optimization for one controller commanding many nodes at rate.
- A **mapping-version** counter lets a subscriber cache a publisher's layout
  and detect reconfiguration, so a layout change can't be silently
  misdecoded.

### Register Map & Numbering

Registers are addressed on the wire by a 16-bit number, assigned by the
schema compiler from a deterministic walk of the register map — not authored
by hand. Discovery (`_children[i]`) reports each register's name alongside
its number, so name strings appear on the wire only during discovery, never
in normal traffic.

`firmware.reg_map_hash` is a global register holding a CRC over the map's
*definitions*. A peer reads it once on first contact and again when the peer
reboots (never polled); a mismatch against its own compiled constant means
the two were built from different maps and every cached number is suspect.
This is what makes auto-numbering safe: numbers may be freely reassigned
across firmware versions because the hash turns any skew into a hard fault
rather than a silent misread. **There is intentionally no wire compatibility
across map versions** — updating one node's firmware requires rebuilding the
peers that talk to it.

#### Number space

The 16-bit number space is partitioned, following the same convention as
CANopen's standard/manufacturer index ranges and J1939's proprietary PGNs:

| Range           | Owner                | Count  |
|-----------------|----------------------|--------|
| `0 – 4095`      | protocol (global map)| 4096   |
| `4096 – 65535`  | board-specific       | 61440  |

```
#define BOARD_BASE 4096
```

Every board compiles the global map plus its own registers. The split exists
so that adding a protocol register never renumbers a board's registers — the
two grow independently. Reserving 4096 for a global map currently using ~70
follows the usual practice of holding back one to two orders of magnitude
more than present use.

The gap costs no flash. `reg_table` holds only real registers, and because
the two blocks are each contiguous, lookup is a subtraction rather than a
sparse index or a search:

```c
static inline int reg_index(uint16_t n) {
    if (n < GLOBAL_COUNT) return n;
    if (n < BOARD_BASE)   return -1;              /* reserved, unused */
    uint16_t i = GLOBAL_COUNT + (n - BOARD_BASE);
    return (i < REG_COUNT) ? i : -1;
}
```

This does not change the compatibility rule above — `firmware.reg_map_hash`
still governs whether two nodes may talk. The partition is about keeping the
two authorities over the number space independent, not about wire
compatibility across versions.

### Required Functions (Inventory)

Everything the protocol must provide, independent of bit layouts. Status:
[x] agreed, [~] partially designed, [ ] not yet designed.

**Core transport of meaning**
- [x] Node addressing — 8-bit UIDs, unique on bus
- [x] Priority policy — prio field, allocation-time assignment
- [x] Broadcast process data — 11-bit, sender-keyed
- [x] PTP messaging — 29-bit, both endpoints in ID
- [~] Payload layout definition — service header + broadcast projection of
      the register map defined; mapping registers still to design
- [~] Multi-frame transfer — service-plane SEG scheme defined (length from
      map, no SEQ); prio-7 bulk scheme with sequencing/flow control open

**Time & determinism**
- [ ] Cycle sync — SYNC broadcast, cycle counter, sample/actuate convention
      (only needed for coordinated multi-node phase alignment; per-node rate
      is a register)
- [ ] Time distribution — shared clock / timestamping (needed beyond cycle
      sync? open)
- [ ] Bandwidth budgeting — per-prio-level load accounting so worst-case latency
      is provable

**Network management**
- [~] Node ID assignment — ID claim chain drafted in global_reg.yaml;
      bootstrap problem open (how does a node transmit before it has a UID?)
- [ ] Node states & transitions — boot / operational / stopped, and who may
      command them in masterless topologies
- [ ] Discovery & identification — enumerate nodes, device class, board
      type, versions
- [ ] Health monitoring — heartbeat/liveness, timeout policy
- [ ] Error reporting — fault broadcast, error codes, severity

**Services**
- [~] Register access — read/write/command implied by permission class;
      16-bit compiler-assigned numbers, map CRC; register system in development
- [~] Request/response semantics — REQ_ID matching, configurable acks,
      duplicate suppression defined; timeout policy value still open
- [~] Firmware update — bootloader exists; protocol integration undefined

**Explicitly deferred / undecided scope**
- [ ] Babbling-idiot protection (TX budget enforcement)
- [ ] Redundant bus support
- [ ] Bus bridging / multi-segment routing
- [ ] Security / authentication

### Open Questions

1. ~~Broadcast ID allocation~~ — resolved: one broadcast frame per node per
   prio; the publish mapping decides which registers fill it. The 11-bit ID
   has no spare bits for a sub-index.
2. Broadcast-setpoint optimization: a setpoint is a register write (PTP), but
   one controller commanding many nodes at rate is N writes/cycle. A single
   broadcast could replace them — an optimization, not a missing mechanism.
   PTP costs ~18 extra bit times in the arbitration phase (no FD fast
   bitrate) per frame.
3. Reserved UID values.
4. SYNC / cycle mechanism — needed only for coordinated multi-node
   sample/actuate alignment; per-node broadcast rate is a plain register.
5. Timeout policy — the value/derivation of the request timeout that
   stale-reply rejection and multi-frame completion depend on.
6. `_describe` convention — whether discovery also exposes units / resolution
   / min / max so generic tooling can render engineering values, or decode
   only.
7. Bulk/firmware transfer (prio 7) — its own segmented scheme with sequence
   numbers and flow control, distinct from the service-plane SEG bit.

---

## Transport Layer

Vendor-agnostic CAN transport abstraction for embedded firmware.

`transport.h` defines the abstract transport handle (`transport_t`), the dispatch
table (`transport_ops_t`), and the supporting types (frames, filters,
configuration, error accounting, callback signatures). Vendor ports under
`transports/` implement the contract for specific hardware (currently FDCAN over
ST HAL).

The transport layer hides the underlying CAN/CAN-FD hardware behind a uniform
API. Higher-level protocol code (e.g. CANopen, J1939, ISO-TP, or a custom
protocol) operates on a `transport_t *` and dispatches every operation through
`transport_t::ops`, never referencing vendor-specific types or HAL handles.

---

## How to use this transport

1. Include `transport.h` and `transport_port.h`, and define the appropriate
   port macro in your build system (`TRANSPORT_PORT_ST_FDCAN` or
   `TRANSPORT_PORT_SOCKETCAN`) to get the right `init_transport()` declaration
   for your hardware. Application code should reference the port macro only
   at the platform / setup layer, never in protocol code.

2. Allocate a `transport_t` for each bus you want to use. The `transport_t`
   only carries the dispatch handle; the vendor port allocates its own
   per-instance state internally.

   ```c
   transport_t bus_a;
   ```

3. Wire the `transport_t` to a specific bus using the vendor's setup function
   (e.g. `init_transport()`). This populates `ctx`, `ops`, and `bus_id` on the
   `transport_t` and binds the back-pointer inside the vendor struct so ISRs
   can deliver events to your callbacks.

   ```c
   init_transport(&bus_a, 0);
   ```

4. Configure the peripheral by calling the `init` op with a populated
   `transport_config_t`. Mode, FD/BRS enable, auto-retransmit, and whether RX
   interrupts should be activated are all expressed at this layer; bitrate is
   owned by the vendor port (typically configured at design time, e.g. via
   CubeMX).

   ```c
   transport_config_t cfg = {
       .mode                       = TP_NORMAL_MODE,
       .fd_enabled                 = true,
       .brs_enabled                = true,
       .auto_retx_enabled          = true,
       .auto_bus_recovery_enabled  = true,
       .rx_int_active              = true,
   };
   bus_a.ops->init(&bus_a, &cfg);
   ```

5. Register receive and event callbacks via the ops table. The transport
   invokes these from ISR context when frames arrive or events occur. Keep
   handlers short; copy frame data out if you need it past the callback's
   lifetime.

   ```c
   static void on_rx(transport_t *t, uint8_t fifo, can_frame_t *msg) {
       // do work with msg before returning
   }
   static void on_fifo_evt(transport_t *t, uint8_t fifo, fifo_event_t evt) {
       // react to MESSAGE_LOST_RX, RX_FULL, ...
   }
   static void on_bus_evt(transport_t *t, bus_event_t evt) {
       // react to BUS_OFF, BUS_PASSIVE, ...
   }
   bus_a.ops->set_rx_cb        (&bus_a, 0, on_rx);
   bus_a.ops->set_fifo_event_cb(&bus_a, 0, on_fifo_evt);
   bus_a.ops->set_bus_event_cb (&bus_a, on_bus_evt);
   ```

6. Install hardware filters **before** starting the bus. Filters can only be
   added while the peripheral is in the `READY` state (post-init, pre-start).
   To modify filters after `start()`, the bus must be stopped first.

   ```c
   transport_filter_t f = {
       .id     = 0x123,
       .mask   = 0x7FF,
       .fifo   = 0,
       .index  = 0,
       .is_ext = false,
   };
   bus_a.ops->add_filter(&bus_a, &f);
   ```

7. Start the bus. The peripheral begins accepting and transmitting frames;
   registered callbacks fire on RX events.

   ```c
   bus_a.ops->start(&bus_a);
   ```

8. Send frames via the `send` op. The call is synchronous: it pushes the
   frame into the hardware TX FIFO and returns. `TP_BUSY` is returned if the
   FIFO is full; the caller decides whether to drop, retry, or backpressure
   upstream.

   ```c
   can_frame_t out = {
       .id  = 0x200,
       .len = 8,
       .data = { 0x01, 0x02, /* ... */ },
   };
   bus_a.ops->send(&bus_a, &out);
   ```

9. Optionally poll the hardware FIFO directly via the `receive` op when not
   running in interrupt mode (`cfg.rx_int_active == false`). The caller
   supplies a `can_frame_t` to be filled.

   ```c
   can_frame_t in;
   while (bus_a.ops->receive(&bus_a, &in, 0) == TP_OK) {
       handle_frame(&in);
   }
   ```

10. Inspect bus health via the `transport_ctx_t` embedded in each port's
    vendor struct: `error_count[]`, `rx_ok_count`, `tx_ok_count`, `bus_state`,
    and the `last_error` snapshot are populated by the driver and the ISR
    path.

11. Tear down the bus via `stop()` then `deinit()` when the transport is no
    longer needed.

---

## How to write a new transport port

This section is for authors of new vendor ports (a different MCU family, a
SPI-attached CAN controller, a USB-CAN adapter, a SocketCAN backend for
host-side simulation, etc.). The contract that every port must satisfy:

1. **Define a vendor struct** that embeds `transport_ctx_t` as a member
   named `base`. The vendor struct also holds:
   - A back-pointer to the wrapping `transport_t` (`transport_t *self`).
   - The hardware handle / peripheral pointer.
   - Storage for registered RX and event callbacks.
   - Any other per-instance state the implementation needs.

   > **Note:** access shared health/error fields explicitly through `vendor->base.*`.
   > Do not cast a vendor-struct pointer to `transport_ctx_t *` — the layout makes
   > no guarantee about which member comes first, and the cast would read garbage.

2. **Implement every function pointer slot** in `transport_ops_t` with a
   port-local `static` function. Each implementation casts `transport_t::ctx`
   to its own vendor struct type to recover the instance state.

3. **Provide a setup function** (typically
   `init_transport(transport_t *t, uint8_t bus)`) declared in the vendor
   header. It must:
   - Look up the appropriate hardware handle for the requested bus.
   - Zero-initialize the vendor struct (`memset`).
   - Set `vendor->handle`, `vendor->self = t`, `t->ctx = vendor`,
     `t->ops = &your_ops`, `t->bus_id = bus`.

   > **Note:** this setup function is the only place that should reference
   > vendor-specific peripheral handles by name. Higher layers only ever see
   > `transport_t`.

4. **Translate the vendor's interrupt or event mechanism** into the
   transport's callback contract:
   - RX frame arrived → invoke `vendor->rx_cb[fifo](self, fifo, msg)` with a
     `can_frame_t` populated from the hardware header.
   - Bus / FIFO event → invoke `vendor->event_cb[fifo](self, fifo, event_code)`
     with a `bus_event_t` value.
   - HW errors that won't surface as a callback should still update the
     `error_count[]` and `last_error` snapshot via the port's `record_error()`
     helper.

   > **Note:** the frame buffer used to invoke the RX callback typically lives
   > on the ISR stack. The application is expected to consume or copy the
   > frame before the callback returns.

5. **Define and export your `transport_ops_t`** (e.g. `fdcan_st_hal_ops`) as
   a `const`, file-scope object. Every slot must be filled; null slots are
   not supported.

6. **Honour `transport_config_t` at init time:**
   - `mode` → peripheral operating mode.
   - `fd_enabled` / `brs_enabled` → frame format selection.
   - `auto_retx_enabled` → retransmission on error.
   - `auto_bus_recovery_enabled` → behaviour when bus-off is detected.
   - `rx_int_active` → whether to enable hardware RX interrupts.

   Bitrate / timing are owned by the port. Either accept that they are
   configured externally (e.g. via CubeMX), or extend `transport_config_t`
   and translate to the vendor's timing fields.

7. **Use the `TRY()` macro** for HAL-bridging functions that record errors at
   the origin and propagate the result. `record_error()` should be a
   port-private helper; it is intentionally not exposed via ops, since it is
   internal bookkeeping invoked by the port itself.

---


