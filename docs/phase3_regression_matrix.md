# Phase 3 Regression Matrix

Phase 3 establishes bounded IPC, service discovery, and the first user-space
service manager on the active UEFI kernel. Services remain ordinary ELF user
programs, while the kernel provides transport, ownership, and cleanup.

| ID | Area | Scenario | Expected result | Status | Automated by |
| --- | --- | --- | --- | --- | --- |
| M01 | IPC ABI | Shared message layout, limits, flags, and result codes | Kernel and SDK layouts remain identical | Automated | Compile-time assertions and `make test-ipc` |
| M02 | Mailbox | Empty/full state, FIFO order, wraparound, and overflow | Messages retain order and queue counters remain correct | Automated | `make test-ipc` |
| M03 | IPC validation | Invalid target, self-send, bad length, and bad user pointers | Requests fail without reading kernel or foreign memory | Automated | `make test-ipc` and `make test-user-sdk` |
| M04 | Blocking receive | Receiver sleeps until a message arrives | One receiver wakes without busy waiting | Automated | `make test-ipc` |
| M05 | Process cleanup | Sender or receiver exits with queued messages or waiters | Mailboxes reset and stale waiters return a stable error | Automated | `make test-ipc` |
| M06 | IPC diagnostics | Inspect queue depth and delivery/drop counters | Inspection does not consume messages | Automated | `make test-ipc` |
| M07 | IPC sample | `uping_c.elf` exchanges a request and reply with `upong_c.elf` | Payload and sender identity are verified | Automated | `make test-ipc` |
| S01 | Service ABI | Validate names, info records, states, and generations | Invalid and duplicate identities are rejected | Automated | `make test-services` |
| S02 | Registry ownership | Register, find, and unregister a service | Only the owner can unregister its service | Automated | `make test-services` |
| S03 | Registry cleanup | Service owner exits or fails | Stale pids are removed before lookup | Automated | `make test-services` |
| S04 | Service manager | Start and query a known service | Repeated start is idempotent | Automated | `make test-services` |
| S05 | Service lifecycle | Stop and restart managed children | Registry and process slots are reclaimed | Automated | `make test-services` |
| S06 | Dependencies | Start a service with a static prerequisite | Dependency starts first and cycles are bounded | Automated | `make test-services` |
| S07 | Reply correlation | Client receives unrelated or stale messages | Sender, command, and request id must match | Automated | `make test-services` |
| S08 | Shell frontend | Route `service` commands through `usvcctl_c.elf` | Policy remains in user space | Automated | `make test-services` |
| V01 | Input service | Start `inputd_c.elf` and query status | Registry lookup and IPC reply succeed | Automated | `make test-services` |
| V02 | Display service | Start `displayd_c.elf` and query display info | Dimensions and status are returned over IPC | Automated | `make test-services` |
| V03 | Service client | Run `usvcprobe_c.elf` against both services | Client reports success without manual inspection | Automated | `make test-services` |
| V04 | Nested shutdown | Stop services and exit the manager | Parent shell resumes and process slots remain usable | Automated | `make test-services` |

Run the full Phase 3 closure baseline with:

```sh
make clean
make all
make uefi
make test-phase1
python3 tools/uefi_smoke.py
python3 tools/uefi_userland_smoke.py
python3 tools/uefi_screen_smoke.py
make test-user-sdk
make test-graphics
make test-input
make test-ipc
make test-services
```

The latest Phase 3 closure run passed this baseline on QEMU/OVMF.
