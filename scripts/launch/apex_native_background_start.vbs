Option Explicit
Dim shell, fileSystem, scriptDirectory, scriptPath, command, exitCode
Set shell = CreateObject("WScript.Shell")
Set fileSystem = CreateObject("Scripting.FileSystemObject")
scriptDirectory = fileSystem.GetParentFolderName(WScript.ScriptFullName)
scriptPath = fileSystem.BuildPath(scriptDirectory, "gamepad_native_background_start.ps1")
command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & scriptPath & """ -Game apex"
exitCode = shell.Run(command, 0, True)
If exitCode <> 0 Then shell.Popup "Apex start failed. Stop the other game runtime first, or check runs\runtime\background\apex\launcher.log.", 0, "Apex", 16
WScript.Quit exitCode
