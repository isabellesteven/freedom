Good question — this file is important because it defines the **stable contract between PC tools and runtime**, and it’s one of the few things you really don’t want to churn later.

Below is what `protocol_v1.md` should contain, structured as an engineering-ready spec.

---

# protocol_v1.md — Contents

Implementation status:

* this document specifies a planned protocol surface
* the current repository does not yet implement the runtime protocol described here

This document defines the **control and tuning protocol** between:

* PC tooling (CLI / GUI / automation)
* Target runtime (PC-native or embedded)

It should contain:

---

# 1. Protocol Overview

### 1.1 Goals

* Transport-independent (UART / USB / TCP / stdio)
* Binary and compact
* Deterministic and easy to parse
* Safe for real-time tuning
* Versioned
* Works for PC-native and embedded targets

### 1.2 Non-goals (v1)

* No streaming audio over protocol
* No partial graph edits
* No dynamic graph construction commands
* No remote file system browsing

---

# 2. Framing Layer

This section defines how messages are framed on the wire.

## 2.1 Byte Order

* All multi-byte fields are **little-endian**

## 2.2 Frame Format

Every message is:

```
[magic u16]
[version u8]
[msg_type u8]
[seq u16]
[length u32]
[payload bytes]
[crc32 u32]
```

### Field Definitions

| Field    | Size | Description                        |
| -------- | ---- | ---------------------------------- |
| magic    | 2    | `0x4750` ("PG" for Pipeline Graph) |
| version  | 1    | protocol major version (1)         |
| msg_type | 1    | message type enum                  |
| seq      | 2    | sequence number from sender        |
| length   | 4    | payload length in bytes            |
| payload  | N    | message-specific                   |
| crc32    | 4    | CRC of all bytes except crc field  |

---

# 3. Message Semantics

Define the lifecycle model.

## 3.1 State Machine (Target)

Target states:

* BOOT
* IDLE (no graph active)
* GRAPH_LOADED (stored but not active)
* GRAPH_ACTIVE

Allowed transitions:

* BOOT → IDLE
* IDLE → GRAPH_LOADED
* GRAPH_LOADED → GRAPH_ACTIVE
* GRAPH_ACTIVE → IDLE

Include a diagram in the doc (ASCII is fine).

---

# 4. Message Types

Define enums:

```c
enum MsgType : uint8_t {
  MSG_HELLO              = 1,
  MSG_HELLO_REPLY        = 2,
  MSG_LIST_MODULES       = 3,
  MSG_LIST_MODULES_REPLY = 4,
  MSG_LOAD_GRAPH         = 5,
  MSG_LOAD_REPLY         = 6,
  MSG_ACTIVATE           = 7,
  MSG_ACTIVATE_REPLY     = 8,
  MSG_SET_PARAM          = 9,
  MSG_SET_PARAM_REPLY    = 10,
  MSG_SET_PARAM_BULK     = 11,
  MSG_GET_METRICS        = 12,
  MSG_GET_METRICS_REPLY  = 13,
  MSG_ERROR              = 255
};
```

---

# 5. Message Payload Definitions

Each message gets a precise payload layout.

---

## 5.1 HELLO

Payload (request):

```
u32 client_version
```

Reply:

```
u32 runtime_version
u32 target_abi
u32 capabilities_flags
```

---

## 5.2 LIST_MODULES

Request:
(empty payload)

Reply:

```
u32 module_count
repeat:
  u32 module_id
  u16 ver_major
  u16 ver_minor
```

---

## 5.3 LOAD_GRAPH

Payload:

```
u32 blob_bytes
u8  blob[blob_bytes]
```

Behavior:

* Store blob to inactive slot
* Verify CRC
* Validate structure
* Do NOT activate

Reply:

```
u32 status_code
```

---

## 5.4 ACTIVATE

Payload:
(empty)

Behavior:

* At next safe boundary:

  * stop current graph
  * instantiate new graph
  * swap active pointer

Reply:

```
u32 status_code
```

---

## 5.5 SET_PARAM

Payload:

```
u32 node_id
u32 param_id
u32 value_bytes
u8  value[value_bytes]
```

Behavior:

* Enqueue parameter change
* Apply at block boundary
* If param page double-buffering is used:

  * write to staging page
  * swap on commit

Reply:

```
u32 status_code
```

---

## 5.6 SET_PARAM_BULK

Payload:

```
u32 entry_count
repeat:
  u32 node_id
  u32 param_id
  u32 value_bytes
  u8  value[value_bytes]
```

---

## 5.7 GET_METRICS

Reply:

```
u32 cpu_percent_x100
u32 xruns
u32 max_block_time_us
```

---

## 5.8 ERROR

Payload:

```
u32 error_code
u16 message_len
u8  message[message_len]
```

---

# 6. Real-Time Safety Rules

This section is critical and should be explicit.

### 6.1 Audio Thread Constraints

* Must not parse frames
* Must not allocate memory
* Must process bounded number of commands per block
* Param changes applied only at block boundary

### 6.2 Command Queue

Define:

* lock-free ring buffer
* max queue depth
* overflow behavior (drop or error)

---

# 7. Error Codes

Define stable error codes:

```c
enum ErrorCode {
  ERR_OK = 0,
  ERR_BAD_CRC = 1,
  ERR_UNSUPPORTED_VERSION = 2,
  ERR_INVALID_BLOB = 3,
  ERR_MODULE_MISSING = 4,
  ERR_INVALID_STATE = 5,
  ERR_PARAM_INVALID = 6,
  ERR_INTERNAL = 255
};
```

---

# 8. Versioning Rules

This section prevents future pain.

* `version` field in frame header is **major version**
* Backward compatible additions:

  * new message types allowed
  * new fields must be appended
* Unknown message types:

  * reply with ERR_UNSUPPORTED_VERSION or ERR_INVALID_MSG

---

# 9. Transport Independence

Define that:

* Framing is identical across:

  * TCP
  * UART
  * USB CDC
  * stdio (PC-native)
* No transport-specific logic inside message parser
* Transport only provides byte stream

---

# 10. Security (v1 minimal)

* No authentication in v1
* Intended for local dev environments
* Future versions may add:

  * authentication
  * signature verification
  * encrypted channel

---

# What protocol_v1.md should NOT contain

* Blob format (that’s in blob_v1.md)
* Module ABI (that’s in abi_v1.md)
* Scheduling details
* GUI behavior

It should strictly define **wire behavior and runtime interaction contract**.

---

# Design Philosophy Behind This Protocol

Your protocol is intentionally:

* Coarse-grained (load whole graph)
* Simple state machine
* Minimal commands
* Deterministic
* Embedded-friendly
* Easy to fuzz-test


