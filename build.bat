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
	if "%1" == "assets" (
		CALL :build_assets
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

pushd ..\src

fxc /Fh gen\tile_render_dxbc_tile_vertex_shader.data.h ^
	/E vs_tile /Vn TILE_RENDER_TILE_VS /T vs_5_0 tile_render.hlsl
fxc /Fh gen\tile_render_dxbc_tile_pixel_shader.data.h  ^
	/E ps_tile /Vn TILE_RENDER_TILE_PS /T ps_5_0 tile_render.hlsl

fxc /Fh gen\sprite_render_dxbc_sprite_vertex_shader.data.h ^
	/E vs_sprite /Vn SPRITE_RENDER_SPRITE_VS /T vs_5_0 sprite_render.hlsl
fxc /Fh gen\sprite_render_dxbc_sprite_pixel_shader.data.h  ^
	/E ps_sprite /Vn SPRITE_RENDER_SPRITE_PS /T ps_5_0 sprite_render.hlsl

popd

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

:build_assets
echo build_assets

python ..\scripts\texture_convert ^
	-i ../assets/tiles.png ^
	-o ../src/gen/background_tiles.data.h ^
	-t Tile_Render_Texture ^
	-n Background_Tiles ^
	--tile-width 24 ^
	--tile-height 24
python ..\scripts\texture_convert ^
	-i ../assets/creatures.png ^
	-o ../src/gen/creature_sprites.data.h ^
	-t Sprite_Render_Texture ^
	-n Creature_Sprites ^
	--tile-width 24 ^
	--tile-height 24

EXIT /B 0
