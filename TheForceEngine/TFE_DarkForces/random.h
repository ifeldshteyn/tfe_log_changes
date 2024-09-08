#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces Random number generator
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_Asset/dfKeywords.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include "logic.h"
#include "time.h"

namespace TFE_DarkForces
{
	// Generate a random value between [0, value]
	s32 random(s32 value);
	s32 random_next();
	void random_serialize(vpFile* stream);

	void random_seed(u32 seed);
}  // namespace TFE_DarkForces