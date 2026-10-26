@echo off
setlocal
REM Compiles tools\editor_ids_check.cpp against the plugin's own include\PluginEditorIDs.h and runs it on the plugins
REM given as arguments (add --expect COTN_LRoadStone01 to require that name). Read-only. See the .cpp for what it proves.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set "OUT=%TEMP%\deepsnow-editor-ids-check"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\editor_ids_check.exe" "%~dp0editor_ids_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\editor_ids_check.exe" %*
