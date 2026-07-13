# Service Supervision And Permissions

Service Manager ABI v2 keeps lifecycle policy in `serviced_c.elf` while the
kernel enforces process permissions at syscall boundaries.

## Lifecycle States

Managed services use `stopped`, `starting`, `running`, `stopping`, and `failed`.
Every transition receives a monotonic manager generation. A registered kernel
service corresponds to the manager's `running` state; registry disappearance
while running is treated as an unexpected exit.

Manager replies also report health, the last failure, restart policy, restart
count, and the static permission mask.

## Timeouts And Health

- start must produce a registry entry within 50 PIT ticks;
- stop must remove the registry entry within 50 PIT ticks;
- health requests must return a correlated service health reply within 25 PIT
  ticks;
- timeout and unresponsive failures remain visible through `service status`.

Use `service health <name>` for an active probe. Failed starts run their bounded
restart transaction before the manager replies. For already-running services,
mailbox traffic and status/health requests drive supervisor passes; this keeps
policy in user space without adding an independent kernel worker thread.

## Restart Policy

Services use either `disabled` or `on-failure`. On-failure restart is bounded
to three attempts with a 20-tick backoff. Exhaustion records
`OS_SERVICE_FAILURE_RESTART_LIMIT`; it does not spin or keep allocating process
slots.

The complete dependency graph is validated before the manager registers.
Missing dependencies and cycles are rejected. A running dependency cannot be
stopped while a dependent service is active.

## Permission Metadata V1

Ordinary launches retain `OS_PROCESS_PERMISSION_ALL` for compatibility.
`serviced_c.elf` launches managed children with `os_run_with_permissions()` and
the kernel assigns the mask before entering user mode.

Permissions cover:

- service discovery;
- service registration;
- IPC send and receive;
- input APIs;
- display APIs;
- transferable/shared surfaces (reserved for the Phase 4 surface syscalls);
- restricted child management.

Missing permissions return `OS_ERR_PERMISSION_DENIED`. The `restricted` sample
service verifies that display, service discovery, and child launch are denied
while its declared registration and IPC operations remain available.

## Regression Coverage

`make test-service-supervision` covers lifecycle state reporting, dependency
ordering and stop protection, health checks, permission denial, start timeout,
bounded restart, registry cleanup, and 1,000 register/query/stop/crash/restart
identity cycles. `make test-services` includes this target.
