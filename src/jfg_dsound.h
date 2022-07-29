#ifndef JFG_DSOUND_H
#define JFG_DSOUND_H

#include "prelude.h"

// XXX - maybe it would be nice to not rely on this header at all?
// the way it is all done is kind of crazy and unnecessary for what
// we actually want - plus we need to hack around the need to link
// against the dsound dll

#define DirectSoundEnumerateA _DirectSoundEnumerateA
#define DirectSoundCreate     _DirectSoundCreate

#include <dsound.h>

#undef DirectSoundEnumerateA
#undef DirectSoundCreate

#define DSOUND_FUNCTIONS \
	DSOUND_FUNCTION(HRESULT, DirectSoundEnumerateA, LPDSENUMCALLBACKA lpDSEnumCallback, LPVOID context) \
	DSOUND_FUNCTION(HRESULT, DirectSoundCreate, LPGUID lpGuid, LPDIRECTSOUND* ppDS, void* unused)

#define DSOUND_FUNCTION(return_type, name, ...) extern return_type (WINAPI *name)(__VA_ARGS__);
DSOUND_FUNCTIONS
#undef DSOUND_FUNCTION

u8 dsound_try_load();

#ifndef JFG_HEADER_ONLY

#define DSOUND_FUNCTION(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
DSOUND_FUNCTIONS
#undef DSOUND_FUNCTION

u8 dsound_try_load()
{
	HMODULE dsound_library = LoadLibraryA("dsound.dll");
	if (!dsound_library) {
		return 0;
	}
#define DSOUND_FUNCTION(return_type, name, ...) \
		name = (return_type (WINAPI *)(__VA_ARGS__))GetProcAddress(dsound_library, #name);
	DSOUND_FUNCTIONS
#undef DSOUND_FUNCTION
	return 1;
}

#endif

#endif
