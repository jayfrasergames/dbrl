#ifndef SOUND_H
#define SOUND_H

#include "prelude.h"
#include "containers.hpp"
#include "assets.h"

#define SOUND_MAX_SIMULTANEOUS_SOUNDS 32

#ifdef JFG_DSOUND_H

#ifndef INCLUDE_AUDIO_API
#define INCLUDE_AUDIO_API
#endif

struct Sound_Player_DSound
{
	IDirectSound       *dsound;
	IDirectSoundBuffer *sound_buffers[NUM_SOUNDS];
	Max_Length_Array<IDirectSoundBuffer*, SOUND_MAX_SIMULTANEOUS_SOUNDS> playing_sounds;
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
u8 sound_player_dsound_init(Sound_Player* player, Assets_Header* assets_header, IDirectSound* dsound);
u8 sound_player_load_sounds(Sound_Player* player);
void sound_player_dsound_free(Sound_Player* player);
void sound_player_dsound_play(Sound_Player* player);
#endif

#ifndef JFG_HEADER_ONLY

u8 sound_player_dsound_init(Sound_Player* player, Assets_Header* assets_header, IDirectSound* dsound)
{
	player->dsound.dsound = dsound;
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		player->dsound.sound_buffers[i] = NULL;
	}
	return 1;
}

u8 sound_player_load_sounds(Sound_Player* player, Assets_Header* assets_header)
{
	HRESULT hr;
	IDirectSound *dsound = player->dsound.dsound;

	u32 num_sounds_inited = 0;
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		Sound_Header *sh = &assets_header->sound_headers[i];
		Asset_Entry  *ae = &assets_header->asset_entries[sound_id_to_asset_id((Sound_ID)i)];
		IDirectSoundBuffer *sound_buffer = NULL;
		{
			WAVEFORMATEX waveformat = {};
			waveformat.wFormatTag = WAVE_FORMAT_PCM;
			waveformat.nChannels = sh->num_channels;
			waveformat.nSamplesPerSec = sh->sample_rate;
			waveformat.wBitsPerSample = sh->sample_width * 8;
			waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
			waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
			waveformat.cbSize = 0;

			DSBUFFERDESC desc = {};
			desc.dwSize = sizeof(desc);
			desc.dwFlags = DSBCAPS_GLOBALFOCUS;
			desc.dwBufferBytes = ae->size;
			desc.lpwfxFormat = &waveformat;

			hr = dsound->CreateSoundBuffer(&desc, &sound_buffer, NULL);
		}
		if (FAILED(hr)) {
			num_sounds_inited = i;
			goto error_init_sound;
		} else {
			void *data_1, *data_2;
			DWORD len_1, len_2;
			hr = sound_buffer->Lock(0, ae->size,
			                        &data_1, &len_1,
			                        &data_2, &len_2,
			                        DSBLOCK_ENTIREBUFFER);
			ASSERT(SUCCEEDED(hr));
			memcpy(data_1, ae->data, len_1);
			if (data_2) {
				memcpy(data_2, (u8*)ae->data + len_1, len_2);
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
	auto& playing_sounds = player->dsound.playing_sounds;
	for (u32 i = 0; i < playing_sounds.len; ++i) {
		playing_sounds[i]->Release();
	}
	for (u32 i = 0; i < NUM_SOUNDS; ++i) {
		player->dsound.sound_buffers[i]->Release();
	}
}

void sound_player_dsound_play(Sound_Player* player)
{
	auto& playing_sounds = player->dsound.playing_sounds;
	for (u32 i = 0; i < playing_sounds.len; ) {
		DWORD status;
		HRESULT hr = playing_sounds[i]->GetStatus(&status);
		ASSERT(hr == DS_OK);
		if (!(status & DSBSTATUS_PLAYING)) {
			playing_sounds.remove(i);
			continue;
		}
		++i;
	}
	IDirectSound *dsound = player->dsound.dsound;
	u32 num_sounds_to_play = player->sounds_to_play.len;
	for (u32 i = 0; i < num_sounds_to_play; ++i) {
		IDirectSoundBuffer *buffer = player->dsound.sound_buffers[player->sounds_to_play[i]];
		if (buffer) {
			IDirectSoundBuffer *play_buffer = NULL;
			dsound->DuplicateSoundBuffer(buffer, &play_buffer);
			play_buffer->Play(0, 0, 0);
			player->dsound.playing_sounds.append(play_buffer);
		}
		player->dsound.sound_buffers[player->sounds_to_play[i]]->Play(0, 0, 0);
	}
	if (player->new_ambience_queued) {
		IDirectSoundBuffer *buffer = player->dsound.sound_buffers[player->ambience];
		if (buffer) {
			player->new_ambience_queued = 0;
			buffer->Play(0, 0, DSBPLAY_LOOPING);
		}
	}
}

#endif

#endif
