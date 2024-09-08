#include "lactorDelt.h"
#include "lsystem.h"
#include "cutscene_film.h"
#include "lcanvas.h"
#include "lview.h"
#include "ldraw.h"
#include "ltimer.h"
#include <TFE_FileSystem/physfswrapper.h>
#include <TFE_Game/igame.h>
#include <assert.h>

namespace TFE_DarkForces
{
	static JBool s_lactorDeltInit = JFALSE;

	///////////////////////////////////////////
	// TODO: Draw API.
	void dirtyRect(LRect* rect)
	{
	}
	//////////////////////////////////////////////

	void  lactorDelt_initActor(LActor* actor, u8* data, LRect* frame, s16 xOffset, s16 yOffset, s16 zPlane);
	void  lactorDelt_update(LActor* actor);

	void lactorDelt_init()
	{
		lactor_createType(CF_TYPE_DELTA_ACTOR, lactorDelt_getFrame, nullptr);
		s_lactorDeltInit = JTRUE;
	}

	void lactorDelt_destroy()
	{
		if (s_lactorDeltInit)
		{
			lactor_destroyType(CF_TYPE_DELTA_ACTOR);
		}
		s_lactorDeltInit = JFALSE;
	}

	void lactorDelt_getFrame(LActor* actor, LRect* rect)
	{
		s16* data = (s16*)actor->data;
		if (data)
		{
			lrect_set(rect, data[0], data[1], data[2], data[3]);
			JBool hFlip, vFlip;
			lactor_getFlip(actor, &hFlip, &vFlip);
			lrect_flip(rect, &actor->bounds, hFlip, JFALSE);
		}
		else
		{
			lrect_set(rect, 0, 0, 0, 0);
		}
	}

	LActor* lactorDelt_alloc(u8* delta, LRect* frame, s16 xOffset, s16 yOffset, s16 zPlane)
	{
		LActor* actor = lactor_alloc(0);
		if (!actor) { return nullptr; }

		lactorDelt_initActor(actor, delta, frame, xOffset, yOffset, zPlane);
		lactor_setName(actor, CF_TYPE_DELTA_ACTOR, "");
		lactor_keepData(actor);

		return actor;
	}

	LActor* lactorDelt_load(const char* name, LRect* rect, s16 x, s16 y, s16 zPlane)
	{
		char deltName[32];
		sprintf(deltName, "%s.DELT", name);

		vpFile file(VPATH_GAME, deltName, false);
		if (!file)
			return nullptr;
		unsigned int deltSize = file.size();

		u8* data = (u8*)landru_alloc(deltSize);
		if (!data)
			return nullptr;
		file.read(data, deltSize);
		file.close();

		LActor* actor = lactor_alloc(0);
		if (!actor)
		{
			landru_free(data);
			return nullptr;
		}

		lactorDelt_initActor(actor, data, rect, x, y, zPlane);
		lactor_setName(actor, CF_TYPE_DELTA_ACTOR, name);

		return actor;
	}

	void lactorDelt_initActor(LActor* actor, u8* data, LRect* frame, s16 xOffset, s16 yOffset, s16 zPlane)
	{
		lrect_set(&actor->frame, frame->left, frame->top, frame->right, frame->bottom);
		actor->x = xOffset;
		actor->y = yOffset;
		actor->zplane = zPlane;

		lactor_discardData(actor);
		actor->drawFunc   = lactorDelt_draw;
		actor->updateFunc = lactorDelt_update;
		actor->data = data;

		LRect rect;
		lactor_addActor(actor);
		lactorDelt_getFrame(actor, &rect);
		actor->w = rect.right - rect.left;
		actor->h = rect.bottom - rect.top;
		actor->bounds = rect;
	}

	void lactorDelt_update(LActor* actor)
	{
		lactor_move(actor);
		lactor_moveFrame(actor);
	}

	JBool lactorDelt_draw(LActor* actor, LRect* rect, LRect* clipRect, s16 x, s16 y, JBool refresh)
	{
		if (!refresh)
		{
			return JFALSE;
		}
		u8* data = actor->data;

		JBool retValue = JFALSE;
		if (data)
		{
			if (actor->flags & LAFLAG_HFLIP)
			{
				retValue = lactorDelt_drawFlippedClipped(actor, data, x, y, lactor_isDirty(actor));
			}
			else
			{
				retValue = lactorDelt_drawClipped(data, x, y, lactor_isDirty(actor));
			}
		}

		return retValue;
	}
		
	JBool lactorDelt_drawClipped(u8* data, s16 x, s16 y, JBool dirty)
	{
		s16* data16 = (s16*)data;
		s16 sx = data16[0] + x;
		s16 sy = data16[1] + y;

		s16 ex = data16[2] + x;
		s16 ey = data16[3] + y;
		data16 += 4;

		LRect drect;
		lrect_set(&drect, sx, sy, ex + 1, ey + 1);
		LRect clipRect = drect;

		JBool retValue = JFALSE;
		if (lcanvas_clipRectToCanvas(&clipRect))
		{
			if (lrect_equal(&clipRect, &drect))
			{
				deltaImage(data16, x, y);
			}
			else
			{
				deltaClip(data16, x, y);
			}

			if (dirty)
			{
				dirtyRect(&clipRect);
			}
			retValue = JTRUE;
		}

		return retValue;
	}

	JBool lactorDelt_drawFlippedClipped(LActor* actor, u8* data, s16 x, s16 y, JBool dirty)
	{
		s16* data16 = (s16*)data;
		JBool hFlip = (actor->flags & LAFLAG_HFLIP) ? JTRUE : JFALSE;
		s16 w = actor->bounds.right + actor->bounds.left - 1;
		s16 h = actor->bounds.bottom + actor->bounds.top - 1;

		s16 sx, sy;
		s16 ex, ey;
		if (hFlip)
		{
			ex = w - data16[0] + x;
			sy = data16[1] + y;
			sx = w - data16[2] + x;
			ey = data16[3] + y;
		}
		else
		{
			sx = data16[0] + x;
			sy = data16[1] + y;
			ex = data16[2] + x;
			ey = data16[3] + y;
		}
		data16 += 4;

		LRect drect;
		lrect_set(&drect, sx, sy, ex + 1, ey + 1);
		LRect clipRect = drect;

		JBool retValue = JFALSE;
		if (lcanvas_clipRectToCanvas(&clipRect))
		{
			if (lrect_equal(&clipRect, &drect))
			{
				deltaFlip(data16, x, y, w);
			}
			else
			{
				deltaFlipClip(data16, x, y, w);
			}

			if (dirty)
			{
				dirtyRect(&clipRect);
			}
			retValue = JTRUE;
		}

		return retValue;
	}
}  // namespace TFE_DarkForces