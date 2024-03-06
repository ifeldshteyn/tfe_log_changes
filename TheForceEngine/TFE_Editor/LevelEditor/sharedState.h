#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_RenderShared/lineDraw3d.h>
#include "levelEditorData.h"
#include <vector>

namespace LevelEditor
{
	enum GridFlags
	{
		GFLAG_NONE = 0,
		GFLAG_OVER = FLAG_BIT(0),
	};
	struct Feature
	{
		EditorSector* sector = nullptr;
		EditorSector* prevSector = nullptr;
		s32 featureIndex = -1;
		bool isObject = false;
		HitPart part = HP_NONE;
	};
	struct ExtrudePlane
	{
		Vec3f origin;
		Vec3f S;
		Vec3f T;
		Vec3f N;

		Vec2f ext;
		EditorSector* sector;
		EditorWall* wall;
	};
		
	extern EditorLevel s_level;
	extern TFE_Editor::AssetList s_levelTextureList;
	extern LevelEditMode s_editMode;
	extern u32 s_editFlags;
	extern u32 s_lwinOpen;
	extern s32 s_curLayer;

	extern Feature s_featureHovered;
	extern Feature s_featureCur;
	extern Feature s_featureCurWall;

	extern Vec3f s_hoveredVtxPos;
	extern Vec3f s_curVtxPos;

	extern s32 s_selectedTexture;
	extern s32 s_selectedEntity;
	extern u32 s_gridFlags;
	extern f32 s_gridHeight;

	// Camera
	extern Camera3d s_camera;
	extern Vec3f s_viewDir;
	extern Vec3f s_viewRight;
	extern Vec3f s_cursor3d;

	// Drawing
	extern ExtrudePlane s_extrudePlane;

	// Search
	extern u32 s_searchKey;
}
