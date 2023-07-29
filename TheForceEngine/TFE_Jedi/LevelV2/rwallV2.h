#pragma once
//////////////////////////////////////////////////////////////////////
// Wall
// Dark Forces Derived Renderer - Wall functions
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/core_math.h>

using namespace TFE_Jedi;

struct TextureDataV2;
struct Allocator;
struct RSectorV2;

enum WallTexture
{
	WTEX_TOP = 0,
	WTEX_MIDDLE,
	WTEX_BOTTOM,
	WTEX_SIGN,
	WTEX_COUNT
};

enum AdjoinType
{
	ADJOIN_NONE   = 0,
	ADJOIN_TOP    = FLAG_BIT(0),
	ADJOIN_BOTTOM = FLAG_BIT(1),
	ADJOIN_DOUBLE = FLAG_BIT(2),
	BOTTOM_SPLIT  = FLAG_BIT(3),
};

enum WallFlags1V2
{
	WF1V2_ADJ_MID_TEX          = FLAG_BIT(0),	// the mid texture is rendered even with adjoin (maskwall)
	WF1V2_ILLUM_SIGN           = FLAG_BIT(1),	// render the sign as fullbright.
	WF1V2_FLIP_HORIZ           = FLAG_BIT(2),	// flip texture horizontally.
	WF1V2_TEX_ANCHORED         = FLAG_BIT(3),
	WF1V2_SIGN_ANCHORED        = FLAG_BIT(4),
	WF1V2_TINT				   = FLAG_BIT(5),
	WF1V2_WALL_MORPHS          = FLAG_BIT(6),
	WF1V2_SCROLL_TOP_TEX       = FLAG_BIT(7),
	WF1V2_SCROLL_MID_TEX       = FLAG_BIT(8),
	WF1V2_SCROLL_BOT_TEX       = FLAG_BIT(9),
	WF1V2_SCROLL_SIGN_TEX      = FLAG_BIT(10),
	WF1V2_SOLID_WALL           = FLAG_BIT(11),
	WF1V2_ALWAYS_WALK          = FLAG_BIT(12),
	WF1V2_PLAYER_WALK_ONLY     = FLAG_BIT(13),	// players can walk through but not enemies.
	WF1V2_SHATTER              = FLAG_BIT(14),  // shattering glass.
	WF1V2_CAN_FIRE_THROUGH     = FLAG_BIT(15),	// projectile *can* pass through.
	WF1V2_NO_RAIL              = FLAG_BIT(16),
	WF1V2_HIDE_ON_MAP          = FLAG_BIT(17),
	WF1V2_SECRET               = FLAG_BIT(18),
};

enum WallFlags2V2
{
	WF2V2_DIRTY               = FLAG_BIT(0),
	// Net Stuff

	WF2V2_NO_BULLETHOLES      = FLAG_BIT(26),
	WF2V2_SHATTERED           = FLAG_BIT(27),
	WF2V2_SLOPE_HINGE         = FLAG_BIT(28),
	WF2V2_RAIL                = FLAG_BIT(29),
	WF2V2_RENDERED            = FLAG_BIT(30),
	WF2V2_VISIBLE             = FLAG_BIT(31),
};

struct RWallV2
{
	s32 id;
	s32 srcId;
	s32 networkId;
	s32 index;

	TextureDataV2** texture[WTEX_COUNT];
	vec2_float texOffset[WTEX_COUNT];
	f32 topTexelHeight;
	f32 midTexelHeight;
	f32 botTexelHeight;

	vec2_float wallDir;
	angle14_16 angle;
	f32 length;

	s32 wallLight;
	RSectorV2* adjoin;
	RSectorV2* dadjoin;
	RWallV2* mirror;
	RWallV2* dmirror;
	s32 adjoinType;	// see AdjoinType{}

	RSectorV2* sector;
	vec2_float* w0;
	vec2_float* w1;
	u32 flags1;
	u32 flags2;

	// Logic
	u32 logicValue;
	u32 logicFrame;
	void* logic;	// TODO
	vec2_float worldPos0;

	// Rendering
	vec2_float* v0;
	vec2_float* v1;
	u32 renderFrame;
};

struct CollisionLine2D
{
	vec2_float p0;
	vec2_float p1;
	vec2_float dir;
	f32 len;
};

struct Rect
{
	f32 x, z;
	f32 w, h;
};

namespace TFE_Jedi
{
	void wall_init(RWallV2* wall);
	void wall_free(RWallV2* wall);
	void wall_computeAdjoinType(RWallV2* wall);
	void wall_computeTexelHeights(RWallV2* wall);
	void wall_shatter(RWallV2* wall, bool broadcast);
	bool wall_lineCrosses(CollisionLine2D* line, RWallV2* wall);
	void wall_getLineIntersection(vec2_float* it);
	RSectorV2* wall_getAdjoinAtHeight(RWallV2* wall, f32 height);
	s32 wall_isPassable(RWallV2* wall);
	s32 wall_isRail(RWallV2* wall, void* collide);
}