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
	void kellDragon_exit();
	void kellDragon_precache();
	Logic* kellDragon_setup(SecObject* obj, LogicSetupFunc* setupFunc);
	void kellDragon_serialize(Logic*& logic, SecObject* obj, vpFile* stream);
}  // namespace TFE_DarkForces