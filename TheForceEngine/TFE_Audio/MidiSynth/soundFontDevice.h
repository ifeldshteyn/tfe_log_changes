#pragma once
#include <TFE_System/types.h>
#include <TFE_Audio/midiDevice.h>
#include <TFE_Audio/midi.h>
#include <TFE_FileSystem/fileutil.h>

struct tsf;

namespace TFE_Audio
{
	class SoundFontDevice : public MidiDevice
	{
	public:
		SoundFontDevice() : m_soundFont(nullptr), m_outputId(-1) {}
		~SoundFontDevice() override;

		MidiDeviceType getType() override { return MIDI_TYPE_SF2; }

		void exit() override;
		bool hasGlobalVolumeCtrl() override { return true; }
		const char* getName() override;

		bool render(f32* buffer, u32 sampleCount) override;
		bool canRender() override;

		void message(u8 type, u8 arg1, u8 arg2) override;
		void message(const u8* msg, u32 len) override;
		void noteAllOff() override;
		void setVolume(f32 volume) override;

		u32  getOutputCount() override;
		void getOutputName(s32 index, char* buffer, u32 maxLength) override;
		bool selectOutput(s32 index) override;
		s32  getActiveOutput(void) override;

	private:
		bool beginStream(FilePath2& soundFont, s32 sampleRate);

		tsf* m_soundFont;
		s32  m_outputId;
		FileList2 m_outputs;
	};
};