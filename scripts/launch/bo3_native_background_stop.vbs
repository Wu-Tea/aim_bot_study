Option Explicit
Dim shell, fileSystem, scriptDirectory, scriptPath, command, exitCode
Set shell = CreateObject("WScript.Shell")
Set fileSystem = CreateObject("Scripting.FileSystemObject")
scriptDirectory = fileSystem.GetParentFolderName(WScript.ScriptFullName)
scriptPath = fileSystem.BuildPath(scriptDirectory, "gamepad_native_background_stop.ps1")
command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & scriptPath & """ -Game bo3"
exitCode = shell.Run(command, 0, True)
If exitCode <> 0 Then shell.Popup "BO3 stop failed. Open the root GUI to check status and logs.", 0, "BO3", 16
WScript.Quit exitCode
