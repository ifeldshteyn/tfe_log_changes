#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Handles the Phase Two Dark Trooper AI.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_DarkForces/logic.h>
#include "actor.h"

namespace TFE_DarkForces
{
	void phaseThree_exit();
	void phaseThree_precache();
	Logic* phaseThree_setup(SecObject* obj, LogicSetupFunc* setupFunc);
	void phaseThree_serialize(Logic*& logic, SecObject* obj, vpFile* stream);
}  // namespace TFE_DarkForces