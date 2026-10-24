@echo off
rem Runs a headless test script in ArmorPaint on Windows, see readme.md
rem Usage: base\tests\paint_headless\run.bat [run.c|make_fixture.c]
rem Exit code: 0 pass, 1 fail, 2 setup error

setlocal
set "HERE=%~dp0"
set "OUT=%HERE%..\..\..\paint\build\out"
set "APP=%OUT%\ArmorPaint.exe"
set "SCRIPT=%~1"
if "%SCRIPT%"=="" set "SCRIPT=run.c"
set "LOG=%OUT%\..\smoke\%~n1.log"
if "%~1"=="" set "LOG=%OUT%\..\smoke\run.log"

if not exist "%APP%" (
	echo ArmorPaint.exe not found at %APP%, build it first: cd paint ^&^& ..\base\make --compile
	exit /b 2
)
if not exist "%HERE%%SCRIPT%" (
	echo Script not found: %HERE%%SCRIPT%
	exit /b 2
)
if not exist "%OUT%\..\smoke" mkdir "%OUT%\..\smoke"

rem Start from the default config, a user config can be in sculpt mode or use another layer resolution
if exist "%OUT%\data\config.json" move /y "%OUT%\data\config.json" "%OUT%\data\config.json.smoke_backup" > nul

pushd "%OUT%"
"%APP%" --background --script ..\..\..\base\tests\paint_headless\%SCRIPT% > "%LOG%" 2>&1
popd

if exist "%OUT%\data\config.json" del "%OUT%\data\config.json"
if exist "%OUT%\data\config.json.smoke_backup" move /y "%OUT%\data\config.json.smoke_backup" "%OUT%\data\config.json" > nul

type "%LOG%"
findstr /c:"SMOKE PASS" /c:"FIXTURE DONE" "%LOG%" > nul
if errorlevel 1 (
	echo Failed, log: %LOG%
	exit /b 1
)
exit /b 0
