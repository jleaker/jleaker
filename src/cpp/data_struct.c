#include "data_struct.h"
#include "agent_util.h"
#include "allocator.h"

extern GlobalData* gdata;
static ThreadData tdata;

MemoryNode* newMemoryNode()
{
	MemoryNode* n = (MemoryNode*)myAlloc(sizeof(*n));
	tdata.nodes_allocated++;
    (void)memset(n, 0, sizeof(*n));
    n->leaks_related = bitMaskSet_new(gdata->numberOfLeaks);
	n->reference_pointing_to_me++;
    return n;
}

jboolean freeMemoryNode(MemoryNode* node)
{
	MemoryReferer* ref;
	ClassReferenceCount* fan_in_ptr;
	JNIEnv* env = getThreadData()->jni;

	if (NULL == node)
	{
		return JNI_FALSE;
	}
	node->reference_pointing_to_me--;
	if (node->visited != JNI_FALSE)
	{
		return JNI_FALSE;
	}
	node->visited = JNI_TRUE;
	if (NULL != node->classname)
	{
		myFree(node->classname);
		node->classname = NULL;
	}
	if (NULL != node->klass)
	{
		(*env)->DeleteLocalRef(env, node->klass);
		node->klass = NULL;
	}
	if (NULL != node->obj)
	{
		(*env)->DeleteLocalRef(env, node->obj);
		node->obj = NULL;
	}
	while (NULL != node->ignore_fields)
	{
		IgnoreField* ignoreField = node->ignore_fields->next;
		myFree(node->ignore_fields->fieldName);
		myFree(node->ignore_fields);
		node->ignore_fields = ignoreField;
	}
	bitMaskSet_free(&node->leaks_related);
	ref = node->start;
	while (NULL != ref)
	{
		MemoryReferer* next = ref->next;
		freeMemoryNode(ref->node);
		myFree(ref);
		ref = next;
	}
	node->start = node->last = NULL;

	fan_in_ptr = node->fan_in;
	while (NULL != fan_in_ptr)
	{
		ClassReferenceCount* next = fan_in_ptr->next;
		myFree(fan_in_ptr);
		fan_in_ptr = next;
	}
	node->fan_in = NULL;

	if (0 < node->reference_pointing_to_me)
	{
		node->visited = JNI_FALSE;
		return JNI_FALSE;
	}
	tdata.nodes_freed++;
	myFree(node);
	return JNI_TRUE;
}

jboolean hasReferenceBetweenObjects(MemoryNode* n1, MemoryNode* n2,
		jvmtiHeapReferenceKind reference_kind, const jvmtiHeapReferenceInfo* reference_info)
{
	MemoryReferer* ref = n1->start;
	while (NULL != ref)
	{
		if (ref->kind == reference_kind)
		{
			if ((NULL == reference_info) || (0 == memcmp(reference_info, &ref->info, sizeof(ref->info))))
			{
				if (ref->node == n2)
				{
					return JNI_TRUE;
				}
			}
		}
		ref = ref->next;
	}
	return JNI_FALSE;
}

int addReferenceClass(MemoryNode* node, MemoryNode* classNode)
{
	jclass theClass;
	ClassReferenceCount* rCount, **last;
	if (!classNode->classNode)
	{
		fatal_error("Node %p is not a class node\n", classNode);
	}
	theClass = (jclass)classNode->obj;
	last = &node->fan_in;
	rCount = *last;
	while (NULL != rCount)
	{
		if (rCount->klass == theClass)
		{
			return ++rCount->count;
		}
		last = &rCount->next;
		rCount = rCount->next;
	}
	*last = (ClassReferenceCount*)myAlloc(sizeof(**last));
	memset(*last, 0, sizeof(**last));
	(*last)->klass = theClass;
	return ++(*last)->count;
}

void freeMemoryForLeakList(LeakingNodes* lstLeaks)
{
	LeakingNodes* leak = lstLeaks;

	while (NULL != leak)
	{
		LeakingNodes* next = leak->next;
		freeMemoryNode(leak->node);
		myFree(leak);
		leak = next;
	}
}

void initThreadData(JNIEnv* env)
{
	jclass objectClass;
	jclass threadClass;
	jmethodID metCurrentThread, metGetThreadID;
	jobject thisThread;
	int i;

	memset(&tdata, 0, sizeof(tdata));
	tdata.jni = env;
	tdata.outputStream.type = OUTPUT_TYPE_FILE;
	tdata.outputStream.handle.file = stdout;
    tdata.classClass = (*env)->FindClass(env, "java/lang/Class");
    objectClass = (*env)->FindClass(env, "java/lang/Object");
    tdata.metEquals = (*env)->GetMethodID(env, objectClass, "equals", "(Ljava/lang/Object;)Z");

    tdata.sizeableClasses = myAlloc(sizeof(*tdata.sizeableClasses)*gdata->sizeableClassesNum);
    for (i = 0; i < gdata->sizeableClassesNum; i++)
    {
    	tdata.sizeableClasses[i].klass = (*env)->FindClass(env, gdata->sizeableClasses[i].classname);
    }

    threadClass = (*env)->FindClass(env, "java/lang/Thread");
    metCurrentThread = (*env)->GetStaticMethodID(env, threadClass, "currentThread", "()Ljava/lang/Thread;");
    metGetThreadID = (*env)->GetMethodID(env, threadClass, "getId", "()J");
    thisThread = (*env)->CallStaticObjectMethod(env, threadClass, metCurrentThread);
    tdata.thread_id = (*env)->CallLongMethod(env, thisThread, metGetThreadID);
	tdata.metGetClassName = (*env)->GetMethodID(env, tdata.classClass, "getCanonicalName", "()Ljava/lang/String;");

    (*env)->DeleteLocalRef(env, objectClass);
    (*env)->DeleteLocalRef(env, threadClass);
    (*env)->DeleteLocalRef(env, thisThread);
}

void releaseThreadData()
{
	JNIEnv* env = tdata.jni;
	int i;
	(*env)->DeleteLocalRef(env, tdata.classClass);

    for (i = 0; i < gdata->sizeableClassesNum; i++)
    {
    	jclass klass = tdata.sizeableClasses[i].klass;
    	if (NULL != klass)
    	{
    		(*env)->DeleteLocalRef(env, klass);
    	}
    }
    sm_delete(gdata->ignore_classes);
    sm_delete(gdata->ignore_referenced_by);
    gdata->ignore_classes = NULL;
    gdata->ignore_referenced_by = NULL;
    myFree(tdata.sizeableClasses);

	if (tdata.nodes_allocated != tdata.nodes_freed)
	{
		alert("DETECTED INTERNAL LEAK: nodes allocated: %d, nodes freed: %d\n", tdata.nodes_allocated, tdata.nodes_freed);
	}
	memset(&tdata, 0, sizeof(tdata));
}

ThreadData* getThreadData()
{
	return &tdata;
}

void startTimer(Timer* t, int debug)
{
	jint err = (*gdata->jvmti)->GetTime(gdata->jvmti, &t->startTime);
	check_jvmti_error(gdata->jvmti, err, "Get Time");
	t->debug = debug;
}

void stopTimer(Timer* t, const char* msg)
{
	jlong endTime;
	long elapsed;
    jint err = (*gdata->jvmti)->GetTime(gdata->jvmti, &endTime);
    check_jvmti_error(gdata->jvmti, err, "Get Time");
    elapsed = (long)(endTime - t->startTime + 500000)/1000000;
    if (t->debug)
    {
    	debug("%s took %ld millis\n",msg, elapsed);
    }
    else
    {
    	alert("%s took %ld millis\n",msg, elapsed);
    }
}

