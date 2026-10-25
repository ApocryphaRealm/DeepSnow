@echo off
setlocal
REM Compiles tools\surface_rules_check.cpp against the plugin's own include\SurfaceRules.h and runs it with the shipped
REM INI's [Surfaces] lists. See the .cpp for what it proves. Optional argument: a names.tsv of a load order's landscape
REM textures (form, LTEX editor ID, TXST editor ID, diffuse, normal - tab separated) to classify every row.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set "OUT=%TEMP%\deepsnow-surface-rules-check"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%~dp0..\include" /Fo"%OUT%\\" /Fe"%OUT%\surface_rules_check.exe" "%~dp0surface_rules_check.cpp" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\surface_rules_check.exe" "%~dp0..\dist\SKSE\Plugins\DeepSnow.ini" %*
