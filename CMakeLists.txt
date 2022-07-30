cmake_minimum_required(VERSION 3.23)
set(CMAKE_GENERATOR_PLATFORM x64)

project(dbrl)

set(CMAKE_DEBUG_POSTFIX _d)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
foreach(config ${CMAKE_CONFIGURATION_TYPES})
	string(TOUPPER ${config} config)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_HOME_DIRECTORY}/bin)
endforeach()

add_subdirectory(src)