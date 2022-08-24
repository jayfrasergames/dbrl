#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "assets.h"

#include "jfg_dsound.h"

#define SOUND_MAX_SIMULTANEOUS_SOUNDS 32

struct Sound_Player_DSound
{
	IDirectSound       *dsound;
	IDirectSoundBuffer *sound_buffers[NUM_SOUNDS];
	Max_Length_Array<IDirectSoundBuffer*, SOUND_MAX_SIMULTANEOUS_SOUNDS> playing_sounds;
};

#define SOUND_MAX_SOUNDS 64
struct Sound_Player
{
	Max_Length_Array<Sound_ID, SOUND_MAX_SOUNDS> sounds_to_play;
	u8 new_ambience_queued;
	Sound_ID ambience;

	Sound_Player_DSound dsound;

	void reset()                      { sounds_to_play.reset();       }
	void play(Sound_ID sound)         { sounds_to_play.append(sound); }
	void set_ambience(Sound_ID sound) { new_ambience_queued = 1; ambience = sound; }
};

u8 sound_player_dsound_init(Sound_Player* player, Assets_Header* assets_header, IDirectSound* dsound);
u8 sound_player_load_sounds(Sound_Player* player, Assets_Header* assets_header);
void sound_player_dsound_free(Sound_Player* player);
void sound_player_dsound_play(Sound_Player* player);