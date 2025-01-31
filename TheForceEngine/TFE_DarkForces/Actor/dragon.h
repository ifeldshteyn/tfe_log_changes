#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Handles the Kell Dragon AI.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_DarkForces/logic.h>
#include "actor.h"

namespace TFE_DarkForces
{
	Logic* kellDragon_setup(SecObject* obj, LogicSetupFunc* setupFunc);
}  // namespace TFE_DarkForces