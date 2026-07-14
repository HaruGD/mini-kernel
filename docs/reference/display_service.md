# Display Service And Present ABI

Phase 4C makes `displayd_c.elf` the sole normal user-space owner of physical
display presentation. Normal GUI applications create and draw into page-backed
surfaces, discover `display`, and submit the surface through the public User SDK.
They do not receive `DISPLAY` permission and cannot call the raw graphics
present syscall.

## Transaction

The public ABI is `OS64_DISPLAY_ABI_VERSION == 1` in
`include/os64/display_types.h`. One logical frame uses this ordered sequence:

```text
PRESENT_BEGIN(surface handle, generation, dimensions, format, totals)
  -> PRESENT_DAMAGE(chunk 0, up to 4 rectangles)
  -> PRESENT_DAMAGE(chunk 1, ...)
  -> PRESENT_COMMIT(generation)
  <- PRESENT_REPLY(result, accepted generation, presented rectangle count)
```

`PRESENT_BEGIN` transfers exactly one graphics-surface handle. IPC attenuation
gives `displayd` only `READ | MAP`; write and re-transfer rights are removed.
Full-frame submissions set `OS_DISPLAY_PRESENT_FLAG_FULL_FRAME` and contain no
damage chunks. Partial submissions contain 1 through 64 rectangles in ordered
chunks of at most four. The SDK call is:

```c
long os_display_present(OsProcessIdentity display,
                        OsHandle surface,
                        uint32_t frame_generation,
                        const OsRect* rects,
                        uint32_t rect_count,
                        uint32_t timeout_ticks,
                        OsDisplayPresentReply* reply);
```

`displayd` accepts one transaction at a time. It binds every chunk and commit
to the initiating PID plus process generation, request id, and frame
generation. It rejects stale or duplicated generations, malformed ABI fields,
missing/reordered/oversized chunks, invalid surface metadata, and unexpected
senders. A newer valid full-frame begin may replace a pending transaction so a
bounded queue cannot preserve obsolete partial state. Incomplete transactions
time out and release their mapping and handle.

## Display Boundary

The kernel `DisplayBackendOps` boundary validates the display-sized source,
clips and merges damage a second time, and calls the current GOP implementation.
The interface is independent of GOP so a later hardware display driver can
replace the backend without changing the user protocol.

The raw present syscall additionally requires all of the following:

- the caller has `OS_PROCESS_PERMISSION_DISPLAY`;
- the caller is the PID plus owner generation currently registered as
  `display`;
- the received handle is a graphics surface with `READ | MAP` and has neither
  `WRITE` nor `TRANSFER` rights.

The existing SDK graphics test binaries remain explicit diagnostic exceptions;
ordinary GUI application profiles are not exceptions. The frozen
`OsServiceInfo` v1 layout keeps its service-registration generation, while
`os_service_find_owner_identity` returns the distinct owner PID plus process
generation needed for identity-safe IPC and authority checks.

## Supervision And Fallback

`serviced_c.elf` starts `displayd_c.elf` with service registration, IPC,
shared-surface, and display permissions and applies bounded restart-on-failure.
The kernel terminal remains available while the service is absent. The
diagnostic `service crash display` command kills the managed child without
marking an explicit stop, allowing the automatic restart and terminal fallback
path to be tested reproducibly.

## Verification

```sh
make test-display-contracts
make test-display-present
```

The host target checks transaction ordering, stale/duplicate/missing/oversized
input, sender identity, full-frame replacement, display clipping, dimensions,
format, backend failure, and statistics. The QEMU target checks a restricted
client, direct-display denial, full and partial pixel results at 800x600,
generation ACKs, concurrent terminal activity, forced display-service crash,
terminal fallback, automatic restart, resubmission, and transient resource
cleanup.
