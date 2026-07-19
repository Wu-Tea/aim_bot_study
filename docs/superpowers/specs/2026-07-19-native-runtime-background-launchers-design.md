# Native Runtime Background Launchers Design

## Goal

Provide two files the user can double-click to start and stop the canonical
native runtime without opening a persistent console window or entering a
command. The runtime remains visible and manageable in Windows Task Manager.

## User Interface

The two public entry points live in `scripts/launch`:

- `gamepad_native_background_start.vbs`
- `gamepad_native_background_stop.vbs`

VBScript is only the zero-window double-click shell. Each entry invokes its
matching PowerShell implementation with `wscript.exe` window style `0`. No
AutoFire or recoil prompt is shown; `config.toml` remains the source of truth.

## Start Behavior

`gamepad_native_background_start.ps1` resolves all paths relative to the
repository, validates the runtime executable and `config.toml`, and checks the
existing state file. If the recorded process is still the same executable, it
exits without starting a duplicate.

Otherwise it launches:

```text
native/vision_native/build/Release/cod_native_runtime.exe --config config.toml
```

using a hidden window and the repository as working directory. It records the
PID, normalized executable path, start time, and config path in
`runs/runtime/background/native_runtime_state.json`. Standard output and error
are redirected to files in the same directory so a console pipe cannot block
the runtime.

## Stop Behavior

`gamepad_native_background_stop.ps1` reads the state file, finds the recorded
PID, and compares the process executable path with the normalized recorded
path. It stops the process only when both identity checks match. A missing,
dead, malformed, or mismatched state is treated as stale state and never causes
a name-based bulk kill. The stale state file is removed after validation.

## Diagnostics

Both PowerShell scripts append concise lifecycle records to
`runs/runtime/background/launcher.log`. They support a `-PrintOnly` mode for
automated verification without starting or stopping the runtime. The public
VBS entries do not display dialogs during normal success; errors are recorded
in the launcher log.

## Tests

Extend `tests/test_startup_scripts.py` to verify:

- both VBS entries exist and invoke their corresponding PowerShell scripts with
  window style `0`;
- start resolves the canonical native runtime and `config.toml`;
- start uses `Start-Process` with hidden window, redirects output, and records a
  PID-backed state;
- stop validates both PID and executable path and does not use process-name
  bulk termination;
- both PowerShell scripts expose `-PrintOnly` and run successfully in that
  mode;
- existing foreground launchers remain unchanged.

## Boundaries

This feature does not hide a process from Task Manager, install a service,
register a scheduled task, change runtime controller behavior, or add an
automatic startup-at-login mechanism.
