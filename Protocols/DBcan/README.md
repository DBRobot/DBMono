# DBcan

DBcan is an open-source CAN FD protocol using both standard (11-bit) and
extended (29-bit) identifiers, plus a vendor-agnostic transport layer that
carries it over real hardware.

---

## Protocol Specification

DBCAN is an open-source CAN FD protocol using both standard (11-bit) and extended (29-bit) identifiers. The frame format itself encodes the first-level message type distinction: 11-bit frames carry infrastructure traffic, 29-bit frames carry data traffic. 11-bit frames inherently win CAN arbitration over 29-bit frames, ensuring infrastructure messages always take priority.

### CAN ID Layout

#### 11-Bit Frames (Infrastructure)

Used for: errors, network management, debug, and heartbeat messages. These are always broadcast.

```
Bits 10-9:  Message Type    (2 bits)
Bit 8:      Response        (1 bit)
Bits 7-0:   Source UID      (8 bits)
```

##### Message Type Encoding

| Value | Name                | Description                          |
|-------|---------------------|--------------------------------------|
| 0b00  | Error               | Fault announcements                  |
| 0b01  | Network Management  | NMT commands (reset, sync, etc.)     |
| 0b10  | Debug               | Diagnostic and debug traffic         |
| 0b11  | Heartbeat           | Periodic health and presence         |

Lower values win CAN arbitration first, so errors have the highest priority, followed by NMT, debug, and heartbeat.

##### Response Flag

| Value | Meaning  |
|-------|----------|
| 0     | Request  |
| 1     | Response |

##### Source UID

8-bit unique identifier of the node that sent the frame. Must be unique on the bus.

---

#### 29-Bit Frames (Data)

Used for: request/response exchanges between nodes.

```
Bits 28-26: Priority        (3 bits)
Bit 25:     Broadcast       (1 bit)
Bit 24:     Response        (1 bit)
Bits 23-16: Reserved        (8 bits)
Bits 15-8:  Sender UID      (8 bits)
Bits 7-0:   Receiver UID    (8 bits)
```

##### Priority

3-bit priority field. Lower values win CAN arbitration first.

| Value | Name        |
|-------|-------------|
| 0     | Highest     |
| 1     |             |
| 2     |             |
| 3     |             |
| 4     |             |
| 5     |             |
| 6     |             |
| 7     | Lowest      |

##### Broadcast Flag

| Value | Meaning                                        |
|-------|------------------------------------------------|
| 0     | Targeted — Receiver UID specifies the destination |
| 1     | Broadcast — all nodes should receive this frame   |

##### Response Flag

| Value | Meaning  |
|-------|----------|
| 0     | Request  |
| 1     | Response |

##### Sender UID

8-bit unique identifier of the node that sent the frame.

##### Receiver UID

8-bit unique identifier of the intended recipient. Interpretation depends on the broadcast flag.

---

### Data Payload

CAN FD provides up to 64 bytes of data payload. The following fields are carried in the data payload, not in the CAN ID:

- Device class and board type (sender/receiver identification)
- Command or register ID
- Request ID (for matching responses to requests)
- Multi-frame segmentation metadata
- Board version

The data payload format is TBD.

---

### Addressing

Node UIDs are 8-bit values (0-255) and must be globally unique on the bus.

Both sender and receiver UIDs are present in 29-bit frames, which:
- Enables hardware filtering on both source and destination
- Guarantees unique CAN IDs (no collision between simultaneous transmitters)
- Allows any node to identify both parties in an exchange

11-bit frames always identify the source node. All 11-bit messages are broadcast.

---

### Arbitration Summary

CAN arbitration is bitwise — lower values win. The overall priority ordering is:

1. **11-bit Error** (highest — infrastructure frame, type 0b00)
2. **11-bit NMT** (infrastructure frame, type 0b01)
3. **11-bit Debug** (infrastructure frame, type 0b10)
4. **11-bit Heartbeat** (infrastructure frame, type 0b11)
5. **29-bit Priority 0** (highest data priority)
6. ...
7. **29-bit Priority 7** (lowest data priority)

Within the same 29-bit priority level, non-broadcast frames (broadcast=0) win over broadcast frames (broadcast=1). Within the same broadcast setting, requests (response=0) win over responses (response=1).

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


