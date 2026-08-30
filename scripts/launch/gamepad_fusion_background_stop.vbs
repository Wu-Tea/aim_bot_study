Option Explicit

Dim shell, fileSystem, scriptDirectory, scriptPath, projectRoot, logPath
Dim command, exitCode, message
Set shell = CreateObject("WScript.Shell")
Set fileSystem = CreateObject("Scripting.FileSystemObject")
scriptDirectory = fileSystem.GetParentFolderName(WScript.ScriptFullName)
scriptPath = fileSystem.BuildPath(scriptDirectory, "gamepad_fusion_background_stop.ps1")
projectRoot = fileSystem.GetParentFolderName(fileSystem.GetParentFolderName(scriptDirectory))
logPath = fileSystem.BuildPath(projectRoot, "runs\fusion_canvas\background\launcher.log")
command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & scriptPath & """"
exitCode = shell.Run(command, 0, True)
If exitCode <> 0 Then
    message = "Fusion background stop failed (exit code " & exitCode & ")." & vbCrLf & _
        "See launcher diagnostics:" & vbCrLf & logPath
    shell.Popup message, 0, "Fusion shutdown failed", 16
End If
