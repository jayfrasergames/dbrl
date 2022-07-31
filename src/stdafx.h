#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
	#include "lua.h"
	#include "lualib.h"
	#include "lauxlib.h"
#ifdef __cplusplus
}
#endif

#include "png.h"

#ifdef WIN32
#include <windows.h>
#include <dxgi1_2.h>
#include "jfg_d3d11.h"
#endif

#include "prelude.h"
#include "format.h"
#include "jfg_math.h"
#include "containers.hpp"