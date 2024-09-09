#include <TFE_FileSystem/physfswrapper.h>
#include "outlawsMain.h"

namespace TFE_Outlaws
{
	bool validateSourceData(const char *path)
	{
		static const char * const testfiles[] = {
			"outlaws.lab", "olgeo.lab", "oltex.lab", "olobj.lab",
			"olsfx.lab", "oltaunt.lab", "olweap.lab"
		};
		bool ok = false;
		TFEMount m = vpMountReal(path, VPATH_TMP);
		if (!m)
			goto out;

		ok = true;
		for (auto i : testfiles)
		{
			if (!vpFileExists(m, i, false))
			{
				ok = false;
				break;
			}
		}
		vpUnmount(m);
out:
		return ok;
	}

	bool Outlaws::runGame(s32 argCount, const char* argv[], vpFile* stream)
	{
		// STUB
		return false;
	}

	void Outlaws::pauseGame(bool pause)
	{
		// STUB
	}

	void Outlaws::pauseSound(bool pause)
	{
		// STUB
	}

	void Outlaws::restartMusic()
	{
		// STUB
	}

	void Outlaws::exitGame()
	{
		// STUB
	}

	// Outlaws does not use loopGame()
}