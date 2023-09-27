#include <cstring>

#include "spriteAsset_Jedi.h"
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Asset/assetSystem.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Serialization/serialization.h>
// TODO: dependency on JediRenderer, this should be refactored...
#include <TFE_Jedi/Renderer/rlimits.h>
//
#include <assert.h>
#include <algorithm>
#include <vector>
#include <string>
#include <map>

using namespace TFE_Jedi;

namespace TFE_Sprite_Jedi
{
	typedef std::map<std::string, JediFrame*> FrameMap;
	typedef std::map<std::string, JediWax*> SpriteMap;
	typedef std::vector<JediFrame*> FrameList;
	typedef std::vector<JediWax*> SpriteList;
	typedef std::vector<std::string> NameList;

	static FrameMap   s_frames[POOL_COUNT];
	static SpriteMap  s_sprites[POOL_COUNT];
	static FrameList  s_frameList[POOL_COUNT];
	static SpriteList s_spriteList[POOL_COUNT];
	static NameList   s_frameNames[POOL_COUNT];
	static NameList   s_spriteNames[POOL_COUNT];
	static std::vector<u8> s_buffer;

	JediFrame* getFrame(const char* name, AssetPool pool)
	{
		FrameMap::iterator iFrame = s_frames[pool].find(name);
		if (iFrame != s_frames[pool].end())
		{
			return iFrame->second;
		}

		// It doesn't exist yet, try to load the frame.
		FilePath filePath;
		if (!TFE_Paths::getFilePath(name, &filePath))
		{
			return nullptr;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			return nullptr;
		}
		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();

		const u8* data = s_buffer.data();

		// Determine ahead of time how much we need to allocate.
		const WaxFrame* base_frame = (WaxFrame*)data;
		const WaxCell* base_cell = WAX_CellPtr(data, base_frame);
		const u32 columnSize = base_cell->sizeX * sizeof(u32);

		// This is a "load in place" format in the original code.
		// We are going to allocate new memory and copy the data.
		u8* assetPtr = (u8*)malloc(s_buffer.size() + columnSize);
		JediFrame* asset = (JediFrame*)assetPtr;
		
		memcpy(asset, data, s_buffer.size());

		WaxFrame* frame = asset;
		WaxCell* cell = WAX_CellPtr(asset, frame);

		// After load, the frame data has to be fixed up before rendering.
		// frame sizes remain in fixed point.
		frame->widthWS  = div16(intToFixed16(cell->sizeX), SPRITE_SCALE_FIXED);
		frame->heightWS = div16(intToFixed16(cell->sizeY), SPRITE_SCALE_FIXED);

		const s32 offsetX = -intToFixed16(frame->offsetX);
		const s32 offsetY =  intToFixed16(cell->sizeY) + intToFixed16(frame->offsetY);
		frame->offsetX = div16(offsetX, SPRITE_SCALE_FIXED);
		frame->offsetY = div16(offsetY, SPRITE_SCALE_FIXED);

		if (cell->compressed == 1)
		{
			// Update the column offset, it starts right after the cell data.
			cell->columnOffset = frame->cellOffset + sizeof(WaxCell);
		}
		else
		{
			u32* columns = (u32*)((u8*)asset + s_buffer.size());
			// Local pointer.
			cell->columnOffset = u32((u8*)columns - (u8*)asset);
			// Calculate column offsets.
			for (s32 c = 0; c < cell->sizeX; c++)
			{
				columns[c] = cell->sizeY * c;
			}
		}
		
		s_frames[pool][name] = asset;
		s_frameList[pool].push_back(asset);
		s_frameNames[pool].push_back(name);
		return asset;
	}

	JediFrame* loadFrameFromMemory(const u8* data, size_t size)
	{
		// Determine ahead of time how much we need to allocate.
		const WaxFrame* base_frame = (WaxFrame*)data;
		const WaxCell* base_cell = WAX_CellPtr(data, base_frame);
		const u32 columnSize = base_cell->sizeX * sizeof(u32);

		// This is a "load in place" format in the original code.
		// We are going to allocate new memory and copy the data.
		u8* assetPtr = (u8*)malloc(size + columnSize);
		JediFrame* asset = (JediFrame*)assetPtr;

		memcpy(asset, data, size);

		WaxFrame* frame = asset;
		WaxCell* cell = WAX_CellPtr(asset, frame);

		// After load, the frame data has to be fixed up before rendering.
		// frame sizes remain in fixed point.
		frame->widthWS = div16(intToFixed16(cell->sizeX), SPRITE_SCALE_FIXED);
		frame->heightWS = div16(intToFixed16(cell->sizeY), SPRITE_SCALE_FIXED);

		const s32 offsetX = -intToFixed16(frame->offsetX);
		const s32 offsetY = intToFixed16(cell->sizeY) + intToFixed16(frame->offsetY);
		frame->offsetX = div16(offsetX, SPRITE_SCALE_FIXED);
		frame->offsetY = div16(offsetY, SPRITE_SCALE_FIXED);

		if (cell->compressed == 1)
		{
			// Update the column offset, it starts right after the cell data.
			cell->columnOffset = frame->cellOffset + sizeof(WaxCell);
		}
		else
		{
			u32* columns = (u32*)((u8*)asset + size);
			// Local pointer.
			cell->columnOffset = u32((u8*)columns - (u8*)asset);
			// Calculate column offsets.
			for (s32 c = 0; c < cell->sizeX; c++)
			{
				columns[c] = cell->sizeY * c;
			}
		}
		return asset;
	}

	static std::vector<u32> s_cellOffsets;

	bool isUniqueCell(u32 offset)
	{
		const size_t count = s_cellOffsets.size();
		const u32* offsetList = s_cellOffsets.data();
		for (u32 i = 0; i < count; i++)
		{
			if (offsetList[i] == offset) { return false; }
		}
		s_cellOffsets.push_back(offset);

		return true;
	}

	void sprite_serializeSpritesAndFrames(Stream* stream)
	{
		const bool modeWrite = serialization_getMode() == SMODE_WRITE;

		if (!modeWrite)
		{
			freeLevelData();
		}

		s32 frameCount, spriteCount;
		if (modeWrite)
		{
			frameCount = (s32)s_frameNames[POOL_LEVEL].size();
			spriteCount = (s32)s_spriteNames[POOL_LEVEL].size();
		}
		SERIALIZE(SaveVersionInit, frameCount, 0);
		SERIALIZE(SaveVersionInit, spriteCount, 0);

		std::string* frameNames  = s_frameNames[POOL_LEVEL].data();
		std::string* spriteNames = s_spriteNames[POOL_LEVEL].data();
		std::string name;
		for (s32 i = 0; i < frameCount; i++)
		{
			u8 size;
			if (modeWrite)
			{
				size = (u8)frameNames[i].length();
			}
			SERIALIZE(SaveVersionInit, size, 0);

			if (!modeWrite)
			{
				name.resize(size);
			}
			SERIALIZE_BUF(SaveVersionInit, modeWrite ? &frameNames[i][0] : &name[0], size);

			if (serialization_getMode() == SMODE_READ)
			{
				getFrame(name.c_str(), POOL_LEVEL);
			}
		}
		for (s32 i = 0; i < spriteCount; i++)
		{
			u8 size;
			if (modeWrite)
			{
				size = (u8)spriteNames[i].length();
			}
			SERIALIZE(SaveVersionInit, size, 0);
			if (!modeWrite)
			{
				name.resize(size);
			}
			SERIALIZE_BUF(SaveVersionInit, modeWrite ? &spriteNames[i][0] : &name[0], size);

			if (serialization_getMode() == SMODE_READ)
			{
				getWax(name.c_str(), POOL_LEVEL);
			}
		}
	}
		
	JediWax* getWax(const char* name, AssetPool pool)
	{
		SpriteMap::iterator iSprite = s_sprites[pool].find(name);
		if (iSprite != s_sprites[pool].end())
		{
			return iSprite->second;
		}

		// It doesn't exist yet, try to load the frame.
		FilePath filePath;
		if (!TFE_Paths::getFilePath(name, &filePath))
		{
			return nullptr;
		}
		FileStream file;
		if (!file.open(&filePath, Stream::MODE_READ))
		{
			return nullptr;
		}
		size_t len = file.getSize();
		s_buffer.resize(len);
		file.readBuffer(s_buffer.data(), u32(len));
		file.close();

		const u8* data = s_buffer.data();
		const Wax* srcWax = (Wax*)data;
		
		// every animation is filled out until the end, so no animations = no wax.
		if (!srcWax->animOffsets[0])
		{
			return nullptr;
		}
		s_cellOffsets.clear();

		// First determine the size to allocate (note that this will overallocate a bit because cells are shared).
		u32 sizeToAlloc = sizeof(JediWax) + (u32)s_buffer.size();
		const s32* animOffset = srcWax->animOffsets;
		for (s32 animIdx = 0; animIdx < 32 && animOffset[animIdx]; animIdx++)
		{
			WaxAnim* anim = (WaxAnim*)(data + animOffset[animIdx]);
			const s32* viewOffsets = anim->viewOffsets;
			for (s32 v = 0; v < 32; v++)
			{
				const WaxView* view = (WaxView*)(data + viewOffsets[v]);
				const s32* frameOffset = view->frameOffsets;
				for (s32 f = 0; f < 32 && frameOffset[f]; f++)
				{
					const WaxFrame* frame = (WaxFrame*)(data + frameOffset[f]);
					const WaxCell* cell = frame->cellOffset ? (WaxCell*)(data + frame->cellOffset) : nullptr;
					if (cell && cell->compressed == 0 && isUniqueCell(frame->cellOffset))
					{
						sizeToAlloc += cell->sizeX * sizeof(u32);
					}
				}
			}
		}

		// Allocate and copy the data (this is a "copy in place" format... mostly.
		JediWax* asset = (JediWax*)malloc(sizeToAlloc);
		Wax* dstWax = asset;
		memcpy(dstWax, srcWax, s_buffer.size());

		// Loop through animation list until we reach 32 (maximum count) or a null animation.
		// This means that animations are contiguous.
		fixed16_16 scaledWidth, scaledHeight;

		u32 cellOffsetPtr = 0;
		fixed16_16 worldWidth, worldHeight;

		s32 animIdx = 0;
		for (; animIdx < 32 && animOffset[animIdx]; animIdx++)
		{
			WaxAnim* dstAnim = (WaxAnim*)((u8*)asset + animOffset[animIdx]);

			if (animIdx == 0)
			{
				scaledWidth  = div16(SPRITE_SCALE_FIXED, dstAnim->worldWidth);
				scaledHeight = div16(SPRITE_SCALE_FIXED, dstAnim->worldHeight);

				worldWidth  = dstAnim->worldWidth;
				worldHeight = dstAnim->worldHeight;

				dstWax->xScale = dstAnim->worldWidth;
				dstWax->yScale = dstAnim->worldHeight;
			}
			else
			{
				dstAnim->worldWidth = worldWidth;
				dstAnim->worldHeight = worldHeight;
			}

			const s32* viewOffsets = dstAnim->viewOffsets;
			for (s32 v = 0; v < 32; v++)
			{
				const WaxView* dstView = (WaxView*)((u8*)asset + viewOffsets[v]);
				const s32* frameOffset = dstView->frameOffsets;
				s32 frameCount = 0;
				for (s32 f = 0; f < 32 && frameOffset[f]; f++, frameCount++)
				{
					const WaxFrame* srcFrame = (WaxFrame*)(data + frameOffset[f]);
					WaxFrame* dstFrame = (WaxFrame*)((u8*)asset + frameOffset[f]);

					// Some frames are shared between animations, so we need to read from the source, unmodified data.
					dstFrame->offsetX = round16(mul16(dstAnim->worldWidth,  intToFixed16(srcFrame->offsetX)));
					dstFrame->offsetY = round16(mul16(dstAnim->worldHeight, intToFixed16(srcFrame->offsetY)));

					WaxCell* dstCell = dstFrame->cellOffset ? (WaxCell*)((u8*)asset + dstFrame->cellOffset) : nullptr;
					if (dstCell)
					{
						dstFrame->widthWS  = div16(intToFixed16(dstCell->sizeX), scaledWidth);
						dstFrame->heightWS = div16(intToFixed16(dstCell->sizeY), scaledHeight);
						assert(dstFrame->widthWS != 0 && dstFrame->heightWS != 0);

						if (dstCell->columnOffset == 0)
						{
							if (dstCell->compressed == 1)
							{
								// Update the column offset, it starts right after the cell data.
								dstCell->columnOffset = dstFrame->cellOffset + sizeof(WaxCell);
							}
							else
							{
								u32* columns = (u32*)((u8*)asset + s_buffer.size() + cellOffsetPtr);
								cellOffsetPtr += dstCell->sizeX * sizeof(u32);

								// Local pointer.
								dstCell->columnOffset = u32((u8*)columns - (u8*)asset);
								// Calculate column offsets.
								for (s32 c = 0; c < dstCell->sizeX; c++)
								{
									columns[c] = dstCell->sizeY * c;
								}
							}
						}

						dstFrame->offsetX = div16(-intToFixed16(dstFrame->offsetX), SPRITE_SCALE_FIXED);
						const s32 adjOffsetY = mul16(intToFixed16(dstCell->sizeY), dstAnim->worldHeight) + intToFixed16(dstFrame->offsetY);
						dstFrame->offsetY = div16(adjOffsetY, SPRITE_SCALE_FIXED);
					}
				}
				if (v == 0)
				{
					dstAnim->frameCount = frameCount;
					assert(frameCount);
				}
				else
				{
					assert(frameCount == dstAnim->frameCount);
				}
			}
		}
		asset->animCount = animIdx;

		s_sprites[pool][name] = asset;
		s_spriteList[pool].push_back(asset);
		s_spriteNames[pool].push_back(name);
		return asset;
	}

	JediWax* loadWaxFromMemory(const u8* data, size_t size)
	{
		const Wax* srcWax = (Wax*)data;

		// every animation is filled out until the end, so no animations = no wax.
		if (!srcWax->animOffsets[0])
		{
			return nullptr;
		}
		s_cellOffsets.clear();

		// First determine the size to allocate (note that this will overallocate a bit because cells are shared).
		u32 sizeToAlloc = sizeof(JediWax) + (u32)size;
		const s32* animOffset = srcWax->animOffsets;
		for (s32 animIdx = 0; animIdx < 32 && animOffset[animIdx]; animIdx++)
		{
			WaxAnim* anim = (WaxAnim*)(data + animOffset[animIdx]);
			const s32* viewOffsets = anim->viewOffsets;
			for (s32 v = 0; v < 32; v++)
			{
				const WaxView* view = (WaxView*)(data + viewOffsets[v]);
				const s32* frameOffset = view->frameOffsets;
				for (s32 f = 0; f < 32 && frameOffset[f]; f++)
				{
					const WaxFrame* frame = (WaxFrame*)(data + frameOffset[f]);
					const WaxCell* cell = frame->cellOffset ? (WaxCell*)(data + frame->cellOffset) : nullptr;
					if (cell && cell->compressed == 0 && isUniqueCell(frame->cellOffset))
					{
						sizeToAlloc += cell->sizeX * sizeof(u32);
					}
				}
			}
		}

		// Allocate and copy the data (this is a "copy in place" format... mostly.
		JediWax* asset = (JediWax*)malloc(sizeToAlloc);
		Wax* dstWax = asset;
		memcpy(dstWax, srcWax, size);

		// Loop through animation list until we reach 32 (maximum count) or a null animation.
		// This means that animations are contiguous.
		fixed16_16 scaledWidth, scaledHeight;

		u32 cellOffsetPtr = 0;
		fixed16_16 worldWidth, worldHeight;

		s32 animIdx = 0;
		for (; animIdx < 32 && animOffset[animIdx]; animIdx++)
		{
			WaxAnim* dstAnim = (WaxAnim*)((u8*)asset + animOffset[animIdx]);

			if (animIdx == 0)
			{
				scaledWidth  = div16(SPRITE_SCALE_FIXED, dstAnim->worldWidth);
				scaledHeight = div16(SPRITE_SCALE_FIXED, dstAnim->worldHeight);

				worldWidth  = dstAnim->worldWidth;
				worldHeight = dstAnim->worldHeight;

				dstWax->xScale = dstAnim->worldWidth;
				dstWax->yScale = dstAnim->worldHeight;
			}
			else
			{
				dstAnim->worldWidth  = worldWidth;
				dstAnim->worldHeight = worldHeight;
			}

			const s32* viewOffsets = dstAnim->viewOffsets;
			for (s32 v = 0; v < 32; v++)
			{
				const WaxView* dstView = (WaxView*)((u8*)asset + viewOffsets[v]);
				const s32* frameOffset = dstView->frameOffsets;
				s32 frameCount = 0;
				for (s32 f = 0; f < 32 && frameOffset[f]; f++, frameCount++)
				{
					const WaxFrame* srcFrame = (WaxFrame*)(data + frameOffset[f]);
					WaxFrame* dstFrame = (WaxFrame*)((u8*)asset + frameOffset[f]);

					// Some frames are shared between animations, so we need to read from the source, unmodified data.
					dstFrame->offsetX = round16(mul16(dstAnim->worldWidth, intToFixed16(srcFrame->offsetX)));
					dstFrame->offsetY = round16(mul16(dstAnim->worldHeight, intToFixed16(srcFrame->offsetY)));

					WaxCell* dstCell = dstFrame->cellOffset ? (WaxCell*)((u8*)asset + dstFrame->cellOffset) : nullptr;
					if (dstCell)
					{
						dstFrame->widthWS = div16(intToFixed16(dstCell->sizeX), scaledWidth);
						dstFrame->heightWS = div16(intToFixed16(dstCell->sizeY), scaledHeight);
						assert(dstFrame->widthWS != 0 && dstFrame->heightWS != 0);

						if (dstCell->columnOffset == 0)
						{
							if (dstCell->compressed == 1)
							{
								// Update the column offset, it starts right after the cell data.
								dstCell->columnOffset = dstFrame->cellOffset + sizeof(WaxCell);
							}
							else
							{
								u32* columns = (u32*)((u8*)asset + size + cellOffsetPtr);
								cellOffsetPtr += dstCell->sizeX * sizeof(u32);

								// Local pointer.
								dstCell->columnOffset = u32((u8*)columns - (u8*)asset);
								// Calculate column offsets.
								for (s32 c = 0; c < dstCell->sizeX; c++)
								{
									columns[c] = dstCell->sizeY * c;
								}
							}
						}

						dstFrame->offsetX = div16(-intToFixed16(dstFrame->offsetX), SPRITE_SCALE_FIXED);
						const s32 adjOffsetY = mul16(intToFixed16(dstCell->sizeY), dstAnim->worldHeight) + intToFixed16(dstFrame->offsetY);
						dstFrame->offsetY = div16(adjOffsetY, SPRITE_SCALE_FIXED);
					}
				}
				if (v == 0)
				{
					dstAnim->frameCount = frameCount;
					assert(frameCount);
				}
				else
				{
					assert(frameCount == dstAnim->frameCount);
				}
			}
		}
		asset->animCount = animIdx;
		return asset;
	}
				
	const std::vector<JediWax*>& getWaxList(AssetPool pool)
	{
		return s_spriteList[pool];
	}

	const std::vector<JediFrame*>& getFrameList(AssetPool pool)
	{
		return s_frameList[pool];
	}

	void freePool(AssetPool pool)
	{
		const size_t frameCount = s_frameList[pool].size();
		JediFrame** frameList = s_frameList[pool].data();
		for (size_t i = 0; i < frameCount; i++)
		{
			free(frameList[i]);
		}
		s_frames[pool].clear();
		s_frameList[pool].clear();
		s_frameNames[pool].clear();

		const size_t waxCount = s_spriteList[pool].size();
		JediWax** waxList = s_spriteList[pool].data();
		for (size_t i = 0; i < waxCount; i++)
		{
			free(waxList[i]);
		}
		s_sprites[pool].clear();
		s_spriteList[pool].clear();
		s_spriteNames[pool].clear();
	}

	void freeAll()
	{
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			freePool(AssetPool(p));
		}
	}

	void freeLevelData()
	{
		freePool(POOL_LEVEL);
	}

	bool getWaxIndex(JediWax* wax, s32* index, AssetPool* pool)
	{
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			const size_t waxCount = s_spriteList[p].size();
			JediWax** waxList = s_spriteList[p].data();
			for (size_t i = 0; i < waxCount; i++)
			{
				if (waxList[i] == wax)
				{
					*index = s32(i);
					*pool = AssetPool(p);
					return true;
				}
			}
		}
		return false;
	}

	JediWax* getWaxByIndex(s32 index, AssetPool pool)
	{
		if (pool >= POOL_COUNT || index >= (s32)s_spriteList[pool].size())
		{
			return nullptr;
		}
		return s_spriteList[pool][index];
	}

	bool getFrameIndex(JediFrame* frame, s32* index, AssetPool* pool)
	{
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			const size_t frameCount = s_frameList[p].size();
			JediFrame** frameList = s_frameList[p].data();
			for (size_t i = 0; i < frameCount; i++)
			{
				if (frameList[i] == frame)
				{
					*index = s32(i);
					*pool = AssetPool(p);
					return true;
				}
			}
		}
		return false;
	}

	JediFrame* getFrameByIndex(s32 index, AssetPool pool)
	{
		if (pool >= POOL_COUNT || index >= (s32)s_frameList[pool].size())
		{
			return nullptr;
		}
		return s_frameList[pool][index];
	}
}
