Option Explicit
Dim shell, fs, project, python, command
Set shell = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
project = fs.GetParentFolderName(WScript.ScriptFullName)
python = "D:\env\python\pythonw.exe"
If Not fs.FileExists(python) Then
    shell.Popup "Python runtime not found: " & python, 0, "Gamepad Assistant", 16
    WScript.Quit 1
End If
shell.Environment("Process")("PYTHONPATH") = fs.BuildPath(project, "python") & ";" & shell.Environment("Process")("PYTHONPATH")
shell.CurrentDirectory = project
command = """" & python & """ -m desktop_app.gui"
shell.Run command, 0, False
