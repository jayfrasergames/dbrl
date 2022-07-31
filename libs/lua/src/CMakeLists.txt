project(lua)

set(source
	lapi.c
	lauxlib.c
	lbaselib.c
	lcode.c
	lcorolib.c
	lctype.c
	ldblib.c
	ldebug.c
	ldo.c
	ldump.c
	lfunc.c
	lgc.c
	linit.c
	liolib.c
	llex.c
	lmathlib.c
	lmem.c
	loadlib.c
	lobject.c
	lopcodes.c
	loslib.c
	lparser.c
	lstate.c
	lstring.c
	lstrlib.c
	ltable.c
	ltablib.c
	ltm.c
	lundump.c
	lutf8lib.c
	lvm.c
	lzio.c
)

set(programs
	lua.c
	luac.c
)

set(headers
	lapi.h
	lauxlib.h
	lcode.h
	lctype.h
	ldebug.h
	ldo.h
	lfunc.h
	lgc.h
	ljumptab.h
	llex.h
	llimits.h
	lmem.h
	lobject.h
	lopcodes.h
	lopnames.h
	lparser.h
	lprefix.h
	lstate.h
	lstring.h
	ltable.h
	ltm.h
	luaconf.h
	lua.h
	lualib.h
	lundump.h
	lvm.h
	lzio.h
)

source_group("inc" FILES ${headers})
source_group("src" FILES ${source} ${programs})

if(MSVC)
	# TODO -- MultiThreadedDebug for debug mode
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_library(liblua STATIC ${source} ${headers})
add_executable(lua lua.c ${headers})
add_executable(luac luac.c ${headers})

target_link_libraries(lua liblua)
target_link_libraries(luac liblua)

foreach(target liblua lua luac)
	set_target_properties(${target} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
endforeach()
