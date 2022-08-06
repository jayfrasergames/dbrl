set(precompiled_headers stdafx.h)

set(headers
	assets.h
	card_render.h
	codepage_437.h
	constants.edit.h
	constants.h
	dbrl.h
	debug_draw_world.h
	debug_line_draw.h
	debug_line_dxbc_pixel_shader.data.h
	debug_line_dxbc_vertex_shader.data.h
	field_of_vision_render.h
	imgui_dxbc_text_pixel_shader.data.h
	imgui_dxbc_text_vertex_shader.data.h
	imgui.h
	input.h
	jfg_d3d11.h
	jfg_dsound.h
	jfg_math.h
	log.h
	mem.h
	particles.h
	physics.h
	pixel_art_upsampler.h
	platform_functions.h
	prelude.h
	random.h
	sound.h
	sounds.list.h
	sprite_sheet.h
	stdafx.h
	thread.h
	types.h
)

set(object_sources
	console.cpp
	assets.cpp
	card_render.cpp
	debug_draw_world.cpp
	debug_line_draw.cpp
	sprite_sheet.cpp
	field_of_vision_render.cpp
	input.cpp
	imgui.cpp
	dbrl.cpp
	format.cpp
	particles.cpp
	pixel_art_upsampler.cpp
	draw_dx11.cpp
	jfg_d3d11.cpp
	random.cpp
	texture.cpp
	render.cpp
)

set(program_sources
	assets_file.cpp
	main_win32.cpp
	test_draw_dx11.cpp
)

set(shader_dir ../assets/shaders)
set(lua_dir ../libs/lua/src)
set(libpng_dir ../libs/libpng ../build/libs/libpng)

set(shaders
	${shader_dir}/card_render_gpu_data_types.h
	${shader_dir}/cpu_gpu_data_types.h
	${shader_dir}/debug_draw_world_gpu_data_types.h
	${shader_dir}/debug_line_gpu_data_types.h
	${shader_dir}/field_of_vision_render_gpu_data_types.h
	${shader_dir}/imgui_gpu_data_types.h
	${shader_dir}/particles_gpu_data_types.h
	${shader_dir}/pixel_art_upsampler_gpu_data_types.h
	${shader_dir}/sprite_sheet_gpu_data_types.h
	${shader_dir}/card_render.hlsl
	${shader_dir}/debug_draw_world.hlsl
	${shader_dir}/debug_line_draw.hlsl
	${shader_dir}/field_of_vision_render.hlsl
	${shader_dir}/imgui.hlsl
	${shader_dir}/particles.hlsl
	${shader_dir}/pass_through_output.hlsl
	${shader_dir}/pixel_art_upsampler.hlsl
	${shader_dir}/sprite_sheet.hlsl
)

source_group("inc" FILES ${precompiled_headers} ${headers})
source_group("src" FILES ${program_sources} ${object_sources})
source_group("shaders" FILES ${shaders})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "Text")

if(MSVC)
	add_definitions(-DWIN32)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# TODO -- list explicitly which objects exes other than DBRL depend on -- rebuilding
# everything each time any object changes is dumb :-)
add_executable(dbrl WIN32 main_win32.cpp ${object_sources} ${precompiled_headers} ${headers} ${shaders})
target_precompile_headers(dbrl PUBLIC ${precompiled_headers})

add_executable(build_assets_file
               assets_file.cpp
               assets.cpp
               assets.h)

# add_executable(test_draw_dx11 test_draw_dx11.cpp draw_dx11.cpp draw.h)

foreach(executable dbrl build_assets_file)
	set_target_properties(${executable} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
	target_include_directories(${executable} PUBLIC ${shader_dir} ${lua_dir} ${libpng_dir})
endforeach()

target_link_libraries(dbrl liblua png_static)