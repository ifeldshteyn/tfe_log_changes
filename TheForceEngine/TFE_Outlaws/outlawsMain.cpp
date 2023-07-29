#include "outlawsMain.h"
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>

using namespace TFE_Jedi;

namespace TFE_Outlaws
{
	static u8* s_framebuffer = nullptr;
	static RSector* s_sector = nullptr;
	u8* s_levelColorMap = nullptr;
	u8* s_lightSourceRamp = nullptr;

	namespace
	{
		void createRenderDisplay()
		{
			TFE_Jedi::renderer_init(RVERSION_2);	// Updated version of the renderer.

			TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
			DisplayInfo info;
			TFE_RenderBackend::getDisplayInfo(&info);

			s32 adjustedWidth = graphics->gameResolution.x;
			if (graphics->widescreen && (graphics->gameResolution.z == 200 || graphics->gameResolution.z == 400))
			{
				adjustedWidth = (graphics->gameResolution.z * info.width / info.height) * 12 / 10;
			}
			else if (graphics->widescreen)
			{
				adjustedWidth = graphics->gameResolution.z * info.width / info.height;
			}
			// Make sure the adjustedWidth is divisible by 4.
			adjustedWidth = 4 * ((adjustedWidth + 3) >> 2);

			vfb_setResolution(adjustedWidth, graphics->gameResolution.z);
			s_framebuffer = vfb_getCpuBuffer();

			// Outlaws does not support the fixed-point renderer.
			TFE_Jedi::setSubRenderer(TSR_HIGH_RESOLUTION);

			TFE_Jedi::renderer_setType(RendererType(graphics->rendererIndex));
			TFE_Jedi::render_setResolution();
			TFE_Jedi::renderer_setLimits();
		}
	}

	bool Outlaws::runGame(s32 argCount, const char* argv[], Stream* stream)
	{
		createRenderDisplay();

		// For now, just start with a level select...
		return true;
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

	// Notes:
	// fixed-point software renderer does not work with Outlaws.
	// float-sectors are used directly.
		
	void Outlaws::loopGame()
	{
		// STUB
		drawWorld(s_framebuffer, s_sector, s_levelColorMap, s_lightSourceRamp);
	}
}