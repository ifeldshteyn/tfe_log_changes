#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Editor
// A system built to view and edit Dark Forces data files.
// The viewing aspect needs to be put in place at the beginning
// in order to properly test elements in isolation without having
// to "play" the game as intended.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "entity.h"
#include "groups.h"
#include <TFE_Editor/EditorAsset/editorAsset.h>
#include <TFE_Editor/EditorAsset/editorTexture.h>
#include <TFE_Editor/editorProject.h>
#include <TFE_Polygon/polygon.h>
#include <TFE_Editor/history.h>

namespace LevelEditor
{
	enum LevelEditorFormat
	{
		LEF_MinVersion = 1,
		LEF_EntityV1   = 2,
		LEF_EntityV2   = 3,
		LEF_EntityList = 4,
		LEF_EntityV3   = 5,
		LEF_EntityV4   = 6,
		LEF_InfV1      = 7,
		LEF_Groups     = 8,
		LEF_CurVersion = 8,
	};

	enum LevelEditMode
	{
		LEDIT_DRAW = 1,
		//LEDIT_SMART,	// vertex + wall + height "smart" edit.
		LEDIT_VERTEX,	// vertex only
		LEDIT_WALL,		// wall only in 2D, wall + floor/ceiling in 3D
		LEDIT_SECTOR,
		LEDIT_ENTITY
	};
		
	enum DrawMode
	{
		DMODE_RECT = 0,
		DMODE_SHAPE,
		DMODE_RECT_VERT,
		DMODE_SHAPE_VERT,
		DMODE_COUNT
	};

	enum WallPart
	{
		WP_MID = 0,
		WP_TOP,
		WP_BOT,
		WP_SIGN,
		WP_COUNT
	};

	enum HitPart
	{
		HP_MID = 0,
		HP_TOP,
		HP_BOT,
		HP_SIGN,
		HP_FLOOR,
		HP_CEIL,
		HP_COUNT,
		HP_NONE = HP_COUNT
	};

	enum RayConst
	{
		LAYER_ANY = -256,
	};

	struct LevelTextureAsset
	{
		std::string name;
		TFE_Editor::AssetHandle handle = NULL_ASSET;
	};

	struct LevelTexture
	{
		s32 texIndex = -1;
		Vec2f offset = { 0 };
	};

	struct EditorWall
	{
		LevelTexture tex[WP_COUNT] = {};

		s32 idx[2] = { 0 };
		s32 adjoinId = -1;
		s32 mirrorId = -1;

		u32 flags[3] = { 0 };
		s32 wallLight = 0;
	};

	struct EditorSector
	{
		s32 id = 0;
		u32 groupId = 0;
		u32 groupIndex = 0;
		std::string name;	// may be empty.

		LevelTexture floorTex = {};
		LevelTexture ceilTex = {};

		f32 floorHeight = 0.0f;
		f32 ceilHeight = 0.0f;
		f32 secHeight = 0.0f;

		u32 ambient = 0;
		u32 flags[3] = { 0 };

		// Geometry
		std::vector<Vec2f> vtx;
		std::vector<EditorWall> walls;
		std::vector<EditorObject> obj;

		// Bounds
		Vec3f bounds[2];
		s32 layer = 0;

		// Polygon
		Polygon poly;

		// For searches.
		u32 searchKey = 0;
	};

	typedef std::vector<EditorSector*> SectorList;

	struct EditorLevel
	{
		std::string name;
		std::string slot;
		std::string palette;
		TFE_Editor::FeatureSet featureSet = TFE_Editor::FSET_VANILLA;

		// Sky Parallax.
		Vec2f parallax = { 1024.0f, 1024.0f };

		// Texture data.
		std::vector<LevelTextureAsset> textures;

		// Sector data.
		std::vector<EditorSector> sectors;

		// Entity data.
		std::vector<Entity> entities;

		// Level bounds.
		Vec3f bounds[2] = { 0 };
		s32 layerRange[2] = { 0 };
	};

	// Collision
	struct Ray
	{
		Vec3f origin;
		Vec3f dir;
		f32 maxDist;
		s32 layer;
	};

	struct RayHitInfo
	{
		// What was hit.
		s32 hitSectorId;
		s32 hitWallId;
		s32 hitObjId;
		HitPart hitPart;

		// Actual hit position.
		Vec3f hitPos;
		f32 dist;
	};

	struct StartPoint
	{
		Vec3f pos;
		f32 yaw;
		f32 pitch;
		EditorSector* sector;
	};

	bool loadLevelFromAsset(TFE_Editor::Asset* asset);
	TFE_Editor::AssetHandle loadTexture(const char* bmTextureName);
	TFE_Editor::AssetHandle loadPalette(const char* paletteName);
	TFE_Editor::AssetHandle loadColormap(const char* colormapName);
	
	bool saveLevel();
	bool exportLevel(const char* path, const char* name, const StartPoint* start);
	void sectorToPolygon(EditorSector* sector);
	void polygonToSector(EditorSector* sector);

	s32 addEntityToLevel(const Entity* newEntity);

	TFE_Editor::EditorTexture* getTexture(s32 index);
	s32 getTextureIndex(const char* name);
		
	f32 getWallLength(const EditorSector* sector, const EditorWall* wall);
	bool getSignExtents(const EditorSector* sector, const EditorWall* wall, Vec2f ext[2]);
	void centerSignOnSurface(const EditorSector* sector, EditorWall* wall);

	void level_createSnapshot(TFE_Editor::SnapshotBuffer* buffer);
	void level_unpackSnapshot(s32 id, u32 size, void* data);

	// Spatial Queries
	s32  findSectorByName(const char* name, s32 excludeId = -1);
	s32  findSector2d(s32 layer, const Vec2f* pos);
	bool traceRay(const Ray* ray, RayHitInfo* hitInfo, bool flipFaces, bool canHitSigns, bool canHitObjects = false);
	// Get all sectors that have bounds that contain the point.
	bool getOverlappingSectorsPt(const Vec3f* pos, s32 curLayer, SectorList* result, f32 padding = 0.0f);
	// Get all sectors that have bounds that overlap the input bounds.
	bool getOverlappingSectorsBounds(const Vec3f bounds[2], SectorList* result);
	// Helpers
	bool aabbOverlap3d(const Vec3f* aabb0, const Vec3f* aabb1);
	bool aabbOverlap2d(const Vec3f* aabb0, const Vec3f* aabb1);
	bool pointInsideAABB3d(const Vec3f* aabb, const Vec3f* pt);
	bool pointInsideAABB2d(const Vec3f* aabb, const Vec3f* pt);
	bool isPointInsideSector2d(EditorSector* sector, Vec2f pos, s32 layer);
	bool isPointInsideSector3d(EditorSector* sector, Vec3f pos, s32 layer);
	s32 findClosestWallInSector(const EditorSector* sector, const Vec2f* pos, f32 maxDistSq, f32* minDistToWallSq);

	// Groups
	inline Group* sector_getGroup(EditorSector* sector)
	{
		Group* group = groups_getByIndex(sector->groupIndex);
		if (!group || group->id != sector->groupId)
		{
			group = groups_getById(sector->groupId);
			sector->groupIndex = group->index;
		}
		assert(group);
		return group;
	}

	inline bool sector_isHidden(EditorSector* sector)
	{
		return (sector_getGroup(sector)->flags & GRP_HIDDEN) != 0;
	}

	inline bool sector_isLocked(EditorSector* sector)
	{
		return (sector_getGroup(sector)->flags & GRP_LOCKED) != 0;
	}

	inline bool sector_isInteractable(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		return !(group->flags & GRP_HIDDEN) && !(group->flags & GRP_LOCKED);
	}

	inline bool sector_excludeFromExport(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		return (group->flags & GRP_EXCLUDE) != 0;
	}

	inline u32 sector_getGroupColor(EditorSector* sector)
	{
		const Group* group = sector_getGroup(sector);
		const u32 r = u32(group->color.x * 255.0f);
		const u32 g = u32(group->color.y * 255.0f);
		const u32 b = u32(group->color.z * 255.0f);
		return (0x80 << 24) | (b << 16) | (g << 8) | (r);
	}

	extern std::vector<u8> s_fileData;
}
