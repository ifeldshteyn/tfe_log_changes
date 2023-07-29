#include <climits>
#include <cstring>

#include "rsectorV2.h"
#include "rwallV2.h"
#include "rslopeV2.h"
#include "levelDataV2.h"
#include <TFE_Game/igame.h>
#include <TFE_System/system.h>

#define PHYSICS_DEFAULT_GRAVITY 	-60.0f
#define PHYSICS_DEFAULT_FRICTION	1.0f
#define PHYSICS_DEFAULT_ELASTICITY	0.3f

#define SIGN(x) ((x) < 0 ? -1 : 1)

namespace TFE_Jedi
{
	enum LineXIntersect
	{
		LX_NO_INTERSECT = -1,
		LX_ON_LINE = 0,
		LX_INTERSECT = 1,
	};

	bool sector_posInsideBounds(vec2_float pos, f32* area, const Rect* rect);
	bool sector_pointInside3D(RSectorV2* sector, vec3_float pos);
	LineXIntersect lineSegmentXIntersect(vec2_float p, vec2_float p0, vec2_float p1);

	void sector_init(RSectorV2* sector)
	{
		memset(sector, 0, sizeof(RSectorV2));

		sector->self       = sector;
		sector->index      = -1;
		sector->id         = -1;
		sector->ambient    = 31;
		sector->friction   = PHYSICS_DEFAULT_FRICTION;
		sector->gravity    = PHYSICS_DEFAULT_GRAVITY;
		sector->elasticity = PHYSICS_DEFAULT_ELASTICITY;
	}

	void sector_free(RSectorV2* sector)
	{
		if (!sector) { return; }

		if (sector->self)
		{
			// TODO

			sector->self = nullptr;
		}
	}

	void sector_computeAdjoinsAndHeights(RSectorV2* sector)
	{
		const s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->adjoin)
			{
				wall_computeAdjoinType(wall);
				wall_computeAdjoinType(wall->mirror);
				wall_computeTexelHeights(wall->mirror);
			}
			wall_computeTexelHeights(wall);
		}
	}

	s32 sector_computeConvexity(RSectorV2* sector)
	{
		s32 convexity = SECV2_CONVEX;

		const s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		for (s32 i0 = 0; i0 < wallCount && convexity > SECV2_NONCONVEXNONCLOSED; i0++, wall++)
		{
			const s32 i1 = (i0 + 1) % wallCount;
			RWallV2* wall0 = &sector->walls[i0];
			RWallV2* wall1 = &sector->walls[i1];

			if (wall0->w1 != wall1->w0)
			{
				// The shape is not closed, or is complex (it might have holes, etc.).
				convexity = SECV2_NONCONVEXNONCLOSED;
			}
			else if (convexity == SECV2_CONVEX)
			{
				const angle14_16 deltaAngle = (wall1->angle - wall0->angle) & ANGLE_MASK;
				if (deltaAngle > 8191)
				{
					convexity = SECV2_NONCONVEXCLOSED;
				}
			}
		}

		return convexity;
	}

	void sector_computeBounds(RSectorV2* sector, Rect* rect)
	{
		RWallV2* wall = sector->walls;
		vec2_float boundsMin = *wall->w0;
		vec2_float boundsMax = *wall->w0;
		wall++;

		for (s32 w = 1; w < sector->wallCount; w++, wall++)
		{
			boundsMin.x = min(boundsMin.x, wall->w0->x);
			boundsMin.z = min(boundsMin.z, wall->w0->z);

			boundsMax.x = max(boundsMax.x, wall->w0->x);
			boundsMax.z = max(boundsMax.z, wall->w0->z);
		}

		rect->x = boundsMin.x;
		rect->z = boundsMin.z;
		rect->w = boundsMax.x - boundsMin.x;
		rect->h = boundsMax.z - boundsMin.z;
	}

	RSectorV2* sector_getById(LevelDataV2* level, s32 id)
	{
		RSectorV2* sector = level->sectors;
		for (s32 s = 0; s < level->sectorCount; s++, sector++)
		{
			if (sector->id == id)
			{
				return sector;
			}
		}
		return nullptr;
	}
		
	RSectorV2* sector_which3D(LevelDataV2* level, vec3_float pos)
	{
		RSectorV2* sector = level->sectors;
		for (s32 s = 0; s < level->sectorCount; s++, sector++)
		{
			if (sector_pointInside3D(sector, pos))
			{
				return sector;
			}
		}
		return nullptr;
	}
		
	RSectorV2* sector_which3DFast(RSectorV2* sector, vec3_float pos)
	{
		// First try the passed in sector.
		if (sector_pointInside3D(sector, pos))
		{
			return sector;
		}

		// Then test the adjoins.
		RWallV2* wall = sector->walls;
		const s32 wallCount = sector->wallCount;
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->dadjoin)
			{
				sector = wall->dadjoin;
				if (sector_pointInside3D(sector, pos))
				{
					return sector;
				}
			}
			if (wall->adjoin)
			{
				sector = wall->adjoin;
				if (sector_pointInside3D(sector, pos))
				{
					return sector;
				}
			}
		}
		return nullptr;
	}

	RSectorV2* sector_which2D(LevelDataV2* level, vec2_float pos)
	{
		f32 foundArea = FLT_MAX;
		RSectorV2* foundSector = nullptr;

		RSectorV2* sector = level->sectors;
		for (s32 s = 0; s < level->sectorCount; s++, sector++)
		{
			f32 area;
			if (sector_pointInside(sector, pos, &area))
			{
				if (area < foundArea)
				{
					foundArea = area;
					foundSector = sector;
				}
			}
		}
		return foundSector;
	}

	struct SectorWallIntersect
	{
		CollisionLine2D line;
		f32 bestDist;
		f32 lastDist;
		vec2_float bestPos;
	};
	static SectorWallIntersect s_lineInt;
	static u32 s_logicFrame = 0;

	u32 nextLogicFrame()
	{
		return ++s_logicFrame;
	}

	u32 getLogicFrame()
	{
		return s_logicFrame;
	}

	RWallV2* sector_findWallIntersect(RSectorV2* sector, vec2_float p0, vec2_float p1)
	{
		if (p1.x - p0.x == 0.0f && p1.z - p0.z == 0.0f) { return nullptr; }
		sector->logicFrame = nextLogicFrame();

		s_lineInt.line.p0 = p0;
		s_lineInt.line.p1 = p1;
		s_lineInt.lastDist = -FLT_MAX;
		return sector_nextWallIntersect(sector);
	}

	RWallV2* sector_nextWallIntersect(RSectorV2* sector)
	{
		u32 logicFrame = getLogicFrame();
		RWallV2* wall = sector->walls;
		RWallV2* bestWall = nullptr;
		CollisionLine2D line = s_lineInt.line;
		for (s32 w = 0; w < sector->wallCount; w++, wall++)
		{
			if (wall->logicFrame == logicFrame) { continue; }

			bool cross = wall_lineCrosses(&line, wall);
			if (cross)
			{
				vec2_float point;
				wall_getLineIntersection(&point);

				if (point.x == line.p0.x && point.z == line.p0.z)
				{
					f32 dx = line.p1.x - line.p0.x;
					f32 dz = line.p1.z - line.p0.z;
					dx = dx * wall->wallDir.z - dz * wall->wallDir.x;
					if (dx >= 0.0f)
					{
						cross = false;
					}
				}

				if (cross)
				{
					f32 dist = distApprox(line.p0.x, line.p0.z, point.x, point.z);
					if (dist < s_lineInt.bestDist && dist > s_lineInt.lastDist)
					{
						bestWall = wall;
						s_lineInt.bestDist = dist;
						s_lineInt.bestPos = point;
					}
				}
			}
		}

		if (bestWall)
		{
			bestWall->logicFrame = logicFrame;
			if (wall->mirror)
			{
				wall->mirror->logicFrame = logicFrame;
			}
			if (wall->dmirror)
			{
				wall->dmirror->logicFrame = logicFrame;
			}
			s_lineInt.lastDist = s_lineInt.bestDist;
		}
		return bestWall;
	}

	void sector_getBestWallIntersect(f32* len, vec2_float* pos)
	{
		*len = s_lineInt.bestDist;
		*pos = s_lineInt.bestPos;
	}
		
	bool sector_pointInside(RSectorV2* sector, vec2_float pos, f32* area)
	{
		Rect rect;
		sector_computeBounds(sector, &rect);
		if (!sector_posInsideBounds(pos, area, &rect))
		{
			return false;
		}

		RWallV2* wall = &sector->walls[sector->wallCount-1];
		f32 prevDeltaZ = wall->w1->z - wall->w0->z;

		s32 count = 0;
		wall = sector->walls;
		for (s32 w = 0; w < sector->wallCount; w++, wall++)
		{
			const vec2_float p0 = *wall->w0;
			const vec2_float p1 = *wall->w1;
			const f32 deltaZ = p1.z - p0.z;

			if (deltaZ != 0.0f)
			{
				if (pos.z == p0.z)
				{
					if (pos.x == p0.x)
					{
						TFE_System::logWrite(LOG_WARNING, "Sector", "Sector_Which2D: Object at (%f,%f) lies on wall of Sector #%d", pos.x, pos.z, sector->id);
						count = 1;
						break;
					}

					if (pos.x < p0.x && (SIGN(deltaZ) == SIGN(prevDeltaZ) || prevDeltaZ == 0.0f))
					{
						count++;
					}
				}
				else if (pos.z != p1.z)
				{
					LineXIntersect xInt = lineSegmentXIntersect(pos, p0, p1);
					if (xInt == LX_INTERSECT)
					{
						count++;
					}
					else if (xInt == LX_ON_LINE)
					{
						TFE_System::logWrite(LOG_WARNING, "Sector", "Sector_Which2D: Object at (%f,%f) lies on wall of Sector #%d", pos.x, pos.z, sector->id);
						count = 1;
						break;
					}
				}
				prevDeltaZ = deltaZ;
			}
			else if (lineSegmentXIntersect(pos, p0, p1) == LX_ON_LINE)
			{
				TFE_System::logWrite(LOG_WARNING, "Sector", "Sector_Which2D: Object at (%f,%f) lies on wall of Sector #%d", pos.x, pos.z, sector->id);
				count = 1;
				break;
			}
		}

		return (count & 1) == 1;
	}

	// Internal only.
	bool sector_posInsideBounds(vec2_float pos, f32* area, const Rect* rect)
	{
		const vec2_float minPos = { rect->x, rect->z };
		const vec2_float maxPos = { rect->x + rect->w, rect->z + rect->h };
		*area = rect->w * rect->h;
		return (pos.x < minPos.x || pos.x > maxPos.x || pos.z < minPos.z || pos.z > maxPos.z) ? false : true;
	}

	// Internal only.
	LineXIntersect lineSegmentXIntersect(vec2_float p, vec2_float p0, vec2_float p1)
	{
		f32 dx = p0.x - p1.x;
		f32 dz = p0.z - p1.z;
		if (dx == 0)
		{
			if (dz > 0)
			{
				if (p.z < p1.z || p.z > p0.z || p.x > p0.x) { return LX_NO_INTERSECT; }
			}
			else
			{
				if (p.z < p0.z || p.z > p1.z || p.x > p0.x) { return LX_NO_INTERSECT; }
			}
			return (p.x == p0.x) ? LX_ON_LINE : LX_INTERSECT;
		}
		else if (dz == 0)
		{
			if (p.z != p0.z)
			{
				return LX_NO_INTERSECT;
			}
			if (dx > 0)
			{
				return (p.x > p0.x) ? LX_NO_INTERSECT : (p.x < p1.x) ? LX_INTERSECT : LX_ON_LINE;
			}
			return (p.x > p1.x) ? LX_NO_INTERSECT : (p.x < p0.x) ? LX_INTERSECT : LX_ON_LINE;
		}
		else if (dx > 0)
		{
			if (p.x > p0.x) { return LX_NO_INTERSECT; }

			p.x -= p1.x;
			if (dz > 0)
			{
				if (p.z < p1.z || p.z > p0.z) { return LX_NO_INTERSECT; }
				p.z -= p1.z;
			}
			else
			{
				if (p.z < p0.z || p.z > p1.z) { return LX_NO_INTERSECT; }
				dz = -dz;
				p.z = p1.z - p.z;
			}
		}
		else // dx <= 0
		{
			if (p.x > p1.x) { return LX_NO_INTERSECT; }

			p.x -= p0.x;
			dx = -dx;
			if (dz > 0)
			{
				if (p.z < p1.z || p.z > p0.z) { return LX_NO_INTERSECT; }
				p.z = p0.z - p.z;
			}
			else  // dz <= 0
			{
				if (p.z < p0.z || p.z > p1.z) { return LX_NO_INTERSECT; }
				dz = -dz;
				p.z -= p0.z;
			}
		}
		const f32 zDx = p.z * dx;
		const f32 xDz = p.x * dz;
		if (xDz == zDx)
		{
			return LX_ON_LINE;
		}
		return (xDz > zDx) ? LX_NO_INTERSECT : LX_INTERSECT;
	}

	// Internal
	bool sector_pointInside3D(RSectorV2* sector, vec3_float pos)
	{
		f32 area;
		const f32 ceiling = sector->slopedCeil ?
			slope_getHeightAtXZ(sector->slopedCeil, { pos.x, pos.z }) : sector->ceilHeight;
		const f32 floor = sector->slopedFloor ?
			slope_getHeightAtXZ(sector->slopedFloor, { pos.x, pos.z }) : sector->floorHeight;
		if (pos.y <= ceiling && pos.y >= floor && sector_pointInside(sector, { pos.x, pos.z }, &area))
		{
			return true;
		}
		return false;
	}

#if 0
	// Internal Forward Declarations
	void sector_computeWallDirAndLength(RWallV2* wall);
	void sector_moveWallVertex(RWallV2* wall, fixed16_16 offsetX, fixed16_16 offsetZ);
	JBool sector_objOverlapsWall(RWallV2* wall, SecObjectV2* obj, s32* objSide);
	JBool sector_canWallMove(RWallV2* wall, fixed16_16 offsetX, fixed16_16 offsetZ);
	void sector_moveObjects(RSectorV2* sector, u32 flags, fixed16_16 offsetX, fixed16_16 offsetZ);

	static f32 isLeft(Vec2f p0, Vec2f p1, Vec2f p2);
	
	/////////////////////////////////////////////////
	// API Implementation
	/////////////////////////////////////////////////
	void sector_clear(RSectorV2* sector)
	{
		sector->vertexCount = 0;
		sector->wallCount = 0;
		sector->objectCount = 0;
		sector->secHeight = 0;
		sector->collisionFrame = 0;
		sector->id = 0;
		sector->prevDrawFrame = 0;
		sector->infLink = 0;
		sector->objectCapacity = 0;
		sector->verticesWS = nullptr;
		sector->verticesVS = nullptr;
		sector->self = sector;
	}

	void sector_setupWallDrawFlags(RSectorV2* sector)
	{
		RWallV2* wall = sector->walls;
		for (s32 w = 0; w < sector->wallCount; w++, wall++)
		{
			wall->drawFlags = WDFV2_MIDDLE;
			if (wall->nextSector)
			{
				RSectorV2* wSector = wall->sector;
				fixed16_16 wFloorHeight = wSector->floorHeight;
				fixed16_16 wCeilHeight = wSector->ceilingHeight;

				RWallV2* mirror = wall->mirrorWall;
				RSectorV2* mSector = mirror->sector;
				fixed16_16 mFloorHeight = mSector->floorHeight;
				fixed16_16 mCeilHeight = mSector->ceilingHeight;
				assert(mSector == wall->nextSector);

				//wall->drawFlags = WDF_MIDDLE;
				mirror->drawFlags = WDFV2_MIDDLE;

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
				wall_computeTexelHeights(wall->mirrorWall);
			}
			wall_computeTexelHeights(wall);
		}
	}
		
	void sector_adjustHeights(RSectorV2* sector, fixed16_16 floorOffset, fixed16_16 ceilOffset, fixed16_16 secondHeightOffset)
	{
		sector->dirtyFlags |= SDFV2_HEIGHTS;

		// Adjust objects.
		if (sector->objectCount)
		{
			fixed16_16 heightOffset = secondHeightOffset + floorOffset;
			#if 0
			for (s32 i = 0; i < sector->objectCapacity; i++)
			{
				SecObjectV2* obj = sector->objectList[i];
				if (!obj) { continue; }
				
				if (obj->posWS.y == sector->floorHeight)
				{
					obj->posWS.y += floorOffset;
					/*
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						s_playerYPos += floorOffset;
					}
					*/
				}

				if (obj->posWS.y == sector->ceilingHeight)
				{
					obj->posWS.y += ceilOffset;
					/*
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						// Why not ceilingOffset?
						s_playerYPos += floorOffset;
					}
					*/
				}

				fixed16_16 secHeight = sector->floorHeight + sector->secHeight;
				if (obj->posWS.y == secHeight)
				{
					obj->posWS.y += heightOffset;
					/*
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						// Dark Forces Bug:
						// The original code was `s_playerYPos += floorOffset;` here instead, which made second-height based elevators
						// jittery in DOS. This change makes the elevators smooth (such as move_offset).
						s_playerYPos += heightOffset;
					}
					*/
				}
			}
			#endif
		}
		// Adjust sector heights.
		sector->ceilingHeight += ceilOffset;
		sector->floorHeight += floorOffset;
		sector->secHeight += secondHeightOffset;

		// Update wall data.
		s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->nextSector)
			{
				wall_setupAdjoinDrawFlags(wall);
				wall_computeTexelHeights(wall->mirrorWall);
			}
			wall_computeTexelHeights(wall);
		}

		// Update collision data.
		fixed16_16 floorHeight = sector->floorHeight;
		if (sector->flags1 & SECV2_FLAGS1_PIT)
		{
			floorHeight += SECV2_SKY_HEIGHT;
		}
		fixed16_16 ceilHeight = sector->ceilingHeight;
		if (sector->flags1 & SECV2_FLAGS1_EXTERIOR)
		{
			ceilHeight -= SECV2_SKY_HEIGHT;
		}
		fixed16_16 secHeight = sector->floorHeight + sector->secHeight;
		if (sector->secHeight >= 0 && floorHeight > secHeight)
		{
			secHeight = floorHeight;
		}

		sector->colFloorHeight = floorHeight;
		sector->colCeilHeight  = ceilHeight;
		sector->colSecHeight   = secHeight;
		sector->colSecCeilHeight = ceilHeight;
	}

	void sector_computeBounds(RSectorV2* sector)
	{
		RWallV2* wall = sector->walls;
		vec2_fixed* w0 = wall->w0;
		fixed16_16 maxX = w0->x;
		fixed16_16 maxZ = w0->z;
		fixed16_16 minX = maxX;
		fixed16_16 minZ = maxZ;

		wall++;
		for (s32 i = 1; i < sector->wallCount; i++, wall++)
		{
			w0 = wall->w0;

			minX = min(minX, w0->x);
			minZ = min(minZ, w0->z);

			maxX = max(maxX, w0->x);
			maxZ = max(maxZ, w0->z);
		}

		sector->boundsMin.x = minX;
		sector->boundsMax.x = maxX;
		sector->boundsMin.z = minZ;
		sector->boundsMax.z = maxZ;
	}

	fixed16_16 sector_getMaxObjectHeight(RSectorV2* sector)
	{
		s32 maxObjHeight = 0;
		/*
		SecObject** objectList = sector->objectList;
		for (s32 count = sector->objectCount; count > 0; objectList++)
		{
			SecObject* obj = *objectList;
			if (obj)
			{
				maxObjHeight = max(maxObjHeight, obj->worldHeight + ONE_16);
				count--;
			}
		}
		*/
		return maxObjHeight;
	}
			
	JBool sector_moveWalls(RSectorV2* sector, fixed16_16 delta, fixed16_16 dirX, fixed16_16 dirZ, u32 flags)
	{
		fixed16_16 offsetX = mul16(delta, dirX);
		fixed16_16 offsetZ = mul16(delta, dirZ);

		JBool sectorBlocked = JFALSE;
		s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		for (s32 i = 0; i < wallCount && !sectorBlocked; i++, wall++)
		{
			if (wall->flags1 & WF1V2_WALL_MORPHS)
			{
				sectorBlocked |= sector_canWallMove(wall, offsetX, offsetZ);

				RWallV2* mirror = wall->mirrorWall;
				if (mirror && (mirror->flags1 & WF1V2_WALL_MORPHS))
				{
					sectorBlocked |= sector_canWallMove(mirror, offsetX, offsetZ);
				}
			}
		}

		if (!sectorBlocked)
		{
			sector->dirtyFlags |= SDFV2_VERTICES;

			wall = sector->walls;
			for (s32 i = 0; i < wallCount; i++, wall++)
			{
				if (wall->flags1 & WF1V2_WALL_MORPHS)
				{
					sector_moveWallVertex(wall, offsetX, offsetZ);
					RWallV2* mirror = wall->mirrorWall;
					if (mirror && (mirror->flags1 & WF1V2_WALL_MORPHS))
					{
						mirror->sector->dirtyFlags |= SDFV2_VERTICES;
						sector_moveWallVertex(mirror, offsetX, offsetZ);
					}
				}
			}
			sector_moveObjects(sector, flags, offsetX, offsetZ);
			sector_computeBounds(sector);
		}

		return ~sectorBlocked;
	}

	void sector_changeWallLight(RSectorV2* sector, fixed16_16 delta)
	{
		RWallV2* wall = sector->walls;
		s32 wallCount = sector->wallCount;

		sector->dirtyFlags |= SDFV2_AMBIENT;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1V2_CHANGE_WALL_LIGHT)
			{
				wall->wallLight += delta;
			}
		}
	}

	void sector_scrollWalls(RSectorV2* sector, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		RWallV2* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		sector->dirtyFlags |= SDFV2_WALL_OFFSETS;

		const u32 scrollFlags = WF1V2_SCROLL_SIGN_TEX | WF1V2_SCROLL_BOT_TEX | WF1V2_SCROLL_MID_TEX | WF1V2_SCROLL_TOP_TEX;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & scrollFlags)
			{
				if (wall->flags1 & WF1V2_SCROLL_TOP_TEX)
				{
					wall->topOffset.x += offsetX;
					wall->topOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1V2_SCROLL_MID_TEX)
				{
					wall->midOffset.x += offsetX;
					wall->midOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1V2_SCROLL_BOT_TEX)
				{
					wall->botOffset.x += offsetX;
					wall->botOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1V2_SCROLL_SIGN_TEX)
				{
					wall->signOffset.x += offsetX;
					wall->signOffset.z += offsetZ;
				}
			}
		}
	}

	void sector_adjustTextureWallOffsets_Floor(RSectorV2* sector, fixed16_16 floorDelta)
	{
		sector->dirtyFlags |= SDFV2_WALL_OFFSETS;

		RWallV2* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			fixed16_16 textureOffset = floorDelta * 8;
			if (wall->flags1 & WF1V2_TEX_ANCHORED)
			{
				if (wall->nextSector)
				{
					wall->botOffset.z -= textureOffset;
				}
				else
				{
					wall->midOffset.z -= textureOffset;
				}
			}
			if (wall->flags1 & WF1V2_SIGN_ANCHORED)
			{
				wall->signOffset.z -= textureOffset;
			}
		}
	}

	void sector_adjustTextureMirrorOffsets_Floor(RSectorV2* sector, fixed16_16 floorDelta)
	{
		RWallV2* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			RWallV2* mirror = wall->mirrorWall;
			if (mirror)
			{
				mirror->sector->dirtyFlags |= SDFV2_WALL_OFFSETS;

				fixed16_16 textureOffset = -floorDelta * 8;
				if (mirror->flags1 & WF1V2_TEX_ANCHORED)
				{
					if (mirror->nextSector)
					{
						mirror->botOffset.z -= textureOffset;
					}
					else
					{
						mirror->midOffset.z -= textureOffset;
					}
				}
				if (mirror->flags1 & WF1V2_SIGN_ANCHORED)
				{
					mirror->signOffset.z -= textureOffset;
				}
			}
		}
	}

	void sector_growObjectList(RSectorV2* sector)
	{
#if 0
		s32 objectCapacity = sector->objectCapacity;
		if (sector->objectCount >= objectCapacity)
		{
			SecObjectV2** list;
			if (!objectCapacity)
			{
				list = (SecObject**)level_alloc(sizeof(SecObject*) * 5);
				sector->objectList = list;
			}
			else
			{
				sector->objectList = (SecObject**)level_realloc(sector->objectList, sizeof(SecObject*) * (objectCapacity + 5));
				list = sector->objectList + objectCapacity;
			}
			memset(list, 0, sizeof(SecObject*) * 5);
			sector->objectCapacity += 5;
		}
#endif
	}

	void sector_addObjectToList(RSectorV2* sector, SecObjectV2* obj)
	{
#if 0
		// Then add the object to the first free slot.
		// Note that this scheme is optimized for deletion rather than addition.
		SecObject** list = sector->objectList;
		for (s32 i = 0; i < sector->objectCapacity; i++, list++)
		{
			if (!(*list))
			{
				*list = obj;
				obj->index = i;
				obj->sector = sector;
				sector->objectCount++;
				break;
			}
		}
#endif
	}

	// Skips some of the checks and does not send messages.
	// Used for serialization.
	void sector_addObjectDirect(RSectorV2* sector, SecObjectV2* obj)
	{
#if 0
		sector->dirtyFlags |= SDFV2_CHANGE_OBJ;

		// The sector containing the player has a special flag.
		if (obj->entityFlags & ETFLAG_PLAYER)
		{
			sector->flags1 |= SECV2_FLAGS1_PLAYER;
		}

		// Grow the object list if necessary.
		sector_growObjectList(sector);

		// Then add the object to the first free slot.
		sector_addObjectToList(sector, obj);
#endif
	}

	void sector_addObject(RSectorV2* sector, SecObjectV2* obj)
	{
#if 0
		if (sector != obj->sector)
		{
			sector->dirtyFlags |= SDFV2_CHANGE_OBJ;

			// Remove the object from its current sector (if it has one).
			if (obj->sector)
			{
				sector_removeObject(obj);
			}

			// The sector containing the player has a special flag.
			if (!((obj->entityFlags & ETFLAG_PLAYER) && s_playerDying))
			{
				message_sendToSector(sector, obj, INF_EVENT_ENTER_SECTOR, MSG_TRIGGER);
			}
			if (obj->entityFlags & ETFLAG_PLAYER)
			{
				sector->flags1 |= SECV2_FLAGS1_PLAYER;
			}

			// Grow the object list if necessary.
			sector_growObjectList(sector);

			// Then add the object to the first free slot.
			sector_addObjectToList(sector, obj);
		}
#endif
	}

	void sector_removeObject(SecObjectV2* obj)
	{
#if 0
		if (!obj || !obj->sector) { return; }
		
		RSectorV2* sector = obj->sector;
		obj->sector = nullptr;
		sector->dirtyFlags |= SDFV2_CHANGE_OBJ;

		// Remove the object from the object list.
		SecObject** objList = sector->objectList;
		objList[obj->index] = nullptr;
		sector->objectCount--;

		if (!((obj->entityFlags & ETFLAG_PLAYER) && s_playerDying))
		{
			message_sendToSector(sector, obj, INF_EVENT_LEAVE_SECTOR, MSG_TRIGGER);
		}
		if (obj->entityFlags & ETFLAG_PLAYER)
		{
			sector->flags1 &= ~SECV2_FLAGS1_PLAYER;
		}
#endif
	}

	void sector_changeGlobalLightLevelV2()
	{
#if 0
		RSectorV2* sector = s_levelState.sectors;
		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			fixed16_16 newLightLevel = intToFixed16(sector->flags3);
			sector->flags3 = floor16(sector->ambient);
			sector->ambient = newLightLevel;
		}
#endif
	}
	
	RSectorV2* sector_which3D(f32 dx, f32 dy, f32 dz)
	{
		/*
		fixed16_16 ix = dx;
		fixed16_16 iz = dz;
		fixed16_16 y = dy;
		
		RSectorV2* sector = s_levelState.sectors;
		RSectorV2* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			if (y >= sector->ceilingHeight && y <= sector->floorHeight)
			{
				const fixed16_16 sectorMaxX = sector->boundsMax.x;
				const fixed16_16 sectorMinX = sector->boundsMin.x;
				const fixed16_16 sectorMaxZ = sector->boundsMax.z;
				const fixed16_16 sectorMinZ = sector->boundsMin.z;

				const s32 dxInt = floor16(sectorMaxX - sectorMinX) + 1;
				const s32 dzInt = floor16(sectorMaxZ - sectorMinZ) + 1;
				sectorUnitArea = dzInt * dxInt;
				
				s32 insideBounds = 0;
				if (ix >= sectorMinX && ix <= sectorMaxX && iz >= sectorMinZ && iz <= sectorMaxZ)
				{
					// pick the containing sector with the smallest area.
					if (sectorUnitArea < prevSectorUnitArea && sector_pointInsideDF(sector, ix, iz))
					{
						prevSectorUnitArea = sectorUnitArea;
						foundSector = sector;
					}
				}
			}
		}

		return foundSector;
		*/
		return nullptr;
	}

	RSectorV2* sector_which3D_Map(f32 dx, f32 dz, s32 layer)
	{
		/*
		fixed16_16 ix = dx;
		fixed16_16 iz = dz;

		RSectorV2* sector = s_levelState.sectors;
		RSectorV2* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (u32 i = 0; i < s_levelState.sectorCount; i++, sector++)
		{
			if (sector->layer == layer)
			{
				const fixed16_16 sectorMaxX = sector->boundsMax.x;
				const fixed16_16 sectorMinX = sector->boundsMin.x;
				const fixed16_16 sectorMaxZ = sector->boundsMax.z;
				const fixed16_16 sectorMinZ = sector->boundsMin.z;

				const s32 dxInt = floor16(sectorMaxX - sectorMinX) + 1;
				const s32 dzInt = floor16(sectorMaxZ - sectorMinZ) + 1;
				sectorUnitArea = dzInt * dxInt;

				s32 insideBounds = 0;
				if (ix >= sectorMinX && ix <= sectorMaxX && iz >= sectorMinZ && iz <= sectorMaxZ)
				{
					// pick the containing sector with the smallest area.
					if (sectorUnitArea < prevSectorUnitArea && sector_pointInsideDF(sector, ix, iz))
					{
						prevSectorUnitArea = sectorUnitArea;
						foundSector = sector;
					}
				}
			}
		}

		return foundSector;
		*/
		return nullptr;
	}

	enum PointSegSide
	{
		PS_INSIDE = -1,
		PS_ON_LINE = 0,
		PS_OUTSIDE = 1
	};

	PointSegSide lineSegmentSideV2(fixed16_16 x, fixed16_16 z, fixed16_16 x0, fixed16_16 z0, fixed16_16 x1, fixed16_16 z1)
	{
		fixed16_16 dx = x0 - x1;
		fixed16_16 dz = z0 - z1;
		if (dx == 0)
		{
			if (dz > 0)
			{
				if (z < z1 || z > z0 || x > x0) { return PS_INSIDE; }
			}
			else
			{
				if (z < z0 || z > z1 || x > x0) { return PS_INSIDE; }
			}
			return (x == x0) ? PS_ON_LINE : PS_OUTSIDE;
		}
		else if (dz == 0)
		{
			if (z != z0)
			{
				// I believe this should be -1 or +1 depending on if z is less than or greater than z0.
				// Otherwise flat lines always give the same answer.
				return PS_INSIDE;
			}
			if (dx > 0)
			{
				return (x > x0) ? PS_INSIDE : (x < x1) ? PS_OUTSIDE : PS_ON_LINE;
			}
			return (x > x1) ? PS_INSIDE : (x < x0) ? PS_OUTSIDE : PS_ON_LINE;
		}
		else if (dx > 0)
		{
			if (x > x0) { return PS_INSIDE; }

			x -= x1;
			if (dz > 0)
			{
				if (z < z1 || z > z0) { return PS_INSIDE; }
				z -= z1;
			}
			else
			{
				if (z < z0 || z > z1) { return PS_INSIDE; }
				dz = -dz;
				z1 -= z;
				z = z1;
			}
		}
		else // dx <= 0
		{
			if (x > x1) { return PS_INSIDE; }

			x -= x0;
			dx = -dx;
			if (dz > 0)
			{
				if (z < z1 || z > z0) { return PS_INSIDE; }
				z0 -= z;
				z = z0;
			}
			else  // dz <= 0
			{
				if (z < z0 || z > z1) { return PS_INSIDE; }
				dz = -dz;
				z -= z0;
			}
		}
		fixed16_16 zDx = mul16(z, dx);
		fixed16_16 xDz = mul16(x, dz);
		if (xDz == zDx)
		{
			return PS_ON_LINE;
		}
		return (xDz > zDx) ? PS_INSIDE : PS_OUTSIDE;
	}

	// The original DF algorithm.
	JBool sector_pointInsideDF(RSectorV2* sector, fixed16_16 x, fixed16_16 z)
	{
		const fixed16_16 xFrac = fract16(x);
		const fixed16_16 zFrac = fract16(z);
		const s32 xInt = floor16(x);
		const s32 zInt = floor16(z);
		const s32 wallCount = sector->wallCount;

		RWallV2* wall = sector->walls;
		RWallV2* last = &sector->walls[wallCount - 1];
		vec2_fixed* w1 = last->w1;
		vec2_fixed* w0 = last->w0;
		fixed16_16 dzLast = w1->z - w0->z;
		s32 crossings = 0;

		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			w0 = wall->w0;
			w1 = wall->w1;

			fixed16_16 x0 = w0->x;
			fixed16_16 x1 = w1->x;
			fixed16_16 z0 = w0->z;
			fixed16_16 z1 = w1->z;
			fixed16_16 dz = z1 - z0;

			if (dz != 0)
			{
				if (z == z0 && x == x0)
				{
					TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on a vertex of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
					return JTRUE;
				}
				else if (z != z0)
				{
					if (z != z1)
					{
						PointSegSide side = lineSegmentSideV2(x, z, x0, z0, x1, z1);
						if (side == PS_OUTSIDE)
						{
							crossings++;
						}
						else if (side == PS_ON_LINE)
						{
							TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on wall of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
							return JTRUE;
						}
					}
					dzLast = dz;
				}
				else if (x != x0)
				{
					if (x < x0)
					{
						fixed16_16 dzSignMatches = dz ^ dzLast;	// dzSignMatches >= 0 if dz and dz0 have the same sign.
						if (dzSignMatches >= 0 || dzLast == 0)  // the signs match OR dz or dz0 are positive OR dz0 EQUALS 0.
						{
							crossings++;
						}
					}
					dzLast = dz;
				}
			}
			else if (lineSegmentSideV2(x, z, x0, z0, x1, z1) == PS_ON_LINE)
			{
				TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on wall of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
				return JTRUE;
			}
		}

		return (crossings & 1) ? JTRUE : JFALSE;
	}

	// Uses the "Winding Number" test for a point in polygon.
	// The point is considered inside if the winding number is greater than 0.
	// Note that this is different than DF's "crossing" algorithm.
	bool sector_pointInside(RSectorV2* sector, fixed16_16 x, fixed16_16 z)
	{
		RWallV2* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		s32 wn = 0;

		const Vec2f point = { fixed16ToFloat(x), fixed16ToFloat(z) };
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			vec2_fixed* w1 = wall->w0;
			vec2_fixed* w0 = wall->w1;

			Vec2f p0 = { fixed16ToFloat(w0->x), fixed16ToFloat(w0->z) };
			Vec2f p1 = { fixed16ToFloat(w1->x), fixed16ToFloat(w1->z) };

			if (p0.z <= z)
			{
				// Upward crossing, if the point is left of the edge than it intersects.
				if (p1.z > z && isLeft(p0, p1, point) > 0)
				{
					wn++;
				}
			}
			else
			{
				// Downward crossing, if point is right of the edge it intersects.
				if (p1.z <= z && isLeft(p0, p1, point) < 0)
				{
					wn--;
				}
			}
		}

		// The point is only outside if the winding number is less than or equal to 0.
		return wn > 0;
	}

	JBool sector_rotatingWallCollidesWithPlayer(RWallV2* wall, fixed16_16 cosAngle, fixed16_16 sinAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
#if 0
		fixed16_16 objSide0;
		SecObjectV2* player = s_playerObject;
		sector_objOverlapsWall(wall, player, &objSide0);

		fixed16_16 localX = wall->worldPos0.x - centerX;
		fixed16_16 localZ = wall->worldPos0.z - centerZ;

		fixed16_16 dirX = wall->wallDir.x;
		fixed16_16 dirZ = wall->wallDir.z;
		fixed16_16 prevX = wall->w0->x;
		fixed16_16 prevZ = wall->w0->z;

		wall->w0->x = mul16(localX, cosAngle) - mul16(localZ, sinAngle) + centerX;
		wall->w0->z = mul16(localX, sinAngle) + mul16(localZ, cosAngle) + centerZ;

		vec2_fixed* w0 = wall->w0;
		vec2_fixed* w1 = wall->w1;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);

		s32 objSide1;
		JBool overlap1 = sector_objOverlapsWall(wall, player, &objSide1);

		// Restore
		wall->wallDir.x = dirX;
		wall->wallDir.z = dirZ;
		wall->w0->x = prevX;
		wall->w0->z = prevZ;

		// Test the results.
		if (overlap1 || ((objSide0 & objSide1) && (objSide0 != objSide1))) { return JTRUE; }
#endif
		return JFALSE;
	}

	void sector_removeCorpses(RSectorV2* sector)
	{
#if 0
		SecObject* obj = nullptr;
		s32 objectCount = sector->objectCount;

		s32 freeCount = 0;
		SecObject* freeList[128];

		for (s32 i = 0, idx = 0; i < objectCount && idx < sector->objectCapacity; idx++)
		{
			SecObject* obj = sector->objectList[idx];
			if (obj)
			{
				i++;

				JBool canRemove = (obj->entityFlags & ETFLAG_CORPSE) != 0;
				canRemove |= ((obj->entityFlags & ETFLAG_PICKUP) && !(obj->flags & OBJ_FLAG_MISSION));

				const u32 projType = (obj->projectileLogic) ? ((TFE_DarkForces::ProjectileLogic*)obj->projectileLogic)->type : (0);
				const JBool isLandMine = projType == PROJ_LAND_MINE || projType == PROJ_LAND_MINE_PROX || projType == PROJ_LAND_MINE_PLACED;
				canRemove |= ((obj->entityFlags & ETFLAG_PROJECTILE) && isLandMine);

				if (canRemove && freeCount < 128)
				{
					freeList[freeCount++] = obj;
				}
			}
		}

		for (s32 i = 0; i < freeCount; i++)
		{
			freeObject(freeList[i]);
		}
#endif
	}

	// Returns JTRUE if the walls can rotate.
	JBool sector_canRotateWalls(RSectorV2* sector, angle14_32 angle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		fixed16_16 cosAngle, sinAngle;
		sinCosFixed(angle, &sinAngle, &cosAngle);

		// Loop through the walls and determine if the player is in the sector of any affected wall.
		s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		JBool playerCollides = JFALSE;
		for (s32 i = 0; i < sector->wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1V2_WALL_MORPHS)
			{
				if (wall->sector->flags1 & SECV2_FLAGS1_PLAYER)
				{
					playerCollides = JTRUE;
					break;
				}
				RWallV2* mirror = wall->mirrorWall;
				if (mirror && (mirror->flags1 & WF1V2_WALL_MORPHS))
				{
					if (mirror->sector->flags1 & SECV2_FLAGS1_PLAYER)
					{
						playerCollides = JTRUE;
						break;
					}
				}
			}
		}

		if (playerCollides)	// really this says that the player *might* collide and then gets definite here.
		{
			playerCollides = JFALSE;
			wallCount = sector->wallCount;
			wall = sector->walls;

			for (s32 i = 0; i < wallCount; i++, wall++)
			{
				if (wall->flags1 & WF1V2_WALL_MORPHS)
				{
					playerCollides |= sector_rotatingWallCollidesWithPlayer(wall, cosAngle, sinAngle, centerX, centerZ);
					RWallV2* mirror = wall->mirrorWall;
					if (mirror && (mirror->flags1 & WF1V2_WALL_MORPHS))
					{
						playerCollides |= sector_rotatingWallCollidesWithPlayer(mirror, cosAngle, sinAngle, centerX, centerZ);
					}
				}
			}
		}
		return playerCollides ? JFALSE : JTRUE;
	}

	void sector_rotateWall(RWallV2* wall, fixed16_16 cosAngle, fixed16_16 sinAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		fixed16_16 x0 = wall->worldPos0.x - centerX;
		fixed16_16 z0 = wall->worldPos0.z - centerZ;
		wall->w0->x = mul16(x0, cosAngle) - mul16(z0, sinAngle) + centerX;
		wall->w0->z = mul16(x0, sinAngle) + mul16(z0, cosAngle) + centerZ;

		vec2_fixed* w1 = wall->w1;
		vec2_fixed* w0 = wall->w0;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		wall->length = computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);
		wall->angle = vec2ToAngle(dx, dz);

		RSectorV2* sector = wall->sector;
#if 0
		if (sector->flags1 & SECV2_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}
#endif

		RWallV2* prevWall;
		if (wall->id == 0)
		{
			prevWall = wall + (sector->wallCount - 1);
		}
		else
		{
			prevWall = wall - 1;
		}

		w0 = prevWall->w0;
		w1 = prevWall->w1;
		dx = w1->x - w0->x;
		dz = w1->z - w0->z;
		prevWall->length = computeDirAndLength(dx, dz, &prevWall->wallDir.x, &prevWall->wallDir.z);
		prevWall->angle = vec2ToAngle(dx, dz);

		sector = prevWall->sector;
#if 0
		if (sector->flags1 & SECV2_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}
#endif
	}

	void sector_rotateWalls(RSectorV2* sector, fixed16_16 centerX, fixed16_16 centerZ, angle14_32 angle, u32 rotateFlags)
	{
		s32 cosAngle, sinAngle;
		sinCosFixed(angle, &sinAngle, &cosAngle);

		sector->dirtyFlags |= SDFV2_WALL_SHAPE;
		// TODO: (TFE) Handle rotateFlags for floor and ceiling texture rotation.

		s32 wallCount = sector->wallCount;
		RWallV2* wall = sector->walls;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1V2_WALL_MORPHS)
			{
				sector_rotateWall(wall, cosAngle, sinAngle, centerX, centerZ);

				RWallV2* mirror = wall->mirrorWall;
				if (mirror && (mirror->flags1 & WF1V2_WALL_MORPHS))
				{
					mirror->sector->dirtyFlags |= SDFV2_WALL_SHAPE;
					sector_rotateWall(mirror, cosAngle, sinAngle, centerX, centerZ);
				}
			}
		}
		sector_computeBounds(sector);
		sector->dirtyFlags |= SDFV2_WALL_SHAPE;
	}

	void sector_rotateObj(SecObjectV2* obj, angle14_32 deltaAngle, fixed16_16 cosdAngle, fixed16_16 sindAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
#if 0
		if (obj->flags & OBJ_FLAG_MOVABLE)
		{
			if (obj->entityFlags & ETFLAG_PLAYER)
			{
				s_playerYaw -= deltaAngle;
			}
			else
			{
				obj->yaw -= deltaAngle;
			}

			fixed16_16 relX = obj->posWS.x - centerX;
			fixed16_16 relZ = obj->posWS.z - centerZ;

			fixed16_16 offsetY = 0;
			fixed16_16 offsetX = mul16(relX, cosdAngle) - mul16(relZ, sindAngle) + centerX - obj->posWS.x;
			fixed16_16 offsetZ = mul16(relX, sindAngle) + mul16(relZ, cosdAngle) + centerZ - obj->posWS.z;

			fixed16_16 height = obj->worldHeight + HALF_16;

			CollisionInfo info =
			{
				obj,
				offsetX, offsetY, offsetZ,
				0, COL_INFINITY, height, 0,
				0, 0, 0,	// to be filled in later.
				obj->worldWidth,
			};
			handleCollision(&info);

			// Collision reponse with a single iteration.
			if (info.responseStep && info.wall)
			{
				handleCollisionResponseSimple(info.responseDir.x, info.responseDir.z, &info.offsetX, &info.offsetZ);
				handleCollision(&info);
			}

			RSectorV2* finalSector = sector_which3D(obj->posWS.x, obj->posWS.y, obj->posWS.z);
			if (finalSector)
			{
				// Adds the object to the new sector and removes it from the previous.
				sector_addObject(finalSector, obj);
			}
			if (obj->type == OBJ_TYPE_3D)
			{
				obj3d_computeTransform(obj);
			}
		}
#endif
	}

	void sector_rotateObjects(RSectorV2* sector, angle14_32 deltaAngle, fixed16_16 centerX, fixed16_16 centerZ, u32 flags)
	{
#if 0
		fixed16_16 cosdAngle, sindAngle;
		sinCosFixed(deltaAngle, &sindAngle, &cosdAngle);
		JBool moveCeil = JFALSE;
		JBool moveSecHgt = JFALSE;
		JBool moveFloor = JFALSE;
		if (flags & INF_EFLAG_MOVE_FLOOR)
		{
			moveFloor = JTRUE;
		}
		if (flags & INF_EFLAG_MOVE_SECHT)
		{
			moveSecHgt = JTRUE;
		}
		if (flags & INF_EFLAG_MOVE_CEIL)
		{
			moveCeil = JTRUE;
		}

		SecObject** objList = sector->objectList;
		s32 objCount = sector->objectCount;
		for (s32 i = 0; i < objCount; i++, objList++)
		{
			SecObject* obj = *objList;
			while (!obj)
			{
				objList++;
				obj = *objList;
			}
			// The first 3 conditionals can be collapsed since the resulting values are the same.
			if ((moveFloor && obj->posWS.y == sector->floorHeight) ||
				(moveSecHgt && sector->secHeight && sector->floorHeight + sector->secHeight == obj->posWS.y) ||
				(moveCeil && sector->ceilingHeight == obj->posWS.y))
			{
				sector_rotateObj(obj, deltaAngle, cosdAngle, sindAngle, centerX, centerZ);
			}
		}
#endif
	}

	//////////////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////////////
	void sector_computeWallDirAndLength(RWallV2* wall)
	{
		vec2_fixed* w0 = wall->w0;
		vec2_fixed* w1 = wall->w1;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		wall->length = computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);
		wall->angle = vec2ToAngle(dx, dz);
	}

	void sector_moveWallVertex(RWallV2* wall, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		// Offset vertex 0.
		wall->w0->x += offsetX;
		wall->w0->z += offsetZ;
		// Update the wall direction and length.
		sector_computeWallDirAndLength(wall);

		// Set the appropriate game value if the player is inside the sector.
		RSectorV2* sector = wall->sector;
#if 0
		if (sector->flags1 & SECV2_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}
#endif

		// Update the previous wall, since it would have changed as well.
		RWallV2* prevWall = nullptr;
		if (wall->id == 0)
		{
			s32 last = sector->wallCount - 1;
			prevWall = &sector->walls[last];
		}
		else
		{
			prevWall = wall - 1;
		}
		// Compute the wall direction and length of the previous wall.
		sector_computeWallDirAndLength(prevWall);

		// Set the appropriate game value if the player is inside the sector.
		sector = prevWall->sector;
#if 0
		if (sector->flags1 & SECV2_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}
#endif
	}

	// returns 0 if the object does NOT overlap, otherwise non-zero.
	// objSide: 0 = no overlap, -1/+1 front or behind.
	JBool sector_objOverlapsWall(RWallV2* wall, SecObjectV2* obj, s32* objSide)
	{
#if 0
		*objSide = 0;
		RSectorV2* next = wall->nextSector;
		if (next)
		{
			RSectorV2* sector = wall->sector;
			if (sector->floorHeight >= obj->posWS.y)
			{
				fixed16_16 objTop = obj->posWS.y - obj->worldHeight;
				if (sector->ceilingHeight < objTop)
				{
					return JFALSE;
				}
			}
		}

		const fixed16_16 threeQuartWidth = obj->worldWidth/2 + obj->worldWidth/4;

		vec2_fixed* w0  = wall->w0;
		// Given a 2D direction: Dx, Dz; the perpendicular (normal) = -Dz, Dx
		fixed16_16 nrmX = wall->wallDir.z;
		fixed16_16 nrmZ = wall->wallDir.x;
		fixed16_16 len  = wall->length;
		fixed16_16 dx   = obj->posWS.x - w0->x;
		fixed16_16 dz   = obj->posWS.z - w0->z;

		fixed16_16 proj = mul16(dx, nrmZ) + mul16(dz, nrmX);
		fixed16_16 minSepDist = threeQuartWidth * 2 + len;	// 1.5 * width + length
		if (u32(proj + threeQuartWidth) <= u32(minSepDist))
		{
			fixed16_16 perpDist = mul16(dx, nrmX) - mul16(dz, nrmZ);
			s32 side = (perpDist >= 0) ? 1 : -1;

			*objSide = side;
			if (abs(perpDist) <= threeQuartWidth)
			{
				return JTRUE;
			}
		}
#endif
		return JFALSE;
	}

	// returns 0 if the wall is free to move, else non-zero.
	JBool sector_canWallMove(RWallV2* wall, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
#if 0
		// Test the initial position, the code assumes there is no collision at this point.
		s32 objSide0 = 0;
		sector_objOverlapsWall(wall, s_playerObject, &objSide0);

		vec2_fixed* w0 = wall->w0;
		fixed16_16 x0 = w0->x;
		fixed16_16 z0 = w0->z;
		w0->x += offsetX;
		w0->z += offsetZ;

		// Then move the wall and test the new position.
		s32 objSide1 = 0;
		JBool col = sector_objOverlapsWall(wall, s_playerObject, &objSide1);

		// Restore the wall.
		w0->x = x0;
		w0->z = z0;

		if (col || ((objSide0 & objSide1) && (objSide0 != objSide1))) { return JTRUE; }
#endif
		return JFALSE;
	}

	void sector_getFloorAndCeilHeight(RSectorV2* sector, fixed16_16* floorHeight, fixed16_16* ceilHeight)
	{
		*floorHeight = sector->floorHeight;
		*ceilHeight = sector->ceilingHeight;

		if (sector->flags1 & SECV2_FLAGS1_PIT)
		{
			*floorHeight += SECV2_SKY_HEIGHT;
		}
		if (sector->flags1 & SECV2_FLAGS1_EXTERIOR)
		{
			*ceilHeight -= SECV2_SKY_HEIGHT;
		}
	}

	void sector_getObjFloorAndCeilHeight(RSectorV2* sector, fixed16_16 y, fixed16_16* floorHeight, fixed16_16* ceilHeight)
	{
		fixed16_16 secHeight = sector->secHeight;
		fixed16_16 bottom = y - FIXED(2);// COL_SEC_HEIGHT_OFFSET;
		if (secHeight < 0)
		{
			fixed16_16 height = sector->floorHeight + sector->secHeight;
			if (bottom <= height)
			{
				*floorHeight = height;
				*ceilHeight = sector->ceilingHeight;
			}
			else
			{
				*floorHeight = sector->floorHeight;
				*ceilHeight = height;
			}
		}
		else // secHeight >= 0
		{
			*floorHeight = sector->floorHeight + sector->secHeight;
			*ceilHeight = sector->ceilingHeight;
		}
		if (sector->flags1 & SECV2_FLAGS1_PIT)
		{
			*floorHeight += SECV2_SKY_HEIGHT;
		}
		if (sector->flags1 & SECV2_FLAGS1_EXTERIOR)
		{
			*ceilHeight -= SECV2_SKY_HEIGHT;
		}
	}

	void sector_moveObject(SecObjectV2* obj, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
#if 0
		if (obj->flags & OBJ_FLAG_MOVABLE)
		{
			s32 height = obj->worldHeight + HALF_16;
			CollisionInfo info =
			{
				obj,
				offsetX, 0, offsetZ,
				0, COL_INFINITY, height, 0,
				0, 0, 0,	// to be filled in later.
				obj->worldWidth,
			};
			handleCollision(&info);

			if (info.responseStep && info.wall)
			{
				handleCollisionResponseSimple(info.responseDir.x, info.responseDir.z, &info.offsetX, &info.offsetZ);
				handleCollision(&info);
			}

			RSectorV2* newSector = sector_which3D(obj->posWS.x, obj->posWS.y, obj->posWS.z);
			if (newSector)
			{
				if (newSector != obj->sector && (obj->entityFlags & (ETFLAG_CORPSE | ETFLAG_PICKUP)))
				{
					obj->posWS.y = newSector->colSecHeight;
				}
				sector_addObject(newSector, obj);
			}
		}
#endif
	}
		
	void sector_moveObjects(RSectorV2* sector, u32 flags, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
#if 0
		JBool ceiling  = (flags & INF_EFLAG_MOVE_CEIL)!=0  ? JTRUE : JFALSE;
		JBool offset   = (flags & INF_EFLAG_MOVE_SECHT)!=0 ? JTRUE : JFALSE;
		JBool floor    = (flags & INF_EFLAG_MOVE_FLOOR)!=0 ? JTRUE : JFALSE;

		SecObject** objList = sector->objectList;
		for (s32 i = 0, idx = 0; i < sector->objectCount && idx < sector->objectCapacity; idx++)
		{
			SecObject* obj = objList[idx];
			if (obj)
			{
				i++;
				if ((obj->flags & OBJ_FLAG_MOVABLE) && (obj->entityFlags != ETFLAG_PLAYER))
				{
					if ((floor   && obj->posWS.y == sector->floorHeight) ||
						(offset  && sector->secHeight && sector->floorHeight + sector->secHeight == obj->posWS.y) ||
						(ceiling && obj->posWS.y == sector->ceilingHeight))
					{
						sector_moveObject(obj, offsetX, offsetZ);
					}
				}
			}
		}
#endif
	}
		
	// Tests if a point (p2) is to the left, on or right of an infinite line (p0 -> p1).
	// Return: >0 p2 is on the left of the line.
	//         =0 p2 is on the line.
	//         <0 p2 is on the right of the line.
	f32 isLeft(Vec2f p0, Vec2f p1, Vec2f p2)
	{
		return (p1.x - p0.x) * (p2.z - p0.z) - (p2.x - p0.x) * (p1.z - p0.z);
	}
#endif
} // namespace TFE_Jedi