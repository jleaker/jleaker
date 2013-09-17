#include "allocator.h"
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 4096
#define FILE_LINE_BUF_SIZE 128


static int allocations = 0;
static int frees = 0;
static int selfCheck = 0;

struct MallocatedNode
{
	const char* file;
	int line;
	struct MallocatedNode *prev, *next;
};

struct MallocatedNode head, tail;

void initAllocator(int _selfCheck)
{
	allocations = 0;
	frees = 0;
	selfCheck = _selfCheck;
	memset(&head, sizeof(head), 0);
	memset(&tail, sizeof(tail), 0);
	head.next = &tail;
	tail.prev = &head;
}

void fillMallocNode(struct MallocatedNode* n, const char* file, int line)
{
	n->file = file;
	n->line = line;
	n->prev = tail.prev;
	n->next = &tail;
	tail.prev->next = n;
	tail.prev = n;
}

void* _myAlloc(int sz, const char* file, int line)
{
	allocations++;
	if (selfCheck)
	{
		struct MallocatedNode* ptr = malloc(sz + sizeof(struct MallocatedNode));
		fillMallocNode(ptr, file, line);
		return ptr + 1;
	}
	return malloc(sz);
}

char* _myStrdup(const char* s, const char* file, int line)
{
	char* ret = _myAlloc(strlen(s) + 1, file, line);
	strcpy(ret, s);
	return ret;
}

void myFree(void* p)
{
	frees++;
	if (selfCheck)
	{
		struct MallocatedNode* ptr = (struct MallocatedNode*)p - 1;

		ptr->prev->next = ptr->next;
		ptr->next->prev = ptr->prev;

		free(ptr);
		return;
	}
	free(p);
}

char* findInternalMallocsLeaks()
{
	if (allocations != frees)
	{
		char* buf = malloc(BUF_SIZE);
		int pos = snprintf(buf, BUF_SIZE, "number of mallocs %d, number of frees %d\n", allocations, frees);
		while (head.next != &tail)
		{
			struct MallocatedNode* n = head.next->next;
			int newPos = snprintf(buf + pos, BUF_SIZE - pos, "Didn't free memory allocated from %s:%d\n", head.next->file, head.next->line);
			pos += newPos;
			free(head.next);
			head.next = n;
		}
		tail.prev = &head;
		return buf;
	}
	return NULL;
}
