set(precompiled_headers stdafx.h)

set(headers
	assets.h
	card_render_gpu_data_types.h
	card_render.h
	codepage_437.h
	constants.edit.h
	constants.h
	cpu_gpu_data_types.h
	dbrl.h
	debug_draw_world_gpu_data_types.h
	debug_draw_world.h
	debug_line_draw.h
	debug_line_dxbc_pixel_shader.data.h
	debug_line_dxbc_vertex_shader.data.h
	debug_line_gpu_data_types.h
	field_of_vision_render_gpu_data_types.h
	field_of_vision_render.h
	imgui_dxbc_text_pixel_shader.data.h
	imgui_dxbc_text_vertex_shader.data.h
	imgui_gpu_data_types.h
	imgui.h
	input.h
	jfg_d3d11.h
	jfg_dsound.h
	jfg_math.h
	log.h
	mem.h
	particles_gpu_data_types.h
	particles.h
	physics.h
	pixel_art_upsampler_gpu_data_types.h
	pixel_art_upsampler.h
	platform_functions.h
	prelude.h
	random.h
	sound.h
	sounds.list.h
	sprite_sheet_gpu_data_types.h
	sprite_sheet.h
	stdafx.h
	thread.h
	types.h
)

set(object_sources
	dbrl.cpp
)

set(program_sources
	assets_file.cpp
	main_win32.cpp
)

set(shaders
	card_render.hlsl
	debug_draw_world.hlsl
	debug_line_draw.hlsl
	field_of_vision_render.hlsl
	imgui.hlsl
	particles.hlsl
	pass_through_output.hlsl
	pixel_art_upsampler.hlsl
	sprite_sheet.hlsl
)

source_group("inc" FILES ${precompiled_headers} ${headers})
source_group("src" FILES ${program_sources} ${object_sources})
source_group("shaders" FILES ${shaders})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")

if(MSVC)
	add_definitions(-DWIN32)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# XXX
# add_executable(dbrl main_win32.cpp dbrl.cpp ${precompiled_headers} ${headers} ${shaders})
add_executable(dbrl WIN32 main_win32.cpp ${precompiled_headers} ${headers} ${shaders})
target_precompile_headers(dbrl PUBLIC ${precompiled_headers})
set_target_properties(dbrl PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})

add_executable(build_assets_file assets_file.cpp ${precompiled_headers} ${headers})
target_precompile_headers(build_assets_file PUBLIC ${precompile_headers})
set_target_properties(build_assets_file PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})