@echo off
rem ---------------------------------------------------------------------------
rem Syntax probe: feed selected .cpp files to the real MSVC compiler (/Zs =
rem syntax check only) against the minimal Qt stubs in this directory.
rem
rem This does NOT validate Qt API usage (the stubs are written to match our own
rem usage). What it DOES validate: that our C++ actually parses, that our own
rem identifiers resolve, and that the Windows crypto code in translatorconfig.cpp
rem goes through a real compiler.
rem
rem Only files whose Qt dependencies are covered by _core.h can be checked.
rem Keep this file ASCII-only so it is immune to console code pages.
rem ---------------------------------------------------------------------------
setlocal

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
	echo [syntax-probe] vcvars64.bat not found: "%VCVARS%"
	exit /b 2
)

call "%VCVARS%" >nul
if errorlevel 1 (
	echo [syntax-probe] failed to initialise MSVC environment
	exit /b 2
)

rem Note: "%~dp0." (with a dot) avoids the trailing-backslash-before-quote problem.
rem third_party\include is on the path so <pluginGl.h> resolves to the REAL SDK header
rem (a stub for it would defeat the purpose).
cl /nologo /Zs /std:c++17 /utf-8 /EHsc /W3 /I "%~dp0." /I "%~dp0..\..\third_party\include" /I "%~dp0..\..\src" %*
exit /b %ERRORLEVEL%
