#ifndef __JVM_REFERENCE_H__
#define __JVM_REFERENCE_H__

#include <jvmti.h>
#include "data_struct.h"

typedef struct _InterfaceList
{
	jclass intr;
	struct _InterfaceList* next;
} InterfaceList;

typedef struct _OrderedReferences
{
	MemoryReferer* ref;
	struct _OrderedReferences* next;
} OrderedReferences;


jint getFieldOffset(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, char* name);
jboolean localReferenceOfThisThread(jvmtiHeapReferenceKind kind, const jvmtiHeapReferenceInfo* info);
jboolean isRootReference(jvmtiHeapReferenceKind);
jboolean shouldConsiderThisReference(jvmtiHeapReferenceKind kind);
void printReferencesChainForLeakingNodes(LeakingNodes*, int);

#endif
