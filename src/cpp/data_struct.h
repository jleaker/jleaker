#ifndef __DATA_STRUCT_H__
#define __DATA_STRUCT_H__

#include "bitmask_set.h"
#include "strmap.h"
#include <jvmti.h>
#ifdef WIN32
#	include <WinSock2.h>
#endif

typedef void (*object_print_function)(jobject);

typedef struct
{
	jlong startTime;
	int debug;
} Timer;

typedef struct
{
	const char* classname;
	object_print_function print_fn;
} SizeableClassDescriptor;

/* Global static data */
typedef struct
{
    jboolean      vmDeathCalled;
    jboolean      vmStarted;
    jboolean      dumpInProgress;
    jboolean run_gc;
    jboolean show_unreachables;
    StrMap *ignore_classes;
    StrMap *ignore_referenced_by;
    jrawMonitorID lock;
    jvmtiEnv *jvmti;
    JavaVM *vm;
    SizeableClassDescriptor* sizeableClasses;
    int sizeableClassesNum;
    int tcp_port;
    jint size_threshold;
    int max_fan_in;
    int reference_chain_length;
    int num_elements_to_dump;
    int numberOfLeaks;
    int debug;
    int consider_local_references;
    int self_check;
} GlobalData;

typedef struct
{
	jclass klass;
} SizeableClassThreadData;

typedef struct
{
	jobject* obj_ptr;
	jlong* tag_ptr;
	jint count;
	jmethodID *sizeMethods;
} LeakCheckData;

typedef struct _MemoryReferer
{
	jvmtiHeapReferenceKind kind;
    jvmtiHeapReferenceInfo info;
    struct _MemoryNode *node;
	struct _MemoryReferer* next;
} MemoryReferer;

typedef struct _ClassReferenceCount
{
	jclass klass;
	int count;
	struct _ClassReferenceCount* next;
} ClassReferenceCount;

typedef struct _IgnoreField
{
	jint field;
	char* fieldName;
	int threshold;
	struct _IgnoreField* next;
} IgnoreField;

typedef struct _MemoryNode
{
	jboolean visited;
	jboolean dead;
	jboolean classNode;
	jclass klass;
	jobject obj;
	jint leak_size;
	char* classname;
	MemoryReferer* start;
	MemoryReferer* last;
	bitmask_set* leaks_related;
	int reference_pointing_to_me;
	ClassReferenceCount* fan_in;
	IgnoreField* ignore_fields;
} MemoryNode;

#define OUTPUT_TYPE_FILE 1
#define OUTPUT_TYPE_WIN32SOCK 2

typedef struct
{
	union 
	{
#ifdef WIN32
		SOCKET windowSocket;
#endif
		FILE* file;
	} handle;
	int type;
} OutputStream;
typedef struct
{
	jlong thread_id;
    jclass classClass;
    jmethodID metEquals;
    jmethodID metGetClassName;
    JNIEnv* jni;
    SizeableClassThreadData* sizeableClasses;
    Timer timer;
    MemoryNode** classNodes;
    int nodes_allocated;
    int nodes_freed;
	OutputStream outputStream;
} ThreadData;

typedef struct _LeakingNodes
{
	MemoryNode* node;
	jint leak_size;
	object_print_function print_fn;
	int leakNumber;
	struct _LeakingNodes *next;
} LeakingNodes;

typedef struct
{
	bitmask_set* leaks_finished;
	int* nodes_found;
} FollowReferencesData;

struct entryMethods
{
	jmethodID getKey;
	jmethodID getValue;
};

extern GlobalData* gdata;

MemoryNode* newMemoryNode();
jboolean freeMemoryNode(MemoryNode* node);
void freeMemoryForLeakList(LeakingNodes* lstLeaks);
void freeGlobalData();
void initThreadData(JNIEnv* env);
void releaseThreadData();
jboolean hasReferenceBetweenObjects(MemoryNode* n1, MemoryNode* n2, jvmtiHeapReferenceKind reference_kind, const jvmtiHeapReferenceInfo* reference_info);
int addReferenceClass(MemoryNode* node, MemoryNode* classNode);
ThreadData* getThreadData();
void startTimer(Timer*, int);
void stopTimer(Timer*, const char*);

#endif
