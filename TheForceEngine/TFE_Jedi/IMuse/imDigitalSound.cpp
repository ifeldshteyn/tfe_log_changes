#include "imuse.h"
#include "imDigitalSound.h"
#include "imSoundFader.h"
#include "imTrigger.h"
#include "imList.h"
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_System/system.h>
#include <TFE_Audio/midi.h>
#include <TFE_Audio/audioSystem.h>
#include <assert.h>

namespace TFE_Jedi
{
	////////////////////////////////////////////////////
	// Structures
	////////////////////////////////////////////////////
	struct ImWaveData;

	struct ImWaveSound
	{
		ImWaveSound* prev;
		ImWaveSound* next;
		ImWaveData* data;
		ImSoundId soundId;

		s32 marker;
		s32 group;
		s32 priority;
		s32 baseVolume;

		s32 volume;
		s32 pan;
		s32 detune;
		s32 transpose;

		s32 detuneTrans;
		s32 mailbox;
	};

	struct ImWaveData
	{
		ImWaveSound* sound;
		s32 offset;
		s32 chunkSize;
		s32 baseOffset;
		s32 chunkIndex;
		s32 u20;
	};

	struct AudioFrame
	{
		u8* data;
		s32 size;
	};

	/////////////////////////////////////////////////////
	// Tables
	/////////////////////////////////////////////////////
	static u8 s_audioPanVolumeTable[] =
	{
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
		0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
		0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
		0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06,
		0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07,
		0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08,
		0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B,
		0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C,
		0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D,
		0x00, 0x01, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0C, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E,
		0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0C, 0x0D, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F,
		0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x0F, 0x10, 0x10, 0x10
	};
	static u8 s_audio8bitTo16bit[16 * 128];	// TODO: Get values.
	
	/////////////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////////////
	static ImWaveSound* s_imWaveSounds = nullptr;
	static ImWaveSound  s_imWaveSound[16];
	static ImWaveData   s_imWaveData[16];
	static u8  s_imWaveChunkData[48];
	static s32 s_imWaveMixCount = 8;
	static s32 s_imWaveNanosecsPerSample;
	static iMuseInitData* s_imDigitalData;

	// In DOS these are 8-bit outputs since that is what the driver is accepting.
	// For TFE, floating-point audio output is used, so these convert to floating-point.
	static f32  s_audioNormalizationMem[2052];
	// Normalizes the sum of all audio playback (16-bit) to a [-1,1) floating point value.
	// The mapping can be addressed with negative values (i.e. s_audioNormalization[-16]), which is why
	// it is built this way.
	static f32* s_audioNormalization = &s_audioNormalizationMem[1028];

	static f32* s_audioDriverOut;
	static s16 s_audioOut[512];
	static s32 s_audioOutSize;
	static u8* s_audioData;

	extern atomic_s32 s_sndPlayerLock;
	extern atomic_s32 s_digitalPause;

	extern void ImMidiPlayerLock();
	extern void ImMidiPlayerUnlock();
	extern s32 ImWrapValue(s32 value, s32 a, s32 b);
	extern s32 ImGetGroupVolume(s32 group);
	extern u8* ImInternalGetSoundData(ImSoundId soundId);

	ImWaveData* ImGetWaveData(s32 index);
	s32 ImComputeAudioNormalization(iMuseInitData* initData);
	s32 ImSetWaveParamInternal(ImSoundId soundId, s32 param, s32 value);
	s32 ImGetWaveParamIntern(ImSoundId soundId, s32 param);
	s32 ImStartDigitalSoundIntern(ImSoundId soundId, s32 priority, s32 chunkIndex);
	s32 audioPlaySoundFrame(ImWaveSound* sound);
	s32 audioWriteToDriver();
	
	/////////////////////////////////////////////////////////// 
	// API
	/////////////////////////////////////////////////////////// 
	s32 ImInitializeDigitalAudio(iMuseInitData* initData)
	{
		IM_DBG_MSG("TRACKS module...");
		if (initData->waveMixCount <= 0 || initData->waveMixCount > 16)
		{
			IM_LOG_ERR("TR: waveMixCount NULL or too big, defaulting to 4...");
			initData->waveMixCount = 4;
		}
		s_imWaveMixCount = initData->waveMixCount;
		s_digitalPause = 0;
		s_imWaveSounds = nullptr;

		if (initData->waveSpeed == IM_WAVE_11kHz) // <- this is the path taken by Dark Forces DOS
		{
			// Nanoseconds per second / wave speed in Hz
			// 1,000,000,000 / 11,000
			s_imWaveNanosecsPerSample = 90909;
		}
		else // IM_WAVE_22kHz
		{
			// Nanoseconds per second / wave speed in Hz
			// 1,000,000,000 / 22,000
			s_imWaveNanosecsPerSample = 45454;
		}

		ImWaveSound* sound = s_imWaveSound;
		for (s32 i = 0; i < s_imWaveMixCount; i++, sound++)
		{
			sound->prev = nullptr;
			sound->next = nullptr;
			ImWaveData* data = ImGetWaveData(i);
			sound->data = data;
			data->sound = sound;
			sound->soundId = IM_NULL_SOUNDID;
		}

		TFE_Audio::setAudioThreadCallback(ImUpdateWave);

		s_sndPlayerLock = 0;
		return ImComputeAudioNormalization(initData);
	}

	void ImTerminateDigitalAudio()
	{
		TFE_Audio::setAudioThreadCallback();
	}

	s32 ImSetWaveParam(ImSoundId soundId, s32 param, s32 value)
	{
		ImMidiPlayerLock();
		s32 res = ImSetWaveParamInternal(soundId, param, value);
		ImMidiPlayerUnlock();
		return res;
	}

	s32 ImGetWaveParam(ImSoundId soundId, s32 param)
	{
		ImMidiPlayerLock();
		s32 res = ImGetWaveParamIntern(soundId, param);
		ImMidiPlayerUnlock();
		return res;
	}

	s32 ImStartDigitalSound(ImSoundId soundId, s32 priority)
	{
		ImMidiPlayerLock();
		s32 res = ImStartDigitalSoundIntern(soundId, priority, 0);
		ImMidiPlayerUnlock();
		return res;
	}
		
	void ImUpdateWave(f32* buffer, u32 bufferSize)
	{
		// Prepare buffers.
		s_audioDriverOut = buffer;
		s_audioOutSize = bufferSize;
		memset(s_audioOut, 0, bufferSize * sizeof(s16));

		// Write sounds to s_audioOut.
		ImWaveSound* sound = s_imWaveSounds;
		while (sound)
		{
			audioPlaySoundFrame(sound);
			sound = sound->next;
		}

		// Convert s_audioOut to "driver" buffer.
		audioWriteToDriver();
	}

	////////////////////////////////////
	// Internal
	////////////////////////////////////
	ImWaveData* ImGetWaveData(s32 index)
	{
		return &s_imWaveData[index];
	}

	s32 ImComputeAudioNormalization(iMuseInitData* initData)
	{
		s_imDigitalData = initData;
		s32 waveMixCount = initData->waveMixCount;
		s32 volumeMidPoint = 128;
		s32 tableSize = waveMixCount << 7;
		for (s32 i = 0; i < tableSize; i++)
		{
			// Results for count ~= 8: (i=0) 0.0, 1.5, 2.5, 3.4, 4.4, 5.2, 6.3, 7.2, ... 127.1 (i = 1023).
			s32 volumeOffset = ((waveMixCount * 127 * i) << 8) / (waveMixCount * 127 + (waveMixCount - 1)*i) + 128;
			volumeOffset >>= 8;

			// These values are 8-bit in DOS, but converted to floating point for TFE.
			s_audioNormalization[i]    = f32(volumeMidPoint + volumeOffset)     / 128.0f - 1.0f;
			s_audioNormalization[-i-1] = f32(volumeMidPoint - volumeOffset - 1) / 128.0f - 1.0f;
		}
		return imSuccess;
	}

	s32 ImSetWaveParamInternal(ImSoundId soundId, s32 param, s32 value)
	{
		ImWaveSound* sound = s_imWaveSounds;
		while (sound)
		{
			if (sound->soundId == soundId)
			{
				if (param == soundGroup)
				{
					if (value >= 16)
					{
						return imArgErr;
					}
					sound->volume = ((sound->baseVolume + 1) * ImGetGroupVolume(value)) >> 7;
					return imSuccess;
				}
				else if (param == soundPriority)
				{
					if (value > 127)
					{
						return imArgErr;
					}
					sound->priority = value;
					return imSuccess;
				}
				else if (param == soundVol)
				{
					if (value > 127)
					{
						return imArgErr;
					}
					sound->baseVolume = value;
					sound->volume = ((sound->baseVolume + 1) * ImGetGroupVolume(sound->group)) >> 7;
					return imSuccess;
				}
				else if (param == soundPan)
				{
					if (value > 127)
					{
						return imArgErr;
					}
					sound->pan = value;
					return imSuccess;
				}
				else if (param == soundDetune)
				{
					if (value < -9216 || value > 9216)
					{
						return imArgErr;
					}
					sound->detune = value;
					sound->detuneTrans = sound->detune + (sound->transpose << 8);
					return imSuccess;
				}
				else if (param == soundTranspose)
				{
					if (value < -12 || value > 12)
					{
						return imArgErr;
					}
					sound->transpose = value ? ImWrapValue(sound->transpose + value, -12, 12) : 0;
					sound->detuneTrans = sound->detune + (sound->transpose << 8);
					return imSuccess;
				}
				else if (param == soundMailbox)
				{
					sound->mailbox = value;
					return imSuccess;
				}
				// Invalid Parameter
				IM_LOG_ERR("ERR: TrSetParam() couldn't set param %lu...", param);
				return imArgErr;
			}
			sound = sound->next;
		}
		return imInvalidSound;
	}

	s32 ImGetWaveParamIntern(ImSoundId soundId, s32 param)
	{
		s32 soundCount = 0;
		ImWaveSound* sound = s_imWaveSounds;
		while (sound)
		{
			if (sound->soundId == soundId)
			{
				if (param == soundType)
				{
					return imFail;
				}
				else if (param == soundPlayCount)
				{
					soundCount++;
				}
				else if (param == soundMarker)
				{
					return sound->marker;
				}
				else if (param == soundGroup)
				{
					return sound->group;
				}
				else if (param == soundPriority)
				{
					return sound->priority;
				}
				else if (param == soundVol)
				{
					return sound->baseVolume;
				}
				else if (param == soundPan)
				{
					return sound->pan;
				}
				else if (param == soundDetune)
				{
					return sound->detune;
				}
				else if (param == soundTranspose)
				{
					return sound->transpose;
				}
				else if (param == soundMailbox)
				{
					return sound->mailbox;
				}
				else if (param == waveStreamFlag)
				{
					return sound->data ? 1 : 0;
				}
				else
				{
					return imArgErr;
				}
			}
			sound = sound->next;
		}
		return (param == soundPlayCount) ? soundCount : imInvalidSound;
	}

	ImWaveSound* ImAllocWavePlayer(s32 priority)
	{
		ImWaveSound* sound = s_imWaveSound;
		ImWaveSound* newSound = nullptr;
		for (s32 i = 0; i < s_imWaveMixCount; i++, sound++)
		{
			if (!sound->soundId)
			{
				return sound;
			}
		}

		IM_LOG_WRN("ERR: no spare tracks...");
		// TODO
		return nullptr;
	}

	u8* ImGetChunkSoundData(s32 chunkIndex, s32 rangeMin, s32 rangeMax)
	{
		IM_LOG_ERR("Digital Sound chunk index should be zero in Dark Forces, but is %d.", chunkIndex);
		assert(0);
		return nullptr;
	}

	s32 ImSeekToNextChunk(ImWaveData* data)
	{
		while (1)
		{
			u8* chunkData = s_imWaveChunkData;
			u8* sndData = nullptr;

			if (data->chunkIndex)
			{
				sndData = ImGetChunkSoundData(data->chunkIndex, 0, 48);
				if (!sndData)
				{
					sndData = ImGetChunkSoundData(data->chunkIndex, 0, 1);
				}
				if (!sndData)
				{
					return imNotFound;
				}
			}
			else  // chunkIndex == 0
			{
				ImWaveSound* sound = data->sound;
				sndData = ImInternalGetSoundData(sound->soundId);
				if (!sndData)
				{
					if (sound->mailbox == 0)
					{
						sound->mailbox = 8;
					}
					IM_LOG_ERR("null sound addr in SeekToNextChunk()...");
					return imFail;
				}
			}

			memcpy(chunkData, sndData + data->offset, 48);
			u8 id = *chunkData;
			chunkData++;

			if (id == 0)
			{
				return imFail;
			}
			else if (id == 1)	// found the next useful chunk.
			{
				s32 chunkSize = (chunkData[0] | (chunkData[1] << 8) | (chunkData[2] << 16)) - 2;
				chunkData += 5;

				data->chunkSize = chunkSize;
				if (chunkSize > 220000)
				{
					ImWaveSound* sound = data->sound;
					if (sound->mailbox == 0)
					{
						sound->mailbox = 9;
					}
				}

				data->offset += 6;
				if (data->chunkIndex)
				{
					IM_LOG_ERR("data->chunkIndex should be 0 in Dark Forces, it is: %d.", data->chunkIndex);
					assert(0);
				}
				return imSuccess;
			}
			else if (id == 4)
			{
				chunkData += 3;
				ImSetSoundTrigger((ImSoundId)data->sound, chunkData);
				data->offset += 6;
			}
			else if (id == 6)
			{
				data->baseOffset = data->offset;
				data->offset += 6;
				if (data->chunkIndex != 0)
				{
					IM_LOG_ERR("data->chunkIndex should be 0 in Dark Forces, it is: %d.", data->chunkIndex);
					assert(0);
				}
			}
			else if (id == 7)
			{
				data->offset = data->baseOffset;
				if (data->chunkIndex != 0)
				{
					IM_LOG_ERR("data->chunkIndex should be 0 in Dark Forces, it is: %d.", data->chunkIndex);
					assert(0);
				}
			}
			else if (id == 'C')
			{
				if (chunkData[0] != 'r' || chunkData[1] != 'e' || chunkData[2] != 'a')
				{
					IM_LOG_ERR("ERR: Illegal chunk in sound %lu...", data->sound->soundId);
					return imFail;
				}
				data->offset += 26;
				if (data->chunkIndex)
				{
					IM_LOG_ERR("data->chunkIndex should be 0 in Dark Forces, it is: %d.", data->chunkIndex);
					assert(0);
				}
			}
			else
			{
				IM_LOG_ERR("ERR: Illegal chunk in sound %lu...", data->sound->soundId);
				return imFail;
			}
		}
		return imSuccess;
	}

	s32 ImWaveSetupSoundData(ImWaveSound* sound, s32 chunkIndex)
	{
		ImWaveData* data = sound->data;
		data->offset = 0;
		data->chunkSize = 0;
		data->baseOffset = 0;
		data->u20 = 0;

		if (chunkIndex)
		{
			IM_LOG_ERR("data->chunkIndex should be 0 in Dark Forces, it is: %d.", chunkIndex);
			assert(0);
		}

		data->chunkIndex = 0;
		return ImSeekToNextChunk(data);
	}

	s32 ImStartDigitalSoundIntern(ImSoundId soundId, s32 priority, s32 chunkIndex)
	{
		priority = clamp(priority, 0, 127);
		ImWaveSound* sound = ImAllocWavePlayer(priority);
		if (!sound)
		{
			return imFail;
		}

		sound->soundId = soundId;
		sound->marker = 0;
		sound->group = 0;
		sound->priority = priority;
		sound->volume = 128;
		sound->baseVolume = ImGetGroupVolume(0);
		sound->pan = 64;
		sound->detune = 0;
		sound->transpose = 0;
		sound->detuneTrans = 0;
		sound->mailbox = 0;
		if (ImWaveSetupSoundData(sound, chunkIndex) != imSuccess)
		{
			IM_LOG_ERR("Failed to setup wave player data - soundId: 0x%x, priority: %d", soundId, priority);
			return imFail;
		}

		ImMidiPlayerLock();
		IM_LIST_ADD(s_imWaveSounds, sound);
		ImMidiPlayerUnlock();

		return imSuccess;
	}

	void ImFreeWaveSound(ImWaveSound* sound)
	{
		IM_LIST_REM(s_imWaveSounds, sound);
		ImClearSoundFaders(sound->soundId, -1);
		ImClearTrigger(sound->soundId, -1, -1);
		sound->soundId = IM_NULL_SOUNDID;
	}
		
	void digitalAudioOutput_Stereo(s16* audioOut, u8* sndData, u8* leftVol, u8* rightVol, s32 size)
	{
		for (; size > 0; size--, sndData++, audioOut+=2)
		{
			const u8 sample = *sndData;
			audioOut[0] += leftVol[sample];
			audioOut[1] += rightVol[sample];
		}
	}

	void audioProcessFrame(AudioFrame* audioFrame, s32 outOffset, s32 vol, s32 pan)
	{
		s32 vTop = vol >> 3;
		if (vol)
		{
			vTop++;
		}
		if (vTop >= 17)
		{
			vTop = 1;
		}

		s32 panTop = (pan >> 3) - 8;
		if (pan > 64)
		{
			panTop++;
		}

		s32 left     = s_audioPanVolumeTable[8 - panTop + vTop * 17];
		s32 right    = s_audioPanVolumeTable[8 + panTop + vTop * 17];
		u8* leftVol  = &s_audio8bitTo16bit[left << 8];
		u8* rightVol = &s_audio8bitTo16bit[right << 8];

		digitalAudioOutput_Stereo(&s_audioOut[outOffset * 2], audioFrame->data, leftVol, rightVol, audioFrame->size);
	}

	s32 audioPlaySoundFrame(ImWaveSound* sound)
	{
		ImWaveData* data = sound->data;
		s32 bufferSize = s_audioOutSize >> 1;
		s32 offset = 0;
		s32 res = imSuccess;
		while (bufferSize > 0)
		{
			res = imSuccess;
			if (!data->chunkSize)
			{
				res = ImSeekToNextChunk(data);
				if (res != imSuccess)
				{
					if (res == imFail)  // Sound has finished playing.
					{
						ImFreeWaveSound(sound);
					}
					break;
				}
			}
			if (!bufferSize)
			{
				break;
			}

			s32 readSize = (bufferSize <= data->chunkSize) ? bufferSize : data->chunkSize;
			s_audioData = ImInternalGetSoundData(sound->soundId) + data->offset;
			audioProcessFrame((AudioFrame*)s_audioData, offset, sound->baseVolume, sound->pan);

			offset += readSize;
			bufferSize -= readSize;
			data->offset += readSize;
			data->chunkSize -= readSize;
		}
		return res;
	}

	s32 audioWriteToDriver()
	{
		if (!s_audioOut)
		{
			return imInvalidSound;
		}

		s32 bufferSize = s_audioOutSize;
		s16* audioOut  = s_audioOut;
		f32* driverOut = s_audioDriverOut;
		for (; bufferSize > 0; bufferSize--, audioOut++, driverOut++)
		{
			*driverOut = s_audioNormalization[*audioOut];
		}
		return imSuccess;
	}

}  // namespace TFE_Jedi
