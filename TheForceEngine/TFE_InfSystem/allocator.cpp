#include "allocator.h"
#include <TFE_System/system.h>
#include <TFE_System/memoryPool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <algorithm>

struct AllocHeader
{
	AllocHeader* prev;
	AllocHeader* next;
};

struct Allocator
{
	Allocator*   self;
	AllocHeader* head;
	AllocHeader* tail;
	AllocHeader* iterPrev;
	AllocHeader* iter;
	s32 size;
	s32 refCount;
	s32* u1c;
};

namespace InfAllocator
{
	// TODO: Use the "zone" allocator (memoryPool) for INF instead of allocating from the HEAP.
	// TODO: Replace the original DOS implementation with one a bit more modern memory friendly, this is basically the original DF code
	// verbatim with minor messaging to work in 32 or 64 bit.
	// This is just to get things working initially.
	static const size_t c_invalidPtr = (~size_t(0));
	#define ALLOC_INVALID_PTR ((AllocHeader*)c_invalidPtr)
		
	// Create and free an allocator.
	Allocator* allocator_create(s32 allocSize)
	{
		Allocator* res = (Allocator*)malloc(sizeof(Allocator));
		res->self = res;

		// the original code used special bit patterns to represent invalid pointers.
		res->head = ALLOC_INVALID_PTR;
		res->tail = ALLOC_INVALID_PTR;
		res->iterPrev = ALLOC_INVALID_PTR;
		res->iter = ALLOC_INVALID_PTR;
		res->size = allocSize + sizeof(AllocHeader);

		res->refCount = 0;
		res->u1c = nullptr;

		return res;
	}

	void allocator_free(Allocator* alloc)
	{
		if (!alloc) { return; }

		void* item = allocator_getHead(alloc);
		while (item)
		{
			allocator_deleteItem(alloc, item);
			item = allocator_getNext(alloc);
		}

		alloc->self = (Allocator*)ALLOC_INVALID_PTR;
		free(alloc);
	}

	// Allocate and free individual items.
	void* allocator_newItem(Allocator* arr)
	{
		if (!arr) { return nullptr; }

		AllocHeader* alloc = (AllocHeader*)malloc(arr->size);
		alloc->next = ALLOC_INVALID_PTR;
		alloc->prev = arr->tail;

		if (arr->tail != ALLOC_INVALID_PTR)
		{
			arr->tail->next = alloc;
		}
		arr->tail = alloc;

		if (arr->head == ALLOC_INVALID_PTR)
		{
			arr->head = alloc;
		}

		return ((u8*)alloc + sizeof(AllocHeader));
	}

	void  allocator_deleteItem(Allocator* alloc, void* item)
	{
		if (!alloc) { return; }

		AllocHeader* header = (AllocHeader*)((u8*)item - sizeof(AllocHeader));
		AllocHeader* prev = header->prev;
		AllocHeader* next = header->next;

		if (prev == ALLOC_INVALID_PTR) { alloc->head = next; }
		else { prev->next = next; }

		if (next == ALLOC_INVALID_PTR) { alloc->tail = prev; }
		else { next->prev = prev; }

		if (alloc->iter == header)
		{
			alloc->iter = header->prev;
		}
		if (alloc->iterPrev == header)
		{
			alloc->iterPrev = header->next;
		}

		free(header);
	}

	// Random access.
	void* allocator_getByIndex(Allocator* alloc, s32 index)
	{
		if (!alloc) { return nullptr; }

		AllocHeader* header = alloc->head;
		while (index > 0 && header != ALLOC_INVALID_PTR)
		{
			index--;
			header = header->next;
		}

		alloc->iterPrev = header;
		alloc->iter = header;
		return (u8*)header + sizeof(AllocHeader);
	}

	// Iteration
	void* allocator_getHead(Allocator* alloc)
	{
		if (!alloc) { return nullptr; }

		alloc->iterPrev = alloc->head;
		alloc->iter = alloc->head;
		return (u8*)alloc->iter + sizeof(AllocHeader);
	}

	void* allocator_getTail(Allocator* alloc)
	{
		if (!alloc) { return nullptr; }

		alloc->iterPrev = alloc->tail;
		alloc->iter = alloc->tail;

		return (u8*)alloc->tail + sizeof(AllocHeader);
	}

	void* allocator_getTail_noIterUpdate(Allocator* alloc)
	{
		if (!alloc) { return nullptr; }
		return (u8*)alloc->tail + sizeof(AllocHeader);
	}

	void* allocator_getNext(Allocator* alloc)
	{
		if (!alloc) { return nullptr; }

		AllocHeader* iter = alloc->iter;
		if (iter != ALLOC_INVALID_PTR)
		{
			alloc->iter = iter->next;
			alloc->iterPrev = iter->next;
			return (u8*)iter->next + sizeof(AllocHeader);
		}

		iter = alloc->head;
		alloc->iter = iter;
		alloc->iterPrev = iter;
		return (u8*)iter + sizeof(AllocHeader);
	}

	void* allocator_getPrev(Allocator* alloc)
	{
		if (!alloc) { return nullptr; }

		AllocHeader* iterPrev = alloc->iterPrev;
		if (iterPrev != ALLOC_INVALID_PTR)
		{
			AllocHeader* prev = iterPrev->prev;
			alloc->iter = iterPrev->prev;
			alloc->iterPrev = iterPrev->prev;
			return (u8*)iterPrev->prev + sizeof(AllocHeader);
		}

		alloc->iter = alloc->tail;
		alloc->iterPrev = alloc->tail;
		return (u8*)alloc->tail + sizeof(AllocHeader);
	}

	// Ref counting.
	void allocator_addRef(Allocator* alloc)
	{
		if (!alloc) { return; }
		alloc->refCount++;
	}

	void allocator_release(Allocator* alloc)
	{
		if (!alloc) { return; }
		alloc->refCount--;
	}

	s32 allocator_getRefCount(Allocator* alloc)
	{
		return alloc->refCount;
	}
}