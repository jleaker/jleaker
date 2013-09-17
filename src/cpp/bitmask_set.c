#include "bitmask_set.h"
#include <stdio.h>
#include <string.h>
#include "allocator.h"

#define SIZE_IN_BYTES(sz) 1+(sz>>3)

bitmask_set* bitMaskSet_new(int size)
{
	bitmask_set* s = myAlloc(sizeof(*s));
	int sizeToAlloc = SIZE_IN_BYTES(size);

	s->size = size;
	s->ptr = myAlloc(sizeToAlloc);
	memset(s->ptr, 0, sizeToAlloc);
	return s;
}

void bitMaskSet_free(bitmask_set** s)
{
	if (NULL == *s)
	{
		return;
	}
	myFree((*s)->ptr);
	myFree(*s);
	*s = NULL;
}

void bitMapSet_add(bitmask_set* s, int n)
{
	unsigned char* p;
	if (n >= s->size)
	{
		return;
	}
	p = (unsigned char*)s->ptr;
	*(p+(n>>3)) |= (1<<(n&7));
}

void bitMapSet_remove(bitmask_set* s, int n)
{
	unsigned char* p;
	if (n >= s->size)
	{
		return;
	}
	p = (unsigned char*)s->ptr;
	*(p+(n>>3)) &= (unsigned char)(~(1<<(n&7)));
}

void bitMapSet_addAll(bitmask_set* s, bitmask_set* o)
{
	unsigned char* p1, *p2;
	int minSize = (s->size < o->size)?s->size:o->size;
	int i;
	minSize = (minSize >> 3) + 1;
	p1 = (unsigned char*)s->ptr;
	p2 = (unsigned char*)o->ptr;
	for (i = 0; i < minSize; i++, p1++, p2++) *p1 |= *p2;
}

void bitMapSet_addAllWithExclusion(bitmask_set* s, bitmask_set* o, bitmask_set* e)
{
	unsigned char* p1, *p2, *p3;
	int minSize = (s->size < o->size)?s->size:o->size;
	int exclSize = (minSize < e->size)?minSize:e->size;
	int i;
	minSize = SIZE_IN_BYTES(minSize);
	exclSize = SIZE_IN_BYTES(exclSize);
	p1 = (unsigned char*)s->ptr;
	p2 = (unsigned char*)o->ptr;
	p3 = (unsigned char*)e->ptr;
	for (i = 0; i < exclSize; i++, p1++, p2++, p3++) *p1 |= (*p2 & ~*p3);
	for (; i < minSize; i++, p1++, p2++) *p1 |= *p2;
}

int bitMapSet_containsAll(bitmask_set* s, bitmask_set* o)
{
	unsigned char* p1, *p2;
	int minSize;
	int i;
	minSize = SIZE_IN_BYTES((s->size < o->size)?s->size:o->size);
	p1 = (unsigned char*)s->ptr;
	p2 = (unsigned char*)o->ptr;
	for (i = 0; i < minSize; i++, p1++, p2++) if ((*p1 & *p2) != *p2) return 0;
	return 1;
}

int bitMapSet_contains(bitmask_set* s, int n)
{
	unsigned char* p;
	if (n >= s->size)
	{
		return 0;
	}
	p = (unsigned char*)s->ptr;
	return (*(p+(n>>3)) & (1<<(n&7)))?1:0;
}

int bitMapSet_isEmpty(bitmask_set* s)
{
	unsigned char* p1;
	int i, size = SIZE_IN_BYTES(s->size);
	p1 = (unsigned char*)s->ptr;
	for (i = 0; i < size; i++, p1++) if (*p1 != 0) return 0;
	return 1;
}
