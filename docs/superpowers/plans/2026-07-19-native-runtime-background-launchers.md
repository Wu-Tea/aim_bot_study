# Native Runtime Background Launchers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two zero-window double-click entries that safely start and stop the canonical native gamepad runtime.

**Architecture:** VBScript entry files provide the no-console double-click surface and delegate to focused PowerShell lifecycle scripts. The start script owns a JSON state record; the stop script requires both PID and normalized executable-path identity before terminating anything.

**Tech Stack:** VBScript/WScript, Windows PowerShell 5.1, Python unittest, native C++ runtime executable.

---

### Task 1: Define launcher behavior with failing tests

**Files:**
- Modify: `tests/test_startup_scripts.py`

- [ ] **Step 1: Add structural contract tests**

Add tests that require four new files, assert each VBS file invokes its matching
PowerShell file through `WScript.Shell.Run` with window style `0`, and inspect
the PowerShell sources for `-PrintOnly`, `Start-Process`, hidden-window output
redirection, JSON state, PID lookup, and `ExecutablePath` comparison. Assert the
stop source does not contain `Get-Process -Name`, `taskkill /IM`, or another
name-based bulk termination path.

- [ ] **Step 2: Add executable PrintOnly tests**

Invoke each PowerShell script with:

```python
subprocess.run(
    ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
     "-File", str(script), "-PrintOnly"],
    cwd=PROJECT_ROOT,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    check=False,
)
```

Require exit code `0`, parse the JSON output, and verify the start preview points
to `native/vision_native/build/Release/cod_native_runtime.exe` plus
`config.toml`, while the stop preview points to the state file under
`runs/runtime/background`.

- [ ] **Step 3: Run tests and witness RED**

```powershell
python -m pytest tests/test_startup_scripts.py -q
```

Expected: the new tests fail because the four launcher files do not exist.

### Task 2: Implement the PowerShell lifecycle core

**Files:**
- Create: `scripts/launch/gamepad_native_background_start.ps1`
- Create: `scripts/launch/gamepad_native_background_stop.ps1`

- [ ] **Step 1: Implement start validation and preview**

The start script accepts `[switch]$PrintOnly`, resolves the project root from
`$PSScriptRoot`, and defines absolute executable, config, state, stdout, stderr,
and lifecycle-log paths. In PrintOnly mode it emits one JSON object and exits
without creating directories or processes.

- [ ] **Step 2: Implement duplicate protection and state ownership**

If state exists, parse it and query `Win32_Process` by the recorded PID. Treat it
as already running only when `ExecutablePath` matches case-insensitively. Remove
malformed/dead/mismatched stale state. Before launching, refuse to adopt or
duplicate any independently running process whose executable path matches but
has no owned state.

- [ ] **Step 3: Implement hidden launch and atomic state write**

Use `Start-Process -WindowStyle Hidden -PassThru`, repository working directory,
quoted `--config` argument, and separate stdout/stderr redirection. Serialize
PID, executable path, config path, and UTC start time to a temporary JSON file,
then move it over the canonical state file.

- [ ] **Step 4: Implement stop identity checks**

The stop script accepts `[switch]$PrintOnly`. Outside preview mode it parses the
owned state, queries the recorded PID, compares `ExecutablePath`, and only then
calls `Stop-Process -Id`. Missing/dead/malformed/mismatched state is cleaned up
without terminating a process. Append lifecycle/error messages to
`launcher.log`.

### Task 3: Implement zero-window double-click entries

**Files:**
- Create: `scripts/launch/gamepad_native_background_start.vbs`
- Create: `scripts/launch/gamepad_native_background_stop.vbs`

- [ ] **Step 1: Add the VBS start entry**

Resolve the sibling PowerShell file from `WScript.ScriptFullName` and invoke:

```vbscript
shell.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & scriptPath & """", 0, False
```

- [ ] **Step 2: Add the VBS stop entry**

Use the identical no-window invocation pattern for
`gamepad_native_background_stop.ps1`. Do not display success dialogs.

- [ ] **Step 3: Run launcher tests and witness GREEN**

```powershell
python -m pytest tests/test_startup_scripts.py -q
```

Expected: all startup-script tests pass.

### Task 4: Verify real lifecycle and commit

**Files:**
- Verify: `runs/runtime/background/native_runtime_state.json`
- Verify: `runs/runtime/background/launcher.log`

- [ ] **Step 1: Run controlled start/stop smoke**

First verify no existing `cod_native_runtime.exe` instance is active. Invoke the
PowerShell start script once, verify the state PID is alive with the expected
executable path, invoke start again and verify the PID is unchanged, then invoke
stop and verify both process and state are gone. Do not run this smoke if an
independently launched runtime is active.

- [ ] **Step 2: Verify repository state**

Run `git diff --check` and confirm generated state/log files remain ignored.

- [ ] **Step 3: Commit implementation**

```powershell
git add tests/test_startup_scripts.py scripts/launch/gamepad_native_background_start.ps1 scripts/launch/gamepad_native_background_stop.ps1 scripts/launch/gamepad_native_background_start.vbs scripts/launch/gamepad_native_background_stop.vbs
git commit -m "feat: add background native runtime launchers"
```
