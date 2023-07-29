#include "rwallV2.h"
#include "rsectorV2.h"
#include "rslopeV2.h"
#include <TFE_System/system.h>
//#include <TFE_Jedi/Collision/collision.h>

#define DB_CLASS_WALL			0x00010000
#define getWallId(p) ((iptr(p) & 0xffffu) + DB_CLASS_WALL)
#define TEXELS_PER_UNIT 8.0f

namespace TFE_Jedi
{
	static vec2_float s_lineInt = { 0 };

	void wall_getHeights(const RSectorV2* sector, vec2_float p0, vec2_float p1, f32* f0, f32* f1, f32* c0, f32* c1);
		
	void wall_init(RWallV2* wall)
	{
		memset(wall, 0, sizeof(RWallV2));
		wall->id = getWallId(wall);
	}

	void wall_free(RWallV2* wall)
	{
		wall->id = 0;
	}

	void wall_computeAdjoinType(RWallV2* wall)
	{
		if (!wall) { return; }

		const f32 eps = 0.0001f;
		const f32 x0 = wall->w0->x;
		const f32 z0 = wall->w0->z;
		const f32 x1 = wall->w1->x;
		const f32 z1 = wall->w1->z;
		const RSectorV2* sector = wall->sector;
		s32 adjoinType = ADJOIN_NONE;

		f32 f0, f1;	  // floor height.
		f32 c0, c1;	  // ceiling height.
		f32 mf0, mf1; // mirror heights
		f32 mc0, mc1;
		f32 df0, df1; // dmirror heights
		f32 dc0, dc1;
		if (wall->dadjoin)
		{
			const RWallV2* mirror = wall->mirror;
			const RWallV2* dmirror = wall->dmirror;
			const RSectorV2* mirrorSector = mirror->sector;
			const RSectorV2* dmirrorSector = dmirror->sector;

			// Calculate the floor and ceiling height at each vertex.
			wall_getHeights(sector, { x0, z0 }, { x1, z1 }, &f0, &f1, &c0, &c1);
			wall_getHeights(mirrorSector, { x0, z0 }, { x1, z1 }, &mf0, &mf1, &mc0, &mc1);
			wall_getHeights(dmirrorSector, { x0, z0 }, { x1, z1 }, &df0, &df1, &dc0, &dc1);

			// Handle floating point accuracy.
			if (fabsf(c0 - mc0) < eps) { mc0 = c0; }
			if (fabsf(c0 - dc0) < eps) { dc0 = c0; }
			if (fabsf(c1 - mc1) < eps) { mc1 = c1; }
			if (fabsf(c1 - dc1) < eps) { dc1 = c1; }

			if (fabsf(f0 - mf0) < eps) { mf0 = f0; }
			if (fabsf(f0 - df0) < eps) { df0 = f0; }
			if (fabsf(f1 - mf1) < eps) { mf1 = f1; }
			if (fabsf(f1 - df1) < eps) { df1 = f1; }

			// Calculate adjoin type.
			adjoinType = ADJOIN_DOUBLE;
			if ((c0 >= mc0 && c1 > mc1) || (c0 > mc0 && c1 >= mc1))
			{
				adjoinType |= ADJOIN_TOP;
			}
			else if ((c0 > mc0 && c1 < mc1) || (c0 < mc0 && c1 > mc1))
			{
				TFE_System::logWrite(LOG_ERROR, "RWall", "Wall:  invalid adjoin type on wall %x in sector %x\n", wall->srcId, wall->sector->id);
				TFE_System::logWrite(LOG_ERROR, "RWall", "       cLeft = %f\tcMirrorLeft = %f\tcRight = %f\tcMirrorRight = %f\n", c0, mc0, c1, mc1);
			}

			if ((f0 <= df0 && f1 < df1) || (f0 < df0 && f1 <= df1))
			{
				adjoinType |= ADJOIN_BOTTOM;
			}
			else if ((f0 < df0 && f1 > df1) || (f0 > df0 && f1 < df1))
			{
				TFE_System::logWrite(LOG_ERROR, "RWall", "Wall:  invalid adjoin type on wall %x in sector %x\n", wall->srcId, wall->sector->id);
				TFE_System::logWrite(LOG_ERROR, "RWall", "       fLeft = %f\tfMirrorLeft = %f\tfRight = %f\tfMirrorRight = %f\n", f0, df0, f1, df1);
			}
		}
		else if (wall->adjoin)
		{
			const RWallV2* mirror = wall->mirror;
			const RSectorV2* mirrorSector = mirror->sector;

			// Calculate the floor and ceiling height at each vertex.
			wall_getHeights(sector, { x0, z0 }, { x1, z1 }, &f0, &f1, &c0, &c1);
			wall_getHeights(mirrorSector, { x0, z0 }, { x1, z1 }, &mf0, &mf1, &mc0, &mc1);

			// Handle floating point accuracy.
			if (fabsf(c0 - mc0) < eps) { mc0 = c0; }
			if (fabsf(c1 - mc1) < eps) { mc1 = c1; }

			if (fabsf(f0 - mf0) < eps) { mf0 = f0; }
			if (fabsf(f1 - mf1) < eps) { mf1 = f1; }

			// Calculate adjoin type.
			if ((c0 >= mc0 && c1 > mc1) || (c0 > mc0 && c1 >= mc1))
			{
				adjoinType |= ADJOIN_TOP;
			}
			else if ((c0 > mc0 && c1 < mc1) || (c0 < mc0 && c1 > mc1))
			{
				TFE_System::logWrite(LOG_ERROR, "RWall", "Wall:  invalid adjoin type on wall %x in sector %x\n", wall->srcId, wall->sector->id);
				TFE_System::logWrite(LOG_ERROR, "RWall", "       cLeft = %f\tcMirrorLeft = %f\tcRight = %f\tcMirrorRight = %f\n", c0, mc0, c1, mc1);
			}

			if ((f0 <= mf0 && f1 < mf1) || (f0 < mf0 && f1 <= mf1))
			{
				adjoinType |= ADJOIN_BOTTOM;
			}
			else if ((f0 < mf0 && f1 > mf1) || (f0 > mf0 && f1 < mf1))
			{
				TFE_System::logWrite(LOG_ERROR, "RWall", "Wall:  invalid adjoin type on wall %x in sector %x\n", wall->srcId, wall->sector->id);
				TFE_System::logWrite(LOG_ERROR, "RWall", "       fLeft = %f\tfMirrorLeft = %f\tfRight = %f\tfMirrorRight = %f\n", f0, mf0, f1, mf1);
			}
		}

		wall->adjoinType = adjoinType;
	}

	void wall_computeTexelHeights(RWallV2* wall)
	{
		const RSectorV2* sector = wall->sector;
		if (wall->dadjoin)
		{
			const RSectorV2* next = wall->adjoin;
			const RSectorV2* dnext = wall->dadjoin;
			if (wall->adjoinType & ADJOIN_TOP)
			{
				wall->topTexelHeight = (sector->ceilHeight - next->ceilHeight) * TEXELS_PER_UNIT;
			}
			if (wall->adjoinType & ADJOIN_BOTTOM)
			{
				wall->botTexelHeight = (dnext->floorHeight - sector->floorHeight) * TEXELS_PER_UNIT;
			}
			wall->midTexelHeight = (next->floorHeight - dnext->ceilHeight) * TEXELS_PER_UNIT;
			// Note: Dual adjoins and transparent mid-textures cannot be used at the same time.
		}
		else if (wall->adjoin)
		{
			const RSectorV2* next = wall->adjoin;
			if (wall->adjoinType & ADJOIN_TOP)
			{
				wall->topTexelHeight = (sector->ceilHeight - next->ceilHeight) * TEXELS_PER_UNIT;
			}
			if (wall->adjoinType & ADJOIN_BOTTOM)
			{
				wall->botTexelHeight = (next->floorHeight - sector->floorHeight) * TEXELS_PER_UNIT;
			}

			// Transparent middle texture.
			if (wall->texture[WTEX_MIDDLE])
			{
				if (wall->adjoinType & ADJOIN_BOTTOM)
				{
					if (wall->adjoinType & ADJOIN_TOP)  // Top and bottom.
					{
						wall->midTexelHeight = (next->ceilHeight - next->floorHeight) * TEXELS_PER_UNIT;
					}
					else  // Ceiling to bottom.
					{
						wall->midTexelHeight = (sector->ceilHeight - next->floorHeight) * TEXELS_PER_UNIT;
					}
				}
				else if (wall->adjoinType & ADJOIN_TOP) // Top to floor
				{
					wall->midTexelHeight = (next->ceilHeight - sector->floorHeight) * TEXELS_PER_UNIT;
				}
				else // Ceiling to floor.
				{
					wall->midTexelHeight = (sector->ceilHeight - sector->floorHeight) * TEXELS_PER_UNIT;
				}
			}
		}
		else
		{
			wall->midTexelHeight = (sector->ceilHeight - sector->floorHeight) * TEXELS_PER_UNIT;
		}
	}

	void wall_shatter(RWallV2* wall, bool broadcast)
	{
		if (wall->flags1 & WF1V2_SHATTER)
		{
			// TODO
		}
	}
		
	bool wall_lineCrosses(CollisionLine2D* line, RWallV2* wall)
	{
		const vec2_float p0 = line->p0;
		const vec2_float p1 = line->p1;
		const vec2_float p2 = *wall->w0;
		const vec2_float p3 = *wall->w1;

		vec2_float low, high;

		// Test the X interval.
		const f32 dx0 = p1.x - p0.x;
		const f32 dx1 = p2.x - p3.x;
		if (dx0 < 0) { low.x = p1.x; high.x = p0.x; }
		else { high.x = p1.x; low.x = p0.x; }

		if (dx1 > 0) { if (high.x < p3.x || p2.x < low.x) { return false; } }
		else { if (high.x < p2.x || p3.x < low.x) { return false; } }

		// Test the Z interval.
		const f32 dz0 = p1.z - p0.z;
		const f32 dz1 = p2.z - p3.z;
		if (dz0 < 0) { low.z = p1.z; high.z = p0.z; }
		else { high.z = p1.z; low.z = p0.z; }

		if (dz1 > 0 && (high.z < p3.z || p2.z < low.z)) { return false; }
		else if (dz1 <= 0 && (high.z < p2.z || p3.z < low.z)) { return false; }

		// Test the parametric ranges.
		const f32 dx02 = p0.x - p2.x;
		const f32 dz02 = p0.z - p2.z;
		const f32 num = dz1 * dx02 - dx1 * dz02;
		const f32 den = dz0 * dx1 - dx0 * dz1;
		if (den > 0.0f && (num < 0.0f || num > den)) { return false; }
		else if (den <= 0.0f && (num > 0.0f || num < den)) { return false; }

		const f32 num2 = dx0 * dz02 - dz0 * dx02;
		if (den > 0.0f && (num2 < 0.0f || num2 > den)) { return false; }
		else if (den <= 0.0f && (num2 > 0.0f || num2 < den)) { return false; }

		// If the denominator is 0, then the lines are collinear.
		if (den == 0.0f) { return false; }

		// Just do the calculation in place.
		const f32 param = num / den;
		s_lineInt.x = line->p0.x + param * dx0;
		s_lineInt.z = line->p0.z + param * dz0;
		return true;
	}

	void wall_getLineIntersection(vec2_float* it)
	{
		*it = s_lineInt;
	}

	RSectorV2* wall_getAdjoinAtHeight(RWallV2* wall, f32 height)
	{
		return (wall->dadjoin && height < wall->adjoin->floorHeight) ? wall->dadjoin : wall->adjoin;
	}
		
	s32 wall_isPassable(RWallV2* wall)
	{
		return (wall->flags1 & WF1V2_SOLID_WALL) ? 0 : 1;
	}

	s32 wall_isRail(RWallV2* wall, void* collide)
	{
		return (wall->flags2 & WF2V2_RAIL) ? 0 : 1;
	}

	// TODO:
	// Rotate, Move, Events, Collision, ...

	///////////////////////////////////////////
	// Internal
	///////////////////////////////////////////
	void wall_getHeights(const RSectorV2* sector, vec2_float p0, vec2_float p1, f32* f0, f32* f1, f32* c0, f32* c1)
	{
		assert(f0 && f1 && c0 && c1);

		// Calculate the floor and ceiling height at each vertex.
		if (sector->slopedFloor)
		{
			*f0 = slope_getHeightAtXZ(sector->slopedFloor, p0);
			*f1 = slope_getHeightAtXZ(sector->slopedFloor, p1);
		}
		else
		{
			*f0 = sector->floorHeight;
			*f1 = sector->floorHeight;
		}

		if (sector->slopedCeil)
		{
			*c0 = slope_getHeightAtXZ(sector->slopedCeil, p0);
			*c1 = slope_getHeightAtXZ(sector->slopedCeil, p1);
		}
		else
		{
			*c0 = sector->ceilHeight;
			*c1 = sector->ceilHeight;
		}
	}

#if 0
	void wall_setupAdjoinDrawFlags(RWallV2* wall)
	{
		if (wall->nextSector)
		{
			RSectorV2* sector = wall->sector;
			RWallV2* mirror = wall->mirrorWall;
			f32 wFloorHeight = sector->floorHeight;
			f32 wCeilHeight = sector->ceilHeight;

			RSectorV2* nextSector = mirror->sector;
			f32 mFloorHeight = nextSector->floorHeight;
			f32 mCeilHeight = nextSector->ceilHeight;
			wall->drawFlags = 0;
			mirror->drawFlags = 0;

			if (wCeilHeight < mCeilHeight)
			{
				wall->drawFlags |= WDFV2_TOP;
			}
			if (wFloorHeight > mFloorHeight)
			{
				wall->drawFlags |= WDFV2_BOT;
			}
			if (mCeilHeight < wCeilHeight)
			{
				mirror->drawFlags |= WDFV2_TOP;
			}
			if (mFloorHeight > wFloorHeight)
			{
				mirror->drawFlags |= WDFV2_BOT;
			}
		}
	}

	void wall_computeTexelHeights(RWallV2* wall)
	{
		wall->sector->dirtyFlags |= SDFV2_HEIGHTS;

		if (wall->nextSector)
		{
			if (wall->drawFlags & WDFV2_TOP)
			{
				RSectorV2* next = wall->nextSector;
				RSectorV2* cur = wall->sector;
				wall->topTexelHeight = (next->ceilingHeight - cur->ceilingHeight) << 3;
			}
			if (wall->drawFlags & WDFV2_BOT)
			{
				RSectorV2* cur = wall->sector;
				RSectorV2* next = wall->nextSector;
				wall->botTexelHeight = (cur->floorHeight - next->floorHeight) << 3;
			}

			if (wall->midTex)
			{
				if (!(wall->drawFlags & WDFV2_BOT) && !(wall->drawFlags & WDFV2_TOP))
				{
					RSectorV2* midSector = wall->sector;
					fixed16_16 midFloorHeight = wall->sector->floorHeight;
					wall->midTexelHeight = (midFloorHeight - midSector->ceilingHeight) << 3;
				}
				else if (!(wall->drawFlags & WDFV2_BOT))
				{
					RSectorV2* midSector = wall->nextSector;
					fixed16_16 midFloorHeight = wall->sector->floorHeight;
					wall->midTexelHeight = (midFloorHeight - midSector->ceilingHeight) << 3;
				}
				else if (!(wall->drawFlags & WDFV2_TOP))
				{
					RSectorV2* midSector = wall->sector;
					fixed16_16 midFloorHeight = wall->nextSector->floorHeight;
					wall->midTexelHeight = (midFloorHeight - midSector->ceilingHeight) << 3;
				}
				else // WDFV2_TOP_AND_BOT
				{
					RSectorV2* midSector = wall->nextSector;
					fixed16_16 midFloorHeight = wall->nextSector->floorHeight;
					wall->midTexelHeight = (midFloorHeight - midSector->ceilingHeight) << 3;
				}
			}
		}
		else
		{
			RSectorV2* midSector = wall->sector;
			fixed16_16 midFloorHeight = midSector->floorHeight;
			wall->midTexelHeight = (midFloorHeight - midSector->ceilingHeight) << 3;
		}
	}

	fixed16_16 wall_computeDirectionVector(RWallV2* wall)
	{
		wall->sector->dirtyFlags |= SDFV2_WALL_SHAPE;

		// Calculate dx and dz
		fixed16_16 dx = wall->w1->x - wall->w0->x;
		fixed16_16 dz = wall->w1->z - wall->w0->z;

		// The original DOS code converts to floating point here to compute the length but
		// switches back to fixed point for the divide.
		// Convert to floating point
		f32 fdx = fixed16ToFloat(dx);
		f32 fdz = fixed16ToFloat(dz);
		// Compute the squared distance in floating point.
		f32 lenSq = fdx * fdx + fdz * fdz;
		f32 len = sqrtf(lenSq);
		fixed16_16 lenFixed = floatToFixed16(len);

		if (lenFixed)
		{
			wall->wallDir.x = div16(dx, lenFixed);
			wall->wallDir.z = div16(dz, lenFixed);
		}
		else
		{
			wall->wallDir.x = 0;
			wall->wallDir.z = 0;
		}
		return lenFixed;
	}

	void wall_getOpeningHeightRange(RWallV2* wall, fixed16_16* topRes, fixed16_16* botRes)
	{
		fixed16_16 curCeilHeight;
		fixed16_16 curFloorHeight;
		RSectorV2* curSector = wall->sector;
		RSectorV2* nextSector = wall->nextSector;
		sector_getFloorAndCeilHeight(curSector, &curFloorHeight, &curCeilHeight);

		fixed16_16 nextCeilHeight;
		fixed16_16 nextFloorHeight;
		sector_getFloorAndCeilHeight(nextSector, &nextFloorHeight, &nextCeilHeight);

		if (nextSector)  // <- This check is too late...
		{
			// There is an upper wall since the current ceiling is higher than the next.
			fixed16_16 top = curCeilHeight;
			if (curCeilHeight <= nextCeilHeight)
			{
				top = nextCeilHeight;
			}
			*topRes = top;

			fixed16_16 bot = curFloorHeight;
			if (curFloorHeight >= nextFloorHeight)
			{
				bot = nextFloorHeight;
			}
			*botRes = bot;
		}
		else
		{
			*topRes = FIXED(9999);// COL_INFINITY;
			*botRes = -FIXED(9999);//COL_INFINITY;
		}
	}
#endif
} // namespace TFE_Jedi