Option Explicit
Dim shell, fs, project, executable, command
Set shell = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
project = fs.GetParentFolderName(WScript.ScriptFullName)
executable = fs.BuildPath(project, "native\build\Release\cod_native_assistant.exe")
If Not fs.FileExists(executable) Then
    shell.Popup "Build tools/build_native_runtime.ps1 first.", 0, "Native Assistant", 16
    WScript.Quit 1
End If
shell.CurrentDirectory = project
command = """" & executable & """ --project """ & project & """"
shell.Run command, 1, False
