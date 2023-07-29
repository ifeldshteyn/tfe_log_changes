#pragma once
//////////////////////////////////////////////////////////////////////
// Sector
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_Jedi/Math/core_math.h>
#include "rwallV2.h"

using namespace TFE_Jedi;

struct Allocator;
struct TextureDataV2;
struct LevelDataV2;

struct SecObjectV2;
struct SlopedPlane;

// Sector flags change position + new flags.
// A few DF flags have been removed.
enum SectorFlags1v2
{
	SECV2_FLAGS1_EXTERIOR      = FLAG_BIT(0),
	SECV2_FLAGS1_PIT           = FLAG_BIT(1),
	SECV2_FLAGS1_EXT_ADJ       = FLAG_BIT(2),
	SECV2_FLAGS1_EXT_FLOOR_ADJ = FLAG_BIT(3),
	SECV2_FLAGS1_NOWALL_DRAW   = FLAG_BIT(4),
	SECV2_FLAGS1_NOSLIDE       = FLAG_BIT(5),	// Do not slide on slope.
	SECV2_FLAGS1_FLOORVELONLY  = FLAG_BIT(6),	// Sector velocity only applies to the floor.
	SECV2_FLAGS1_LIQUID        = FLAG_BIT(7),	// Sector is a liquid (water).
	SECV2_FLAGS1_DOOR          = FLAG_BIT(8),   // Automatic door.
	SECV2_FLAGS1_REV		   = FLAG_BIT(9),	// Automatic door, opposite direction.
	SECV2_FLAGS1_SUNLIT        = FLAG_BIT(10),  // Use the sun direction for lighting.
	SECV2_FLAGS1_SWIRLTEX      = FLAG_BIT(11),  // Swirling floor texture.
	SECV2_FLAGS1_SECRET        = FLAG_BIT(12),
	SECV2_FLAGS1_REVERB_LOW	   = FLAG_BIT(13),
	SECV2_FLAGS1_REVERB_MED	   = FLAG_BIT(14),
	SECV2_FLAGS1_REVERB_HIGH   = FLAG_BIT(15),
	SECV2_FLAGS1_UNUSED_1	   = FLAG_BIT(16),
	SECV2_FLAGS1_UNUSED_2      = FLAG_BIT(17),
	SECV2_FLAGS1_LOW_DMG       = FLAG_BIT(18),
	SECV2_FLAGS1_HIGH_DMG      = FLAG_BIT(19),
	SECV2_FLAGS1_DEADLY        = FLAG_BIT(20),
	SECV2_FLAGS1_FLOOR_LOW_DMG = FLAG_BIT(21),
	SECV2_FLAGS1_FLOOR_HIGH_DMG= FLAG_BIT(22),
	SECV2_FLAGS1_FLOOR_DEADLY  = FLAG_BIT(23),
	SECV2_FLAGS1_TERM_ACTOR    = FLAG_BIT(24),
	SECV2_FLAGS1_SECRET_TAG    = FLAG_BIT(25),
	SECV2_FLAGS1_FLOOR_ILLUM   = FLAG_BIT(26),	// The floor is fullbright.
	SECV2_FLAGS1_RAIL_PULL     = FLAG_BIT(27),
	SECV2_FLAGS1_RAIL_LINE     = FLAG_BIT(28),
	SECV2_FLAGS1_NO_MAP        = FLAG_BIT(29),	// Sector is not shown on the map.
	SECV2_FLAGS1_SLOPE_FLOOR   = FLAG_BIT(30),	// Floor is sloped.
	SECV2_FLAGS1_SLOPE_CEIL    = FLAG_BIT(31),	// Ceiling is sloped.
};

// DF didn't use flag2 really, but Outlaws does.
enum SectorFlags2v2
{
	SECV2_FLAGS2_DIRTY              = FLAG_BIT(0),
	// Net stuff...

	SECV2_FLAGS2_3DO                = FLAG_BIT(23),
	SECV2_FLAGS2_VADJOIN            = FLAG_BIT(24),
	SECV2_FLAGS2_BASE_VEL           = FLAG_BIT(25),
	SECV2_FLAGS2_VEL                = FLAG_BIT(26),
	//
	SECV2_FLAGS2_SECRET_NOT_COUNTED = FLAG_BIT(28),
	SECV2_FLAGS2_CONCAVECLOSED      = FLAG_BIT(29),
	SECV2_FLAGS2_CONVEX             = FLAG_BIT(30),
	SECV2_FLAGS2_SUBSECTOR          = FLAG_BIT(31),
};

enum SectorConstantsV2
{
	SECV2_SKY_HEIGHT = FIXED(100),
	SECV2_TEXTURE_COUNT = 4,	// floor, ceiling, floor overlay, ceiling overlay. 
	SECV2_NONCONVEXNONCLOSED = 0,
	SECV2_NONCONVEXCLOSED = 1,
	SECV2_CONVEX = 2,
};

struct RSectorV2
{
	RSectorV2* self;
	s32 index;
	s32 id;
	f32 layer;
	s32 networkId;

	// Characteristics.
	u8 palIndex;
	u8 cmapIndex;
	s32 ambient;
	vec2_float boundsMin;
	vec2_float boundsMax;
	f32 friction;
	f32 gravity;
	f32 elasticity;
	vec2_float* baseVel;
	vec2_float* vel;

	void* actor;	// TODO
	u32   soundId;

	// Textures
	TextureDataV2** textures[SECV2_TEXTURE_COUNT];
	vec2_float texOffset[SECV2_TEXTURE_COUNT];
	f32 texAngle[SECV2_TEXTURE_COUNT];
		
	// Render heights.
	f32 ceilHeight;
	f32 floorHeight;
	SlopedPlane* slopedCeil;
	SlopedPlane* slopedFloor;

	RSectorV2* vAdjoin;

	// Logic
	u32   logicValue;
	void* logic;			// TODO
	s16*  lineOfSight;

	// Vertices.
	s32 vertexCount;		// number of vertices.
	vec2_float* verticesWS;	// world space and view space XZ vertex positions.
	vec2_float* verticesVS;

	// Walls.
	s32 wallCount;			// number of walls.
	RWallV2* walls;			// wall list.
	s32 startWall;
	s32 drawWallCount;

	// Last update/draw.
	u32 logicFrame;
	u32 renderFrame;
	u32 transformFrame;

	// Flags & Layer.
	u32 flags1;				// sector flags.
	u32 flags2;
};

namespace TFE_Jedi
{
	void sector_init(RSectorV2* sector);
	void sector_free(RSectorV2* sector);
	void sector_computeAdjoinsAndHeights(RSectorV2* sector);
	s32 sector_computeConvexity(RSectorV2* sector);
	void sector_computeBounds(RSectorV2* sector, Rect* rect);
	RSectorV2* sector_getById(LevelDataV2* level, s32 id);
	RSectorV2* sector_which3D(LevelDataV2* level, vec3_float pos);
	RSectorV2* sector_which3DFast(RSectorV2* sector, vec3_float pos);
	RSectorV2* sector_which2D(LevelDataV2* level, vec2_float pos);
	RWallV2* sector_findWallIntersect(RSectorV2* sector, vec2_float p0, vec2_float p1);
	RWallV2* sector_nextWallIntersect(RSectorV2* sector);
	void sector_getBestWallIntersect(f32* len, vec2_float* pos);
	bool sector_pointInside(RSectorV2* sector, vec2_float pos, f32* area);
}