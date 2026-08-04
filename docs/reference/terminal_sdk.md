# OS64 Terminal SDK

Phase 5D provides a user-space terminal frontend and shell backend without
granting either process direct display or raw-input authority. `terminal.elf`
owns a normal Window SDK surface. `terminal_shell.elf` reuses the existing C
shell command engine with an IPC I/O adapter.

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
stale sequences. IPC mailbox capacity supplies the hard queue bound.
`os_terminal_send` retries queue pressure only for the caller-provided tick
budget. A backend that exhausts that budget marks the next delivered output
packet with `OS_TERMINAL_FLAG_OVERFLOW`; the frontend renders an explicit
truncation marker. Input has a separate 256-byte backend ring and reports its
own overflow.

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
before creating the window, then backgrounds the paused child. Keyboard bytes
arrive only through focused `OS_WINDOW_EVENT_KEY` events. A configure event
uses `os_window_apply_configure`, recomputes the grid, redraws the model, and
sends `RESIZE`.

Normal `exit`, frontend hangup, and injected child loss all converge on one
bounded teardown path. The frontend waits briefly after `HANGUP`, kills only
an unresponsive owned child, drains a final `EXIT` packet, and reaps child
records exactly once. `Ctrl+Shift+Q` requests terminal hangup;
`Ctrl+Up`/`Ctrl+Down` navigate scrollback. `Ctrl+Shift+K` is currently retained
as a diagnostic child-failure injection shortcut.

The first backend redirects the shared shell engine itself. Legacy commands
whose kernel syscalls print directly to the kernel console have not yet been
converted to stream-returning SDK calls; native terminal-output commands such
as `echo`, prompts, path handling, and shell diagnostics use the GUI stream.
