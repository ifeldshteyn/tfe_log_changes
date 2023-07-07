#include <cstring>

#include <SDL.h>

#include "menu.h"
#include "delt.h"
#include "uiDraw.h"
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
#include <TFE_DarkForces/Landru/ldraw.h>
#include <TFE_Archive/archive.h>
#include <TFE_Archive/lfdArchive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Ui/ui.h>

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Internal State
	///////////////////////////////////////////
	static LfdArchive s_archive;

	Vec2i s_cursorPosAccum;
	Vec2i s_cursorPos;
	s32 s_buttonPressed = -1;
	JBool s_buttonHover = JFALSE;
		
	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void menu_init()
	{
	}

	void menu_destroy()
	{
		menu_resetState();
	}

	void menu_resetState()
	{
		delt_resetState();
	}
	
	void menu_handleMousePosition()
	{
		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		LRect bounds;
		lcanvas_getBounds(&bounds);
		s32 width  = bounds.right  - bounds.left;
		s32 height = bounds.bottom - bounds.top;

		LRect displayRect = TFE_RenderBackend::calcDisplayRect();

		s32 mx, my;
		TFE_Input::getMousePos(&mx, &my);

		s_cursorPosAccum = {
			interpolate(mx, displayRect.left, displayRect.right, bounds.left, bounds.right),
			interpolate(my, displayRect.top, displayRect.bottom, bounds.top, bounds.bottom),
		};
		s_cursorPos = s_cursorPosAccum;
	}

	void menu_resetCursor()
	{
		// Reset the cursor.
		u32 width, height;
		vfb_getResolution(&width, &height);

		DisplayInfo displayInfo;
		TFE_RenderBackend::getDisplayInfo(&displayInfo);

		s_cursorPosAccum = { (s32)displayInfo.width / 2, (s32)displayInfo.height / 2 };
		s_cursorPos.x = clamp(s_cursorPosAccum.x * (s32)height / (s32)displayInfo.height, 0, (s32)width - 3);
		s_cursorPos.z = clamp(s_cursorPosAccum.z * (s32)height / (s32)displayInfo.height, 0, (s32)height - 3);

		LRect bounds;
		lcanvas_getBounds(&bounds);

		LRect displayRect = TFE_RenderBackend::calcDisplayRect();
		SDL_WarpMouseInWindow(TFE_Ui::getSDLWindow(),
			interpolate(s_cursorPos.x, bounds.left, bounds.right, displayRect.left, displayRect.right),
			interpolate(s_cursorPos.z, bounds.top, bounds.bottom, displayRect.top, displayRect.bottom)
		);
	}

	u8* menu_startupDisplay()
	{
		TFE_Input::setMouseCursorMode(MCURSORMODE_ABSOLUTE);
		vfb_setResolution(320, 200);
		return vfb_getCpuBuffer();
	}

	void menu_blitCursor(s32 x, s32 y, u8* framebuffer)
	{
		LRect bounds;
		lcanvas_getBounds(&bounds);
		if (x < bounds.left || x > bounds.right || y < bounds.top || y > bounds.bottom)
		{
			return;
		}

		blitDeltaFrame(&s_cursor, x, y, framebuffer);
	}

	JBool menu_openResourceArchive(const char* name)
	{
		FilePath lfdPath;
		if (!TFE_Paths::getFilePath(name, &lfdPath))
		{
			return JFALSE;
		}
		// Load the mission briefing text.
		if (!s_archive.open(lfdPath.path))
		{
			return JFALSE;
		}
		TFE_Paths::addLocalArchive(&s_archive);
		return JTRUE;
	}

	void menu_closeResourceArchive()
	{
		s_archive.close();
		TFE_Paths::removeLastArchive();
	}

	void menu_blitToScreen(u8* framebuffer/*=nullptr*/, JBool transparent/*=JFALSE*/, JBool swap/*=JTRUE*/)
	{
		u32 outWidth, outHeight;
		vfb_getResolution(&outWidth, &outHeight);

		// If there is no override, the default behavior is to use the Landru bitmap.
		if (!framebuffer)
		{
			framebuffer = ldraw_getBitmap();
		}

		if (outWidth == 320 && outHeight == 200)
		{
			if (transparent)
			{
				TFE_Jedi::ScreenImage canvas =
				{
					320,
					200,
					framebuffer,
					transparent,
					JFALSE,
				};
				ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
				blitTextureToScreen(&canvas, (DrawRect*)uiRect, 0, 0, vfb_getCpuBuffer());
			}
			else
			{
				// This is a straight copy - best for performance since the GPU can do the upscale.
				memcpy(vfb_getCpuBuffer(), framebuffer, 320 * 200);
			}
		}
		else
		{
			// This version requires a software upscale, this is handy when some parts need to be higher resolution or
			// to avoid switching virtual framebuffers during play.
			TFE_Jedi::ScreenImage canvas =
			{
				320,
				200,
				framebuffer,
				transparent,
				JFALSE,
			};
			ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
			fixed16_16 xScale = vfb_getXScale();
			fixed16_16 yScale = vfb_getYScale();

			s32 virtualWidth = floor16(mul16(intToFixed16(320), xScale));
			s32 offset = max(0, ((uiRect->right - uiRect->left + 1) - virtualWidth) / 2);
			
			if (!transparent)
			{
				memset(vfb_getCpuBuffer(), 0, outWidth * outHeight);
			}
			blitTextureToScreenScaled(&canvas, (DrawRect*)uiRect, offset, 0, xScale, yScale, vfb_getCpuBuffer());
		}
		if (swap) { vfb_swap(); }
	}

	void menu_blitCursorScaled(s16 x, s16 y, u8* buffer)
	{
		ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
		fixed16_16 xScale = vfb_getXScale();
		fixed16_16 yScale = vfb_getYScale();

		s32 virtualWidth = floor16(mul16(intToFixed16(320), xScale));
		s32 offset = max(0, ((uiRect->right - uiRect->left + 1) - virtualWidth) / 2);

		x = floor16(mul16(intToFixed16(x), xScale)) + offset;
		y = floor16(mul16(intToFixed16(y), yScale));

		blitDeltaFrameScaled(&s_cursor, x, y, xScale, yScale, buffer);
	}
}