#pragma once
//////////////////////////////////////////////////////////////////////
// Level
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_Jedi/Math/core_math.h>

using namespace TFE_Jedi;
struct RSectorV2;

struct LevelDataV2
{
	s32 sectorCount;
	RSectorV2* sectors;
};