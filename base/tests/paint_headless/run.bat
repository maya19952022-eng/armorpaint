@echo off
rem Runs headless test scripts in ArmorPaint on Windows, see readme.md
rem Usage: base\tests\paint_headless\run.bat [script.c]
rem   no script: run.c and every phase test (p1_*.c, p2_*.c, ...)
rem Exit code: 0 pass, 1 fail, 2 setup error

setlocal enabledelayedexpansion
set "HERE=%~dp0"
set "OUT=%HERE%..\..\..\paint\build\out"
set "APP=%OUT%\ArmorPaint.exe"

if not exist "%APP%" (
	echo ArmorPaint.exe not found at %APP%, build it first: cd paint ^&^& ..\base\make --compile
	exit /b 2
)

set "SCRIPTS=%~1"
if "%SCRIPTS%"=="" (
	set "SCRIPTS=run.c"
	for %%f in ("%HERE%p?_*.c") do set "SCRIPTS=!SCRIPTS! %%~nxf"
)
for %%s in (%SCRIPTS%) do (
	if not exist "%HERE%%%s" (
		echo Script not found: %HERE%%%s
		exit /b 2
	)
)
if not exist "%OUT%\..\smoke" mkdir "%OUT%\..\smoke"

rem Start from the default config, a user config can be in sculpt mode or use another layer resolution
if exist "%OUT%\data\config.json" move /y "%OUT%\data\config.json" "%OUT%\data\config.json.smoke_backup" > nul

set "STATUS=0"
pushd "%OUT%"
for %%s in (%SCRIPTS%) do (
	set "LOG=%OUT%\..\smoke\%%~ns.log"
	if exist "data\config.json" del "data\config.json"
	"%APP%" --background --script ..\..\..\base\tests\paint_headless\%%s > "!LOG!" 2>&1
	type "!LOG!"
	findstr /c:"SMOKE PASS" /c:"FIXTURE DONE" "!LOG!" > nul
	if errorlevel 1 (
		echo [%%s] failed, log: !LOG!
		set "STATUS=1"
	) else (
		echo [%%s] passed
	)
)
popd

if exist "%OUT%\data\config.json" del "%OUT%\data\config.json"
if exist "%OUT%\data\config.json.smoke_backup" move /y "%OUT%\data\config.json.smoke_backup" "%OUT%\data\config.json" > nul

exit /b %STATUS%
