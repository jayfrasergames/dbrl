@echo off

pushd %~dp0

set project_name=dbrl
if not exist build mkdir build

pushd build

:Loop
if "%1"=="" GOTO Continue
	if "%1" == "debug_library" (
		CALL :build_debug_library
	)
	if "%1" == "debug_runner" (
		CALL :build_debug_runner
	)
	if "%1" == "release" (
		CALL :build_release
	)
	if "%1" == "shaders" (
		CALL :build_shaders
	)
SHIFT
GOTO Loop
:Continue

popd
popd
EXIT /B 0

:build_debug_runner
echo build_debug_runner

cl ..\src\main_win32.cpp ^
	d3d11.lib user32.lib ^
	/I ..\src ^
	/Fe: %project_name%_d.exe ^
	/Fd: %project_name%_d.pdb ^
	/Zi /Od /MDd ^
	/D WIN32 /D DEBUG ^
	/link /incremental:no /subsystem:windows

EXIT /B 0

:build_debug_library
echo build_debug_library

if not exist pdb mkdir pdb

set time_str=%time%
set hour_str=%time_str:~0,2%
if %hour_str% lss 10 (set hour_str=0%hour_str:~1,1%)
set min_str=%time_str:~3,2%
set sec_str=%time_str:~6,2%

set date_str=%date%
set year_str=%date_str:~6,4%
set mon_str=%date_str:~3,2%
set day_str=%date_str:~0,2%

set time_str=%year_str%-%mon_str%-%day_str%-%hour_str%-%min_str%-%sec_str%

cl ..\src\dbrl.cpp ^
	/I ..\src ^
	/Fe: %project_name%_d.dll ^
	/Fd: pdb\%project_name%_lib_d_%time_str%_2.pdb ^
	/Zi /LDd /MDd /D DEBUG /D LIBRARY ^
	/link /dll /incremental:no /pdb:pdb\%project_name%_lib_d_%time_str%_1.pdb

echo written_library > written_library

EXIT /B 0

:build_shaders
echo build_shaders

EXIT /B 0

:build_release
echo build_debug_release

cl ..\src\main_win32.cpp ^
	d3d11.lib user32.lib ^
	/I ..\src ^
	/Fe: %project_name%.exe ^
	/Ot /MD ^
	/D WIN32 ^
	/link /incremental:no /subsystem:windows

EXIT /B 0
