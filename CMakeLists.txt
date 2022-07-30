cmake_minimum_required(VERSION 3.23)
set(CMAKE_GENERATOR_PLATFORM x64)

project(dbrl)

add_subdirectory(libs/zlib)

# XXX -- these directories aren't set correctly. Should be set relative to the target build
# directory for zlib
option(PNG_BUILD_ZLIB "" ON)
set(ZLIB_INCLUDE_DIR
    ${CMAKE_HOME_DIRECTORY}/libs/zlib
    ${CMAKE_HOME_DIRECTORY}/libs/zlib
    ${CMAKE_HOME_DIRECTORY}/build/libs/zlib)
set(ZLIB_LIBRARY ${CMAKE_HOME_DIRECTORY}/build/libs/zlib/$<CONFIG>/zlib$<$<CONFIG:Debug>:d>.lib)
add_subdirectory(libs/libpng)

set(CMAKE_DEBUG_POSTFIX _d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
foreach(config ${CMAKE_CONFIGURATION_TYPES})
	string(TOUPPER ${config} config)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_HOME_DIRECTORY}/bin)
endforeach()

add_subdirectory(src)