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
	user32.lib ^
	/W3 ^
	/I ..\src ^
	/Fe: %project_name%_d.exe ^
	/Fd: %project_name%_d.pdb ^
	/Zi /Od /MTd ^
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
	/W3 ^
	/I ..\src ^
	/Fe: %project_name%_d.dll ^
	/Fd: pdb\%project_name%_lib_d_%time_str%_2.pdb ^
	/Zi /LDd /MTd /D DEBUG /D LIBRARY ^
	/link /dll /incremental:no /pdb:pdb\%project_name%_lib_d_%time_str%_1.pdb

echo written_library > written_library

EXIT /B 0

:build_shaders
echo build_shaders

pushd ..\src

fxc /Fh gen\sprite_sheet_dxbc_vertex_shader.data.h ^
	/E vs_sprite /Vn SPRITE_SHEET_RENDER_DXBC_VS /T vs_5_0 sprite_sheet.hlsl
fxc /Fh gen\sprite_sheet_dxbc_pixel_shader.data.h ^
	/E ps_sprite /Vn SPRITE_SHEET_RENDER_DXBC_PS /T ps_5_0 sprite_sheet.hlsl
fxc /Fh gen\sprite_sheet_dxbc_clear_sprite_id_compute_shader.data.h ^
	/E cs_clear_sprite_id /Vn SPRITE_SHEET_CLEAR_SPRITE_ID_CS /T cs_5_0 sprite_sheet.hlsl
fxc /Fh gen\sprite_sheet_dxbc_highlight_sprite_compute_shader.data.h ^
	/E cs_highlight_sprite /Vn SPRITE_SHEET_HIGHLIGHT_CS /T cs_5_0 sprite_sheet.hlsl
fxc /Fh gen\sprite_sheet_dxbc_font_vertex_shader.data.h ^
	/E vs_font /Vn SPRITE_SHEET_FONT_DXBC_VS /T vs_5_0 sprite_sheet.hlsl
fxc /Fh gen\sprite_sheet_dxbc_font_pixel_shader.data.h ^
	/E ps_font /Vn SPRITE_SHEET_FONT_DXBC_PS /T ps_5_0 sprite_sheet.hlsl

fxc /Fh gen\pixel_art_upsampler_dxbc_compute_shader.data.h ^
	/E cs_pixel_art_upsampler /Vn PIXEL_ART_UPSAMPLER_CS /T cs_5_0 pixel_art_upsampler.hlsl

fxc /Fh gen\pass_through_dxbc_vertex_shader.data.h ^
	/E vs_pass_through /Vn PASS_THROUGH_VS /T vs_5_0 pass_through_output.hlsl
fxc /Fh gen\pass_through_dxbc_pixel_shader.data.h ^
	/E ps_pass_through /Vn PASS_THROUGH_PS /T ps_5_0 pass_through_output.hlsl

fxc /Fh gen\card_render_dxbc_vertex_shader.data.h ^
	/E vs_card /Vn CARD_RENDER_DXBC_VS /T vs_5_0 card_render.hlsl
fxc /Fh gen\card_render_dxbc_pixel_shader.data.h ^
	/E ps_card /Vn CARD_RENDER_DXBC_PS /T ps_5_0 card_render.hlsl

fxc /Fh gen\debug_draw_world_dxbc_triangle_vertex_shader.data.h ^
	/E vs_triangle /Vn DDW_TRIANGLE_DXBC_VS /T vs_5_0 debug_draw_world.hlsl
fxc /Fh gen\debug_draw_world_dxbc_triangle_pixel_shader.data.h ^
	/E ps_triangle /Vn DDW_TRIANGLE_DXBC_PS /T ps_5_0 debug_draw_world.hlsl
fxc /Fh gen\debug_draw_world_dxbc_line_vertex_shader.data.h ^
	/E vs_line /Vn DDW_LINE_DXBC_VS /T vs_5_0 debug_draw_world.hlsl
fxc /Fh gen\debug_draw_world_dxbc_line_pixel_shader.data.h ^
	/E ps_line /Vn DDW_LINE_DXBC_PS /T ps_5_0 debug_draw_world.hlsl

fxc /Fh gen\particles_dxbc_compute_shader.data.h ^
	/E cs_particles /Vn PARTICLES_DXBC_CS /T cs_5_0 particles.hlsl

fxc /Fh gen\field_of_vision_edge_vs_dxbc.data.h ^
	/E vs_edge /Vn FOV_EDGE_DXBC_VS /T vs_5_0 field_of_vision_render.hlsl
fxc /Fh gen\field_of_vision_edge_ps_dxbc.data.h ^
	/E ps_edge /Vn FOV_EDGE_DXBC_PS /T ps_5_0 field_of_vision_render.hlsl
fxc /Fh gen\field_of_vision_fill_vs_dxbc.data.h ^
	/E vs_fill /Vn FOV_FILL_DXBC_VS /T vs_5_0 field_of_vision_render.hlsl
fxc /Fh gen\field_of_vision_fill_ps_dxbc.data.h ^
	/E ps_fill /Vn FOV_FILL_DXBC_PS /T ps_5_0 field_of_vision_render.hlsl
fxc /Fh gen\field_of_vision_blend_cs_dxbc.data.h ^
	/E cs_shadow_blend /Vn FOV_BLEND_DXBC_CS /T cs_5_0 field_of_vision_render.hlsl
fxc /Fh gen\field_of_vision_composite_cs_dxbc.data.h ^
	/E cs_composite /Vn FOV_COMPOSITE_DXBC_CS /T cs_5_0 field_of_vision_render.hlsl

popd

EXIT /B 0

:build_release
echo build_debug_release

cl ..\src\main_win32.cpp ^
	user32.lib ^
	/W3 ^
	/I ..\src ^
	/Fe: %project_name%.exe ^
	/Ot /MT ^
	/D WIN32 ^
	/link /incremental:no /subsystem:windows

EXIT /B 0

:build_assets
echo build_assets

python ..\scripts\make_sprite_sheet.py ^
	-i ../assets/creatures.png ^
	-o ../src/gen/sprite_sheet_creatures.data.h ^
	-n Creatures ^
	--tile-width 24 ^
	--tile-height 24
python ..\scripts\make_sprite_sheet.py ^
	-i ../assets/tiles.png ^
	-o ../src/gen/sprite_sheet_tiles.data.h ^
	-n Tiles ^
	--tile-width 24 ^
	--tile-height 24
python ..\scripts\make_sprite_sheet.py ^
	-i ../assets/water_edges.png ^
	-o ../src/gen/sprite_sheet_water_edges.data.h ^
	-n Water_Edges ^
	--tile-width 24 ^
	--tile-height 24
python ..\scripts\make_sprite_sheet.py ^
	-i ../assets/effects24.png ^
	-o ../src/gen/sprite_sheet_effects_24.data.h ^
	-n Effects_24 ^
	--tile-width 24 ^
	--tile-height 24
python ..\scripts\make_sprite_sheet.py ^
	-i ../assets/effects32.png ^
	-o ../src/gen/sprite_sheet_effects_32.data.h ^
	-n Effects_32 ^
	--tile-width 32 ^
	--tile-height 32
python ..\assets\make_metadata_header.py ^
	-o ../src/gen/appearance.data.h
python ..\assets\build_cards.py ^
	-o ../src/gen/cards.data.h
python ..\assets\make_boxy_bold.py ^
	-i ../assets/boxy_font.png ^
	-o ../src/gen/boxy_bold.data.h

cl ..\src\assets_file.cpp ^
	user32.lib ^
	/W3 ^
	/I ..\src ^
	/Fe: build_assets_file.exe ^
	/Fd: build_assets_file.pdb ^
	/Zi /Od /MTd ^
	/D WIN32 /D DEBUG ^
	/link /incremental:no /subsystem:console
build_assets_file.exe

EXIT /B 0
