# DBCAN Protocol Specification

DBCAN is an open-source CAN FD protocol using both standard (11-bit) and extended (29-bit) identifiers. The frame format itself encodes the first-level message type distinction: 11-bit frames carry infrastructure traffic, 29-bit frames carry data traffic. 11-bit frames inherently win CAN arbitration over 29-bit frames, ensuring infrastructure messages always take priority.

## CAN ID Layout

### 11-Bit Frames (Infrastructure)

Used for: errors, network management, debug, and heartbeat messages. These are always broadcast.

```
Bits 10-9:  Message Type    (2 bits)
Bit 8:      Reserved        (1 bit)
Bits 7-0:   Source UID      (8 bits)
```

#### Message Type Encoding

| Value | Name                | Description                          |
|-------|---------------------|--------------------------------------|
| 0b00  | Error               | Fault announcements                  |
| 0b01  | Network Management  | NMT commands (reset, sync, etc.)     |
| 0b10  | Debug               | Diagnostic and debug traffic         |
| 0b11  | Heartbeat           | Periodic health and presence         |

Lower values win CAN arbitration first, so errors have the highest priority, followed by NMT, debug, and heartbeat.

#### Source UID

8-bit unique identifier of the node that sent the frame. Must be unique on the bus.

---

### 29-Bit Frames (Data)

Used for: request/response exchanges between nodes.

```
Bits 28-26: Priority        (3 bits)
Bit 25:     Broadcast       (1 bit)
Bit 24:     Response        (1 bit)
Bits 23-16: Reserved        (8 bits)
Bits 15-8:  Sender UID      (8 bits)
Bits 7-0:   Receiver UID    (8 bits)
```

#### Priority

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

#### Broadcast Flag

| Value | Meaning                                        |
|-------|------------------------------------------------|
| 0     | Targeted — Receiver UID specifies the destination |
| 1     | Broadcast — all nodes should receive this frame   |

#### Response Flag

| Value | Meaning  |
|-------|----------|
| 0     | Request  |
| 1     | Response |

#### Sender UID

8-bit unique identifier of the node that sent the frame.

#### Receiver UID

8-bit unique identifier of the intended recipient. Interpretation depends on the broadcast flag.

---

## Data Payload

CAN FD provides up to 64 bytes of data payload. The following fields are carried in the data payload, not in the CAN ID:

- Device class and board type (sender/receiver identification)
- Command or register ID
- Request ID (for matching responses to requests)
- Multi-frame segmentation metadata
- Board version

The data payload format is TBD.

---

## Addressing

Node UIDs are 8-bit values (0-255) and must be globally unique on the bus.

Both sender and receiver UIDs are present in 29-bit frames, which:
- Enables hardware filtering on both source and destination
- Guarantees unique CAN IDs (no collision between simultaneous transmitters)
- Allows any node to identify both parties in an exchange

11-bit frames always identify the source node. All 11-bit messages are broadcast.

---

## Arbitration Summary

CAN arbitration is bitwise — lower values win. The overall priority ordering is:

1. **11-bit Error** (highest — infrastructure frame, type 0b00)
2. **11-bit NMT** (infrastructure frame, type 0b01)
3. **11-bit Debug** (infrastructure frame, type 0b10)
4. **11-bit Heartbeat** (infrastructure frame, type 0b11)
5. **29-bit Priority 0** (highest data priority)
6. ...
7. **29-bit Priority 7** (lowest data priority)

Within the same 29-bit priority level, non-broadcast frames (broadcast=0) win over broadcast frames (broadcast=1). Within the same broadcast setting, requests (response=0) win over responses (response=1).
