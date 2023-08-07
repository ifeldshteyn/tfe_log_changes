#pragma once
#include <string>
#include <vector>
#include <TFE_System/system.h>
using std::string;

namespace TFE_A11Y {
	enum A11yStatus
	{
		CC_NOT_LOADED, CC_LOADED, CC_ERROR
	};

	enum CaptionEnv 
	{
		CC_GAMEPLAY, CC_CUTSCENE
	};

	enum CaptionType
	{
		CC_VOICE, CC_EFFECT
	};

	struct Caption 
	{
		string text;
		s64 microsecondsRemaining;
		CaptionType type;
		CaptionEnv env;
	};

	void init();
	std::vector<string> getCaptionFileNames();
	void loadCaptions(const string fileName);
	string getCurrentCaptionFileName();
	A11yStatus getStatus();
	void clearActiveCaptions();
	void drawCaptions();
	void drawExampleCaptions();
	void focusCaptions();
	void addCaption(Caption caption);
	void onSoundPlay(char* name, CaptionEnv env);

	//True if captions or subtitles are enabled for cutscenes
	bool cutsceneCaptionsEnabled();
	//True if captions or subtitles are enabled for gameplay
	bool gameplayCaptionsEnabled();
}