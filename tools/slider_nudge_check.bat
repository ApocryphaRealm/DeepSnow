@echo off
setlocal
REM Compiles tools\slider_nudge_check.cpp against the framework's own Dear ImGui build (1.90.8 with AMF's overlay
REM patches) and runs it headless. See the .cpp for what it proves.
REM ImGui comes from AMF_IMGUI_ROOT when it is set (a vcpkg_installed triplet folder holding include\imgui.h and
REM lib\imgui.lib), otherwise from the Apocrypha Menu Framework checkout beside this repo once its configure.bat has run.
call "%~dp0..\find-msvc.bat"
if errorlevel 1 exit /b 1
set "VI=%AMF_IMGUI_ROOT%"
if "%VI%"=="" set "VI=%~dp0..\..\ApocryphaMenuFramework\build\relwithdebinfo-se-only\vcpkg_installed\x64-windows-static-md"
if not exist "%VI%\lib\imgui.lib" (
    echo slider_nudge_check: no ImGui build at "%VI%" - set AMF_IMGUI_ROOT, or run the framework's configure.bat once
    exit /b 1
)
set "OUT=%TEMP%\deepsnow-slider-nudge-check"
if not exist "%OUT%\inc" mkdir "%OUT%\inc"
REM the plugin's own headers, with the ImGuiMCP stand-in beside them (their #include "SKSEMenuFramework.h" finds it first)
copy /y "%~dp0..\include\PreciseSlider.h" "%OUT%\inc\" >nul || exit /b 1
copy /y "%~dp0..\include\SliderNudge.h" "%OUT%\inc\" >nul || exit /b 1
copy /y "%~dp0imgui-shim\SKSEMenuFramework.h" "%OUT%\inc\" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /MD /I"%OUT%\inc" /I"%VI%\include" /Fo"%OUT%\\" /Fe"%OUT%\slider_nudge_check.exe" "%~dp0slider_nudge_check.cpp" "%VI%\lib\imgui.lib" >"%OUT%\cl.log" || (type "%OUT%\cl.log" & exit /b 1)
"%OUT%\slider_nudge_check.exe" %*
