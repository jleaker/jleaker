#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include <stdio.h>

#ifdef WIN32
#	define __UNUSED__
#else
#	define __UNUSED__ __attribute__ ((unused))
#endif

#if _MSC_VER
#define snprintf _snprintf
#endif

void initAllocator(int selfCheck);

#define myAlloc(x) (_myAlloc(x, __FILE__, __LINE__))
#define myStrdup(x) (_myStrdup(x, __FILE__, __LINE__))

void* _myAlloc(int sz, const char* file, int line);
char* _myStrdup(const char* s, const char* file, int line);
void myFree(void* p);
char* findInternalMallocsLeaks();

#endif

