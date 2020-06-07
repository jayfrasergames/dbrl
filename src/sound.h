#ifndef SOUND_H
#define SOUND_H

#include "jfg/prelude.h"
#include "jfg/containers.hpp"

// SOUND(name, header, data)
#define SOUNDS \
	SOUND(DEAL_CARD)

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

#ifdef INCLUDE_AUDIO_API
	union {
	#ifdef JFG_DSOUND_H
		Sound_Player_DSound dsound;
	#endif
	};
#endif

	void reset()              { sounds_to_play.reset();       }
	void play(Sound_ID sound) { sounds_to_play.append(sound); }
};

#ifdef JFG_DSOUND_H
u8 sound_player_dsound_init(Sound_Player* player, IDirectSound* dsound);
void sound_player_dsound_free(Sound_Player* player);
void sound_player_dsound_play(Sound_Player* player);
#endif

#ifndef JFG_HEADER_ONLY
#include "gen/sound_deal_card.data.h"

u8 sound_player_dsound_init(Sound_Player* player, IDirectSound* dsound)
{
	HRESULT hr;
	IDirectSoundBuffer *deal_card_sound = NULL;
	{
		WAVEFORMATEX waveformat = {};
		waveformat.wFormatTag = WAVE_FORMAT_PCM;
		waveformat.nChannels = SOUND_DEAL_CARD_HEADER.num_channels;
		waveformat.nSamplesPerSec = SOUND_DEAL_CARD_HEADER.sample_rate;
		waveformat.wBitsPerSample = SOUND_DEAL_CARD_HEADER.sample_width * 8;
		waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
		waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
		waveformat.cbSize = 0;

		DSBUFFERDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_GLOBALFOCUS;
		desc.dwBufferBytes = sizeof(SOUND_DEAL_CARD_DATA);
		desc.lpwfxFormat = &waveformat;

		hr = dsound->CreateSoundBuffer(&desc, &deal_card_sound, NULL);
	}
	if (FAILED(hr)) {
		goto error_init_sound;
	} else {
		void *data_1, *data_2;
		DWORD len_1, len_2;
		hr = deal_card_sound->Lock(0, sizeof(SOUND_DEAL_CARD_DATA),
		                           &data_1, &len_1,
		                           &data_2, &len_2,
		                           DSBLOCK_ENTIREBUFFER);
		ASSERT(SUCCEEDED(hr));
		memcpy(data_1, SOUND_DEAL_CARD_DATA, len_1);
		if (data_2) {
			memcpy(data_2, SOUND_DEAL_CARD_DATA + len_1, len_2);
		}
		hr = deal_card_sound->Unlock(data_1, len_1, data_2, len_2);
		ASSERT(SUCCEEDED(hr));
	}
	player->dsound.sound_buffers[SOUND_DEAL_CARD] = deal_card_sound;

	return 1;

error_init_sound:
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
}

#endif

#endif
