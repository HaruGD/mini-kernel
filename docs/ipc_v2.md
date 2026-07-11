# IPC v2

IPC v2 extends the Phase 3 fixed mailbox ABI without breaking v1 programs.
The original `OsIpcMessage` remains 88 bytes and keeps using the existing
send, receive, and wait syscalls. New programs can opt into `OsIpcMessageV2`
and explicit feature discovery.

## Feature Discovery

`os_ipc_features()` reports:

- IPC ABI version `2`
- `OS_IPC_FEATURE_V2`
- `OS_IPC_FEATURE_CORRELATION`
- `OS_IPC_FEATURE_HANDLE_TRANSFER`

Existing v1 programs do not need to call this API.

## Message Shape

`OsIpcMessageV2` adds:

- `abi_version`
- `sender_pid`
- `sender_generation`
- `request_id`
- `reply_to`
- `handle_count`
- two handle slots
- a 96-byte payload

The kernel overwrites sender identity and zero-fills unused payload and handle
slots before enqueueing.

## Correlation

Requests should set `request_id`. Replies should set `reply_to` to the original
request id. `OsIpcReceiveFilter` can match by sender identity, message type, and
`reply_to`.

`os_msg_v2_request()` sends a request and waits for the matching reply by using
`recv_match`, so unrelated mailbox messages cannot satisfy the request.

## Handle Transfer

IPC v2 can carry up to two handle tokens. The kernel validates each source
handle against `KERNEL_HANDLE_RIGHT_TRANSFER`, creates receiver-owned handle
entries atomically, and rolls back partial transfers on failure.

Current VFS file and directory handles are not transferable because they are not
created with `TRANSFER` rights. Shared-memory and graphics-surface handles are
the intended first transferable object families.

## Backpressure

Mailboxes remain bounded at 16 queued messages. Send is nonblocking:

- success returns `0`;
- a full receiver mailbox returns `IPC_ERR_QUEUE_FULL`;
- dropped counters record saturation;
- failed handle transfers are rolled back before returning.

Blocking receive remains available through the v1 wait syscall. IPC v2 request
helpers use `recv_match` plus bounded sleep/yield polling so they can wait for a
specific correlated reply without consuming unrelated messages.

## Compatibility

The kernel mailbox stores messages internally as v2 envelopes. v1 send and
receive paths convert at the syscall boundary, preserving the v1 ABI and
truncating only when a v1 receiver intentionally reads a v2-sized payload.
