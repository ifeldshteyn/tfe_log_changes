#include "imageAsset.h"
#include <TFE_System/system.h>
#include <TFE_Asset/assetSystem.h>
#include <TFE_Archive/archive.h>
#include <TFE_FileSystem/memorystream.h>
#include <assert.h>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <SDL_image.h>
#include <SDL_rwops.h>

namespace TFE_Image
{
	typedef std::map<std::string, Image*> ImageMap;
	static ImageMap s_images;
	static std::vector<u8> s_buffer;

	static SDL_Surface* convertToRGBA(SDL_Surface* src)
	{
		SDL_PixelFormat rgba32 = {
			.format = SDL_PIXELFORMAT_RGBA32,
			.palette = NULL,
			.BitsPerPixel = 32,
			.BytesPerPixel = 4,
			.Rmask = 0x000000FF,
			.Gmask = 0x0000FF00,
			.Bmask = 0x00FF0000,
			.Amask = 0xFF000000,
			.Rloss = 0,
			.Gloss = 0,
			.Bloss = 0,
			.Aloss = 0,
			.Rshift = 0,
			.Gshift = 8,
			.Bshift = 16,
			.Ashift = 24
		};
		
		SDL_Surface* n = SDL_ConvertSurface(src, &rgba32, 0);
		SDL_FreeSurface(src);
		return n;
	}

	void init()
	{
		int ret;
		int flags = IMG_INIT_PNG | IMG_INIT_JPG;
		
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_Image::init");
		ret = IMG_Init(flags);
		if ((ret & flags) != flags)
		{
			TFE_System::logWrite(LOG_ERROR, "ImageAsset", "SDL_image init failed!");
		}
	}

	void shutdown()
	{
		freeAll();
		IMG_Quit();
	}

	Image* loadFromMemory(const u8* buffer, size_t size)
	{
		SDL_RWops* memops = SDL_RWFromConstMem(buffer, size);
		if (!memops)
			return nullptr;
			
		SDL_Surface* sdlimg = IMG_Load_RW(memops, 1);
		if (!sdlimg)
			return nullptr;
		if (sdlimg->format->BitsPerPixel != 32)
			sdlimg = convertToRGBA(sdlimg);
		if (!sdlimg)
			return nullptr;

		Image* image = new Image;
		
		image->sdl = sdlimg;
		image->width  = (u32)sdlimg->w;
		image->height = (u32)sdlimg->h;
		image->data = (u32*)sdlimg->pixels;
		SDL_LockSurface(sdlimg);	// required to manipulate the pixel buffer

		return image;
	}

	Image* get(const char* imagePath)
	{
		ImageMap::iterator iImage = s_images.find(imagePath);
		if (iImage != s_images.end())
		{
			return iImage->second;
		}

		SDL_Surface* sdlimg = IMG_Load(imagePath);
		if (!sdlimg)
			return nullptr;
		if (sdlimg->format->BitsPerPixel != 32)
			sdlimg = convertToRGBA(sdlimg);
		if (!sdlimg)
			return nullptr;
		
		Image* image = new Image;
		if (!image)
		{
			SDL_FreeSurface(sdlimg);
			return nullptr;
		}

		image->sdl = sdlimg;
		image->width  = (u32)sdlimg->w;
		image->height = (u32)sdlimg->h;
		image->data = (u32*)sdlimg->pixels;
		SDL_LockSurface(sdlimg);	// required to manipulate the pixel buffer

		s_images[imagePath] = image;
		return image;
	}

	void free(Image* image)
	{
		if (!image) { return; }

		SDL_UnlockSurface(image->sdl);
		SDL_FreeSurface(image->sdl);
		image->sdl = nullptr;
		
		ImageMap::iterator iImage = s_images.begin();
		for (; iImage != s_images.end(); ++iImage)
		{
			if (iImage->second == image)
			{
				delete iImage->second;
				s_images.erase(iImage);
				break;
			}
		}
	}

	void freeAll()
	{
		ImageMap::iterator iImage = s_images.begin();
		for (; iImage != s_images.end(); ++iImage)
		{
			Image* image = iImage->second;
			if (image)
			{
				SDL_UnlockSurface(image->sdl);
				SDL_FreeSurface(image->sdl);
			}
			delete image;
		}
		s_images.clear();
	}

	void writeImage(const char* path, u32 width, u32 height, u32* pixelData)
	{
		SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(pixelData, width, height,
							     32, width * sizeof(u32), 
							     0xFF, 0xFF00, 0xFF0000, 0xFF000000);
		if (!surf)
			return;
		int ret = IMG_SavePNG(surf, path);
		if (ret != 0)
		{
			fprintf(stderr, "writeImage(%s) failed '%s'\n", path, SDL_GetError());
		}
	}

	//////////////////////////////////////////////////////
	// Wacky file override to get SDL-Image to write
	// images to memory.
	//////////////////////////////////////////////////////
	static size_t SDLCALL _sdl_wop_mem(struct SDL_RWops * context, const void *ptr,
					   size_t size, size_t num)
	{
		size_t bytes = num * size;
		ptrdiff_t space = context->hidden.mem.stop - context->hidden.mem.here;
		if (space >= bytes)
		{
			memcpy(context->hidden.mem.here, ptr, bytes);
			context->hidden.mem.here += bytes;
			return bytes;
		}
		else
		{
			fprintf(stderr, "AIEEE: _sdl_wop_mem %p %x %x | %llu %llu\n", ptr, size, num, space, bytes);
		}
			
		return 0;
	}

	//////////////////////////////////////////////////////
	// Code to write and read images from memory.
	//////////////////////////////////////////////////////
	size_t writeImageToMemory(u8* output, u32 width, u32 height, const u32* pixelData)
	{
		SDL_Surface* surf = SDL_CreateRGBSurfaceFrom((void *)pixelData, width, height, 32, width * sizeof(u32), 
							     0xFF, 0xFF00, 0xFF0000, 0xFF000000);
		if (!surf)
			return 0;
		SDL_RWops* memops = SDL_RWFromMem(output, width * height * sizeof(u32));
		if (!memops)
		{
			SDL_FreeSurface(surf);
			return 0;
		}
		memops->write = _sdl_wop_mem;
		int ret = IMG_SavePNG_RW(surf, memops, 0);
		SDL_FreeSurface(surf);
		size_t written = 0;
		if (ret == 0)
		{
			written = memops->hidden.mem.here - memops->hidden.mem.base;
		}
		SDL_FreeRW(memops);
		return written;
	}

	void readImageFromMemory(Image* output, size_t size, const u32* pixelData)
	{
		SDL_RWops* memops = SDL_RWFromConstMem(pixelData, size);
		if (!memops)
			return;
			
		SDL_Surface* sdlimg = IMG_Load_RW(memops, 1);
		if (!sdlimg)
			return;

		if (output->sdl)
		{
			SDL_UnlockSurface(output->sdl);
			SDL_FreeSurface(output->sdl);
		}

		output->sdl = sdlimg;
		output->width  = (u32)sdlimg->w;
		output->height = (u32)sdlimg->h;
		output->data = (u32*)sdlimg->pixels;
		SDL_LockSurface(sdlimg);	// required to manipulate the pixel buffer
	}
}
