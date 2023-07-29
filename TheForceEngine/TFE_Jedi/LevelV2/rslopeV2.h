#pragma once
//////////////////////////////////////////////////////////////////////
// Wall
// Dark Forces Derived Renderer - Wall functions
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/core_math.h>

struct RWallV2;
struct RSectorV2;

using namespace TFE_Jedi;

struct SlopedPlane
{
	RWallV2* hingeWall;	// the hinge edge that defines the slope.
	RSectorV2* sector;
	f32 planeAngle;
	vec2_float heightBounds;
	vec2_float normal;	// assume Y is +1 or -1 depending on floor or ceiling.
	vec3_float p0, p1;	// 
	f32 p0DotNrm;		// p0.normal
	vec3_float s, t;	// direction from p1 -> p0; normal projected onto plane.
	f32 sDotP1, tDotP1;
};

namespace TFE_Jedi
{
	inline f32 slope_getHeightAtXZ(SlopedPlane* slope, vec2_float pos)
	{
		return slope->p0DotNrm - pos.x*slope->normal.x - pos.z*slope->normal.z;
	}
}
