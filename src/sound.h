#ifndef SOUND_H
#define SOUND_H

#include "jfg/prelude.h"
#include "jfg/containers.hpp"

// SOUND(name, header, data)
#define SOUNDS \
	SOUND(DEAL_CARD) \
	SOUND(FIREBALL_EXPLOSION) \
	/* SOUND(CAVE_AMBIENCE) */

enum Sound_ID
{
#define SOUND(name) SOUND_##name,
	SOUNDS
#undef SOUND

	NUM_SOUNDS
};

#ifdef JFG_DSOUND_H

#ifndef INCLUDE_AUDIO_API
#define INCLUDE_AUDIO_API
#endif

struct Sound_Player_DSound
{
	IDirectSoundBuffer *sound_buffers[NUM_SOUNDS];
};
#endif

#define SOUND_MAX_SOUNDS 64
struct Sound_Player
{
	Max_Length_Array<Sound_ID, SOUND_MAX_SOUNDS> sounds_to_play;
	u8 new_ambience_queued;
	Sound_ID ambience;

#ifdef INCLUDE_AUDIO_API
	union {
	#ifdef JFG_DSOUND_H
		Sound_Player_DSound dsound;
	#endif
	};
#endif

	void reset()                      { sounds_to_play.reset();       }
	void play(Sound_ID sound)         { sounds_to_play.append(sound); }
	void set_ambience(Sound_ID sound) { new_ambience_queued = 1; ambience = sound; }
};

#ifdef JFG_DSOUND_H
u8 sound_player_dsound_init(Sound_Player* player, IDirectSound* dsound);
void sound_player_dsound_free(Sound_Player* player);
void sound_player_dsound_play(Sound_Player* player);
#endif

#ifndef JFG_HEADER_ONLY
#include "gen/sound_deal_card.data.h"
#include "gen/sound_fireball_explosion.data.h"
// #include "gen/sound_cave_ambience.data.h"

u8 sound_player_dsound_init(Sound_Player* player, IDirectSound* dsound)
{
	HRESULT hr;

	struct Sound_Data
	{
		u8   num_channels;
		u8   sample_width;
		u32  sample_rate;
		u32  num_samples;
		u8  *data;
		u32  data_size;
	};

	Sound_Data sound_data[] = {
#define SOUND(name) \
		{ \
			SOUND_##name##_HEADER.num_channels, \
			SOUND_##name##_HEADER.sample_width, \
			SOUND_##name##_HEADER.sample_rate, \
			SOUND_##name##_HEADER.num_samples, \
			SOUND_##name##_HEADER.data, \
			sizeof(SOUND_##name##_DATA), \
		},
	SOUNDS
#undef SOUND
	};

	u32 num_sounds_inited = 0;
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		Sound_Data *sd = &sound_data[i];
		IDirectSoundBuffer *sound_buffer = NULL;
		{
			WAVEFORMATEX waveformat = {};
			waveformat.wFormatTag = WAVE_FORMAT_PCM;
			waveformat.nChannels = sd->num_channels;
			waveformat.nSamplesPerSec = sd->sample_rate;
			waveformat.wBitsPerSample = sd->sample_width * 8;
			waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
			waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
			waveformat.cbSize = 0;

			DSBUFFERDESC desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DSBCAPS_GLOBALFOCUS;
			desc.dwBufferBytes = sd->data_size;
			desc.lpwfxFormat = &waveformat;

			hr = dsound->CreateSoundBuffer(&desc, &sound_buffer, NULL);
		}
		if (FAILED(hr)) {
			num_sounds_inited = i;
			goto error_init_sound;
		} else {
			void *data_1, *data_2;
			DWORD len_1, len_2;
			hr = sound_buffer->Lock(0, sizeof(SOUND_DEAL_CARD_DATA),
						   &data_1, &len_1,
						   &data_2, &len_2,
						   DSBLOCK_ENTIREBUFFER);
			ASSERT(SUCCEEDED(hr));
			memcpy(data_1, sd->data, len_1);
			if (data_2) {
				memcpy(data_2, sd->data + len_1, len_2);
			}
			hr = sound_buffer->Unlock(data_1, len_1, data_2, len_2);
			ASSERT(SUCCEEDED(hr));
		}
		player->dsound.sound_buffers[i] = sound_buffer;
	}

	return 1;

error_init_sound:
	for (u32 i = 0; i < num_sounds_inited; ++i) {
		player->dsound.sound_buffers[i]->Release();
	}
	return 0;
}

void sound_player_dsound_free(Sound_Player* player)
{
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		player->dsound.sound_buffers[i]->Release();
	}
}

void sound_player_dsound_play(Sound_Player* player)
{
	u32 num_sounds_to_play = player->sounds_to_play.len;
	for (u32 i = 0; i < num_sounds_to_play; ++i) {
		player->dsound.sound_buffers[player->sounds_to_play[i]]->Play(0, 0, 0);
	}
	if (player->new_ambience_queued) {
		player->new_ambience_queued = 0;
		player->dsound.sound_buffers[player->ambience]->Play(0, 0, DSBPLAY_LOOPING);
	}
}

#endif

#endif
