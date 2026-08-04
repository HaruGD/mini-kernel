# OS64 Terminal SDK

Phase 5D provides a user-space terminal frontend and shell backend without
granting either process direct display or raw-input authority. `terminal.elf`
owns a normal Window SDK surface. `terminal_shell.elf` reuses the existing C
shell command engine with an inheritable kernel terminal session.

## Protocol

`OsTerminalPacket` is a fixed 96-byte IPC v2 payload with ABI version 1. Every
packet carries a nonzero monotonic sequence and one of these commands:

- `HELLO`: backend identity and initial grid;
- `OUTPUT`: up to 60 output bytes;
- `INPUT`: up to 60 focused-window input bytes;
- `RESIZE`: validated columns and rows;
- `HANGUP`: frontend teardown request;
- `EXIT`: backend exit status.

Both endpoints bind messages to the complete PID plus generation identity and
reject malformed sizes, versions, flags, commands, dimensions, lengths, and
stale sequences. `HELLO`, `INPUT`, `RESIZE`, and `HANGUP` use IPC v2. After
`os_terminal_session_bind`, standard writes from the backend and every child it
starts enter a separate dynamically allocated 256-packet terminal stream.
`os_terminal_session_read` drains that stream without consuming the frontend's
general IPC mailbox. A bounded queue overflow marks the next delivered output
packet explicitly. Input has a separate 256-byte ring.

## Terminal Model

`OsTerminalModel` supports 20-120 columns, 5-60 visible rows, and a fixed
128-row history. It handles printable ASCII, newline, carriage return,
backspace, tab stops, a visible cursor, and these baseline ANSI CSI controls:

- SGR reset plus normal/bright 8-color foreground and background;
- absolute cursor position and relative up/down/left/right movement;
- display clear, line erase, and cursor save/restore.

History never allocates while parsing output. Once full, the oldest row is
dropped and `dropped_history_rows` advances. `os_terminal_model_scroll`
navigates the retained history; new output returns the live view to the tail.

## Lifecycle And Authority

The frontend starts its backend with `IPC | MANAGE_CHILD`, receives `HELLO`
before creating the window, then backgrounds the paused child. The backend
binds its parent as the only terminal peer; descendants inherit that binding.
`SYS_WRITE`, `SYS_PUTCHAR`, `SYS_GETCHAR`, screen clearing, kernel-backed file
commands, FAT32 listing, and process diagnostics honor the binding. Keyboard
bytes arrive only through focused `OS_WINDOW_EVENT_KEY` events. When a nested
interactive child is active, only the newest descendant is woken to consume
the shared input stream. A configure event recomputes the grid and sends
`RESIZE`.

Normal `exit`, frontend hangup, and injected child loss all converge on one
bounded teardown path. The frontend waits briefly after `HANGUP`, kills only
an unresponsive owned child, drains a final `EXIT` packet, and reaps child
records exactly once and closes the dynamically allocated stream.
`Ctrl+Shift+Q` requests terminal hangup; `Ctrl+Up`/`Ctrl+Down` navigate
scrollback. `Ctrl+Shift+K` is enabled only by the `--fault-test` launch mode and
is not active in a normal terminal.

The GUI terminal now carries shell-native output, legacy filesystem commands,
external ELF output, and inherited interactive ELF input. Kernel diagnostics
remain on the serial/kernel console unless they are explicitly part of a
user-requested command response.
