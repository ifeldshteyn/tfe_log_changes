#pragma once
//////////////////////////////////////////////////////////////////////
// Wall
// Dark Forces Derived Renderer - Wall functions
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_DarkForces/time.h>

// was BM_SubHeader
#pragma pack(push)
#pragma pack(1)
struct TextureDataV2
{
	u16 width;		// if = 1 then multiple BM in the file
	u16 height;		// EXCEPT if SizeY also = 1, in which case
					// it is a 1x1 BM
};
#pragma pack(pop)

namespace TFE_Jedi
{
	// TODO
}
