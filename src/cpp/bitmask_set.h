#ifndef _BITMASK_SET_INCLUDED_
#define _BITMASK_SET_INCLUDED_

typedef struct
{
	void* ptr;
	int size;
} bitmask_set;

bitmask_set* bitMaskSet_new(int size);
void bitMaskSet_free(bitmask_set** s);
void bitMapSet_add(bitmask_set* s, int n);
void bitMapSet_remove(bitmask_set* s, int n);
int bitMapSet_contains(bitmask_set* s, int n);
void bitMapSet_addAll(bitmask_set* s, bitmask_set* o);
void bitMapSet_addAllWithExclusion(bitmask_set* s, bitmask_set* o, bitmask_set* e);
int bitMapSet_containsAll(bitmask_set* s, bitmask_set* o);
int bitMapSet_isEmpty(bitmask_set* s);

#endif

