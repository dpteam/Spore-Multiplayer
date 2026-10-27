@echo off
REM Builds the 32 bit DLL injector used for local multiplayer testing.
REM Requires the Visual Studio C++ toolset (vcvars32.bat).

setlocal
set VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" set VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat

if not exist "%VCVARS%" (
    echo Could not find vcvars32.bat. Open a "x86 Native Tools Command Prompt" and run:
    echo     cl /EHsc /O2 /Fe:mp-inject.exe mp-inject.cpp
    exit /b 1
)

call "%VCVARS%" >nul
cl /nologo /EHsc /O2 /W3 /Fe:mp-inject.exe mp-inject.cpp
if errorlevel 1 exit /b 1

echo Built %CD%\mp-inject.exe
