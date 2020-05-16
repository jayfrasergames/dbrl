@echo off

pushd %~dp0

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
	/Fe: dbrl_d.exe ^
	/Fd: dbrl_d.pdb ^
	/Zi /Od /MDd ^
	/D WIN32 /D DEBUG ^
	/link /incremental:no /subsystem:windows

EXIT /B 0

:build_debug_library
echo build_debug_library
EXIT /B 0

:build_shaders
echo build_shaders

fxc /Fh ..\src\gen\vs_text.data.h /E vs_text /Vn VS_TEXT /T vs_5_0 ..\src\shaders.hlsl
fxc /Fh ..\src\gen\ps_text.data.h /E ps_text /Vn PS_TEXT /T ps_5_0 ..\src\shaders.hlsl

EXIT /B 0

:build_release
echo build_debug_release

cl ..\src\main_sdl.cpp ^
	..\libs\build\SDL2\Release\SDL2main.lib ^
	..\libs\build\SDL2\Release\SDL2.lib ^
	..\libs\build\box2d\src\Release\box2d.lib ^
	user32.lib kernel32.lib shell32.lib winmm.lib advapi32.lib ole32.lib gdi32.lib version.lib ^
	setupapi.lib oleaut32.lib imm32.lib ^
	/I ..\src /I ..\libs\SDL2\include /I ..\libs\box2d\include ^
	/Fe: super_over_it.exe ^
	/Ot /MD ^
	/D WIN32 ^
	/link /incremental:no /subsystem:windows

EXIT /B 0
