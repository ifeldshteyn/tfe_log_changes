#pragma once
#include <TFE_System/types.h>
#include <TFE_Editor/history.h>
#include <TFE_Editor/LevelEditor/selection.h>
#include <TFE_Editor/LevelEditor/levelEditorData.h>

namespace LevelEditor
{
	enum LevCommandName
	{
		LName_MoveVertex = 0,
		LName_SetVertex,
		LName_MoveWall,
		LName_MoveFlat,
		LName_InsertVertex,
		LName_DeleteVertex,
		LName_DeleteWall,
		LName_DeleteSector,
		LName_CreateSectorFromRect,
		LName_CreateSectorFromShape,
		LName_MoveTexture,
		LName_SetTexture,
		LName_CopyTexture,
		LName_ClearTexture,
		LName_Autoalign,
		LName_Count
	};

	void levHistory_init();
	void levHistory_destroy();

	void levHistory_clear();
	void levHistory_createSnapshot(const char* name);

	void levHistory_undo();
	void levHistory_redo();

	void captureEditState();
	void restoreEditState();

	// Commands
	void cmd_addMoveVertices(s32 count, const FeatureId* vertices, Vec2f delta, LevCommandName name = LName_MoveVertex);
	void cmd_addSetVertex(FeatureId vertex, Vec2f pos);
	void cmd_addMoveFlats(s32 count, const FeatureId* flats, f32 delta);
	void cmd_addInsertVertex(s32 sectorIndex, s32 wallIndex, Vec2f newPos);
	void cmd_addDeleteVertex(s32 sectorIndex, s32 vertexIndex, LevCommandName name = LName_DeleteVertex);
	void cmd_addDeleteSector(s32 sectorId);
	void cmd_addCreateSectorFromRect(const f32* heights, const Vec2f* corners);
	void cmd_addCreateSectorFromShape(const f32* heights, s32 vertexCount, const Vec2f* vtx);
	void cmd_addMoveTexture(s32 count, FeatureId* features, Vec2f delta);
	void cmd_addSetTexture(s32 count, FeatureId* features, s32 texId, Vec2f* offset);
	void cmd_addClearTexture(s32 count, FeatureId* features);
	void cmd_addAutoAlign(s32 sectorId, s32 featureIndex, HitPart part);
}
