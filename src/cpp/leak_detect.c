#include "leak_detect.h"
#include <stdint.h>
#include "allocator.h"
#include "jvm_reference.h"
#include "agent_util.h"
#include "jobject_print.h"

#define INITIAL_CLASS_TAG ((jlong)0xBadCafe)
#define CLASS_CLASS_INITIAL_TAG ((jlong)0xFeedDad)

typedef struct
{
	jlong thread_id;
	int jniLocalRefs;
} SelfLeakCheckData;

/* Heap object callback */
static jint JNICALL
cbHeapObject(__UNUSED__ jvmtiHeapReferenceKind reference_kind,
	 __UNUSED__ const jvmtiHeapReferenceInfo* reference_info, jlong class_tag,
	 __UNUSED__ jlong referrer_class_tag, __UNUSED__ jlong size,
     __UNUSED__ jlong* tag_ptr, __UNUSED__ jlong* referrer_tag_ptr, __UNUSED__ jint length, __UNUSED__ void* user_data)
{
	if (class_tag == CLASS_CLASS_INITIAL_TAG)
	{
	    return JVMTI_VISIT_OBJECTS;
	}
	if ((class_tag >= INITIAL_CLASS_TAG) && (class_tag - INITIAL_CLASS_TAG < gdata->sizeableClassesNum))
	{
		*tag_ptr = class_tag;
	}
	else
	{
		*tag_ptr = (jlong)0;
	}
    return JVMTI_VISIT_OBJECTS;
}


static jboolean isFieldIgnored(MemoryNode* refClassNode, MemoryNode* thisNode, jint fieldIndex)
{
	IgnoreField* ignoreFields = refClassNode->ignore_fields;
	while (NULL != ignoreFields)
	{
		if (ignoreFields->field == fieldIndex)
		{
			if (thisNode->leak_size < ignoreFields->threshold)
			{
				debug("\tField name=%s, Size=%d, threshold=%d\n", ignoreFields->fieldName, (int)thisNode->leak_size, ignoreFields->threshold);
				return JNI_TRUE;
			}
		}
		ignoreFields = ignoreFields->next;
	}
	return JNI_FALSE;
}

/* Callback for referrers object tagging (heap_reference_callback). */

static jint JNICALL
cbObjectTagReferrer(jvmtiHeapReferenceKind reference_kind,
     const jvmtiHeapReferenceInfo* reference_info, __UNUSED__ jlong class_tag,
     jlong referrer_class_tag, __UNUSED__ jlong size,
     jlong* tag_ptr, jlong* referrer_tag_ptr, __UNUSED__ jint length, void* user_data)
{
	FollowReferencesData* data;
	MemoryNode* thisNode, *refNode;
	MemoryReferer* ref;
    int i;

	if (NULL == user_data)
	{
		return JVMTI_VISIT_ABORT;
	}
	if (localReferenceOfThisThread(reference_kind, reference_info))
	{
		return JVMTI_VISIT_OBJECTS;
	}
    data = (FollowReferencesData*)user_data;
    thisNode = (MemoryNode*)(intptr_t)*tag_ptr;

	if (bitMapSet_containsAll(data->leaks_finished, thisNode->leaks_related))
	{
		return JVMTI_VISIT_OBJECTS;
	}
	if (bitMapSet_isEmpty(thisNode->leaks_related))
	{
		return JVMTI_VISIT_OBJECTS;
	}

    if (0L != referrer_class_tag)
    {
    	int refsFromSameClass = addReferenceClass(thisNode, (MemoryNode*)(intptr_t)referrer_class_tag);
    	if (refsFromSameClass >= gdata->max_fan_in)
    	{
    		return JVMTI_VISIT_OBJECTS;
    	}
    	if (reference_kind == JVMTI_HEAP_REFERENCE_FIELD && isFieldIgnored((MemoryNode*)(intptr_t)referrer_class_tag, thisNode, reference_info->field.index))
    	{
    		debug("Ignoring field %d\n", reference_info->field.index);
    		return JVMTI_VISIT_OBJECTS;
    	}
    }

    if (NULL == referrer_tag_ptr)
    {
    	/* Referrer is not a class */
    	refNode = NULL;
    }
    else if ( 0L != *referrer_tag_ptr )
    {
    	refNode = (MemoryNode*)(intptr_t)*referrer_tag_ptr;
    	if (reference_kind == JVMTI_HEAP_REFERENCE_STATIC_FIELD && isFieldIgnored(refNode, thisNode, reference_info->field.index))
    	{
    		debug("Ignoring static field %d\n", reference_info->field.index);
    		return JVMTI_VISIT_OBJECTS;
    	}
    	if (hasReferenceBetweenObjects(thisNode, refNode, reference_kind, reference_info))
    	{
    		return JVMTI_VISIT_OBJECTS;
    	}
        refNode->reference_pointing_to_me++;
    }
    else
    {
    	refNode = newMemoryNode();
        /* If the referrer can be tagged, and hasn't been tagged, tag it */
        *referrer_tag_ptr = (jlong)(intptr_t)refNode;
    }

	if (NULL != refNode)
	{
		bitMapSet_addAllWithExclusion(refNode->leaks_related, thisNode->leaks_related, data->leaks_finished);
		if (refNode->leak_size < thisNode->leak_size)
		{
			refNode->leak_size = thisNode->leak_size;
		}
	}

    ref = (MemoryReferer*)myAlloc(sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->node = refNode;

    if (NULL == thisNode->start)
    {
    	thisNode->start = ref;
    	thisNode->last = ref;
    }
    else
    {
    	thisNode->last->next = ref;
    	thisNode->last = ref;
    }
    if (NULL != reference_info)
    {
    	ref->info = *reference_info;
    }
    ref->kind = reference_kind;

    for (i = 0; i < gdata->numberOfLeaks; i++)
    {
    	if (bitMapSet_contains(thisNode->leaks_related, i) && !bitMapSet_contains(data->leaks_finished, i))
    	{
    		data->nodes_found[i]++;
    	}
    }

    if (isRootReference(reference_kind))
    {
    	bitMapSet_addAll(data->leaks_finished, thisNode->leaks_related);
    }

	return JVMTI_VISIT_OBJECTS;

}

static void fillClassIgnoreList(JNIEnv* jni, MemoryNode* node)
{
	IgnoreField* ignoreFields = NULL;
	jint status, err;
	char* classname;

	err = (*gdata->jvmti)->GetClassStatus(gdata->jvmti, node->obj, &status);
    check_jvmti_error(gdata->jvmti, err, "get class status");
    if ((status & (JVMTI_CLASS_STATUS_INITIALIZED|JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_VERIFIED)) !=
    		(JVMTI_CLASS_STATUS_INITIALIZED|JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_VERIFIED))
    {
    	return;
    }

	classname = get_class_name(gdata->jvmti, jni, node->obj);
	sm_get(gdata->ignore_referenced_by, classname, (void**)&ignoreFields);
	if (NULL != ignoreFields)
	{
		node->ignore_fields = ignoreFields;
		sm_put(gdata->ignore_referenced_by, classname, NULL);
		while (NULL != ignoreFields)
		{
			ignoreFields->field = getFieldOffset(gdata->jvmti, jni, node->obj, ignoreFields->fieldName);
			debug("Field offset for [%s].[%s] is %d\n", classname, ignoreFields->fieldName, ignoreFields->field);
			ignoreFields = ignoreFields->next;
		}
	}
	myFree(classname);
}

static void tagAllClasses()
{
	jint err, count;
	int i, j;
	jclass *classes;
	ThreadData* tdata = getThreadData();
	JNIEnv* jni = tdata->jni;

    err = (*gdata->jvmti)->GetLoadedClasses(gdata->jvmti, &count, &classes);
    check_jvmti_error(gdata->jvmti, err, "get loaded classes");

    tdata->classNodes = myAlloc(count*sizeof(MemoryNode*)+1);

    for (i = 0, j = 0; i < count; i++)
    {
    	if (!(*jni)->CallBooleanMethod(jni, classes[i], tdata->metEquals, tdata->classClass))
    	{
    		MemoryNode* node = newMemoryNode();
    		node->obj = classes[i];
    		node->classNode = JNI_TRUE;
    		fillClassIgnoreList(jni, node);
    		err = (*gdata->jvmti)->SetTag(gdata->jvmti, classes[i], (jlong)(intptr_t)node);
    	    check_jvmti_error(gdata->jvmti, err, "set tag");
    		tdata->classNodes[j++] = node;
    	}
    	else
    	{
    		err = (*gdata->jvmti)->SetTag(gdata->jvmti, classes[i], (jlong)0L);
    	    check_jvmti_error(gdata->jvmti, err, "set tag");
    		(*jni)->DeleteLocalRef(jni, classes[i]);
    	}
    }
    tdata->classNodes[j] = NULL;

    deallocate(gdata->jvmti, classes);
}

static void untagAllClasses()
{
	jint err, count;
	int i;
	jclass *classes;
	ThreadData* tdata = getThreadData();
	JNIEnv* jni = tdata->jni;

	if (NULL == tdata->classNodes)
	{
		/* classes were'nt tagged */
		return;
	}
    err = (*gdata->jvmti)->GetLoadedClasses(gdata->jvmti, &count, &classes);
    check_jvmti_error(gdata->jvmti, err, "get loaded classes");

    for (i = 0; i < count; i++)
    {
    	(*gdata->jvmti)->SetTag(gdata->jvmti, classes[i], 0L);
    	(*jni)->DeleteLocalRef(jni, classes[i]);
    }
    deallocate(gdata->jvmti, classes);

    for (i = 0; NULL != tdata->classNodes[i]; i++)
    {
    	if (!freeMemoryNode(tdata->classNodes[i]))
    	{
    		char* classname = get_class_name(gdata->jvmti, jni, classes[i]);
    		alert("class not freed! %s remaining references to it: %d\n", classname, tdata->classNodes[i]->reference_pointing_to_me);
    		myFree(classname);
    	}
    }
    myFree(tdata->classNodes);
}


static void tagReferencesChain()
{
	jint err;
	jvmtiHeapCallbacks heap_callbacks;
	FollowReferencesData data;
	int i;
	bitmask_set* bms;
	int* nodes_found;

	tagAllClasses();

	memset(&data,0,sizeof(data));
	bms = bitMaskSet_new(gdata->numberOfLeaks);
	nodes_found = myAlloc(sizeof(*nodes_found)*gdata->numberOfLeaks);
	data.leaks_finished = bms;
	data.nodes_found = nodes_found;
    memset(&heap_callbacks,0,sizeof(heap_callbacks));
    heap_callbacks.heap_reference_callback = &cbObjectTagReferrer;

    for (i = 0; i < gdata->reference_chain_length; i++)
    {
    	int j, hasUnfinished = 0;
    	Timer timer;

    	startTimer(&timer, 1);

    	debug("Starting heap iteration number %d\n", i+1);
    	memset(nodes_found,0,sizeof(*nodes_found)*gdata->numberOfLeaks);
    	err = (*gdata->jvmti)->FollowReferences(gdata->jvmti, JVMTI_HEAP_FILTER_UNTAGGED|JVMTI_HEAP_FILTER_CLASS_UNTAGGED, NULL, NULL, &heap_callbacks, &data);
    	check_jvmti_error(gdata->jvmti, err, "follow references");
    	for (j = 0; j < gdata->numberOfLeaks; j++)
    	{
    		int contains = bitMapSet_contains(bms, j);
    		debug("leak #%d: reached static? %d, new nodes discovered: %d\n", j, contains, nodes_found[j]);
    		if (!(contains || (0 == nodes_found[j])))
    		{
    			hasUnfinished = 1;
        		break;
    		}
    	}

    	stopTimer(&timer, "Heap iteration");

    	if (!hasUnfinished)
    	{
    		debug("Finished root chain seek!\n");
    		break;
    	}
    }
    myFree(nodes_found);
	bitMaskSet_free(&bms);
}

jlong generateTagForClass(jvmtiEnv* jvmti, JNIEnv* jni_env, jclass theClass, jmethodID metGetEnclosingClass)
{
	jboolean boolResult;
	jobject enclosingClass;
	void* isIgnore = NULL;
	char* classname;
	ThreadData* tdata = getThreadData();
	int j;

	if ((*jni_env)->CallBooleanMethod(jni_env, theClass, tdata->metEquals, tdata->classClass))
	{
		return CLASS_CLASS_INITIAL_TAG;
	}

	if (JNI_OK != (*jvmti)->IsInterface(jvmti, theClass, &boolResult) || (boolResult != JNI_FALSE))
    {
    	return 0L;
    }
    if (JNI_OK != (*jvmti)->IsArrayClass(jvmti, theClass, &boolResult) || (boolResult != JNI_FALSE))
    {
    	return 0L;
    }
    enclosingClass = (*jni_env)->CallObjectMethod(jni_env, theClass, metGetEnclosingClass);
    if (NULL != enclosingClass)
    {
    	(*jni_env)->DeleteLocalRef(jni_env, enclosingClass);
    	return 0L;
    }

	classname = get_class_name(gdata->jvmti, jni_env, theClass);
	sm_get(gdata->ignore_classes, classname, &isIgnore);
	if (NULL != isIgnore)
	{
		debug("Ignoring class %s\n", classname);
		myFree(classname);
		return 0L;
	}
	myFree(classname);

    for (j = 0; j < gdata->sizeableClassesNum; j++)
    {
    	jclass klass = tdata->sizeableClasses[j].klass;
    	if ((NULL != klass) && (*jni_env)->IsAssignableFrom(jni_env, theClass, klass))
    	{
    		return INITIAL_CLASS_TAG + j;
    	}
    }
    return 0L;
}

void tagAllMapsAndCollections()
{
    jclass            *classes;
    jint               count, err;
    jmethodID metGetEnclosingClass;
    jvmtiHeapCallbacks heapCallbacks;

    jvmtiEnv* jvmti = gdata->jvmti;
    JNIEnv* jni_env = getThreadData()->jni;
    int i;
    Timer timer;

	memset(&heapCallbacks,0,sizeof(heapCallbacks));

	startTimer(&timer, 1);

    /* Get all the loaded classes */
    err = (*jvmti)->GetLoadedClasses(jvmti, &count, &classes);
    check_jvmti_error(jvmti, err, "get loaded classes");

    metGetEnclosingClass = (*jni_env)->GetMethodID(jni_env, getThreadData()->classClass, "getEnclosingClass", "()Ljava/lang/Class;");

    /* Setup an area to hold details about these classes */
    for ( i = 0 ; i < count ; i++ )
    {
        /* Tag this jclass */
        err = (*jvmti)->SetTag(jvmti, classes[i], generateTagForClass(jvmti, jni_env, classes[i], metGetEnclosingClass));
        check_jvmti_error(jvmti, err, "set object tag");
    	(*jni_env)->DeleteLocalRef(jni_env, classes[i]);
    }
    deallocate(jvmti, classes);

    stopTimer(&timer, "Mark classes");
	startTimer(&timer, 1);

    heapCallbacks.heap_reference_callback = &cbHeapObject;

    err = (*jvmti)->FollowReferences(jvmti, 0, NULL, NULL, &heapCallbacks, NULL);
    check_jvmti_error(jvmti, err, "follow references");

    stopTimer(&timer, "First heap iteration");
}

void findLeaksInTaggedObjects()
{
    jlong* tags;
    jobject* obj_ptr;
    jlong* tag_ptr;
    LeakCheckData data;
    jmethodID* sizeMethods;
    LeakingNodes* lst;
    jint err, count;
    int i;
    JNIEnv* jni_env = getThreadData()->jni;

    tags = myAlloc(sizeof(*tags)*gdata->sizeableClassesNum);
    for (i = 0; i < gdata->sizeableClassesNum; i++)
    {
    	tags[i] = INITIAL_CLASS_TAG + i;
    }

    sizeMethods = myAlloc(sizeof(*sizeMethods)*gdata->sizeableClassesNum);
    for (i = 0; i < gdata->sizeableClassesNum; i++)
    {
    	jclass klass = getThreadData()->sizeableClasses[i].klass;
    	if (NULL == klass)
    	{
    		sizeMethods[i] = NULL;
    	}
    	else
    	{
    		sizeMethods[i] = (*jni_env)->GetMethodID(jni_env, klass, "size", "()I");
    	}
    }

    err = (*gdata->jvmti)->GetObjectsWithTags(gdata->jvmti, gdata->sizeableClassesNum, tags, &count, &obj_ptr, &tag_ptr);
    check_jvmti_error(gdata->jvmti, err, "get objects with tags");

    data.obj_ptr = obj_ptr;
    data.tag_ptr = tag_ptr;
    data.count = count;
    data.sizeMethods = sizeMethods;
    lst = searchObjectsForLeaks(&data);
    deallocate(gdata->jvmti, obj_ptr);
    deallocate(gdata->jvmti, tag_ptr);

    if (NULL != lst)
    {
    	tagReferencesChain();
    	printReferencesChainForLeakingNodes(lst, (gdata->reference_chain_length > 0));
    	freeMemoryForLeakList(lst);
    }
    myFree(sizeMethods);
    myFree(tags);
   	untagAllClasses();
}

LeakingNodes* searchObjectsForLeaks(LeakCheckData* data)
{
	int i;
	JNIEnv* env = getThreadData()->jni;
	LeakingNodes* res = NULL, *last = NULL;
	for (i = 0; i < data->count ; i++)
	{
		jint size = 0;
		object_print_function fn = NULL;
		int idx = data->tag_ptr[i] - INITIAL_CLASS_TAG;
		if ((idx >= 0) && (idx < gdata->sizeableClassesNum))
		{
			size = (*env)->CallIntMethod(env, data->obj_ptr[i], data->sizeMethods[idx]);
			fn = gdata->sizeableClasses[idx].print_fn;
		}
		(*env)->ExceptionClear(env);
		if (size > gdata->size_threshold)
		{
			LeakingNodes* n = myAlloc(sizeof(*n));
			memset(n, 0, sizeof(*n));
			n->leakNumber = gdata->numberOfLeaks;
			debug("leak #%d is of size %d\n", n->leakNumber, (int)size);
			gdata->numberOfLeaks++;
			n->node = newMemoryNode();
			bitMapSet_add(n->node->leaks_related, n->leakNumber);
			if (NULL == last)
			{
				res = n;
				last = n;
			}
			else
			{
				last->next = n;
				last = n;
			}
			n->print_fn = fn;
			n->node->obj = data->obj_ptr[i];
			n->node->leak_size = size;
			n->leak_size = size;
			(*gdata->jvmti)->SetTag(gdata->jvmti, data->obj_ptr[i], (jlong)(intptr_t)n->node);
		}
		else
		{
			(*gdata->jvmti)->SetTag(gdata->jvmti, data->obj_ptr[i], 0L);
			(*env)->DeleteLocalRef(env, data->obj_ptr[i]);
		}
	}

	return res;
}

static jint JNICALL
findJniLocalsLeak(jvmtiHeapReferenceKind reference_kind,
	 const jvmtiHeapReferenceInfo* reference_info, __UNUSED__ jlong class_tag,
	 __UNUSED__ jlong referrer_class_tag, __UNUSED__ jlong size,
     __UNUSED__ jlong* tag_ptr, __UNUSED__ jlong* referrer_tag_ptr, __UNUSED__ jint length, void* user_data)
{
	if (JVMTI_HEAP_REFERENCE_JNI_LOCAL == reference_kind)
	{
		SelfLeakCheckData* data = (SelfLeakCheckData*)user_data;
		if (reference_info->jni_local.thread_id == data->thread_id)
		{
			data->jniLocalRefs++;
		}
	}
    return JVMTI_VISIT_OBJECTS;
}

void selfCheck()
{
	jvmtiHeapCallbacks heap_callbacks;
	Timer t;
	jint err;
	SelfLeakCheckData data;

    memset(&data,0,sizeof(data));
    memset(&heap_callbacks,0,sizeof(heap_callbacks));
    heap_callbacks.heap_reference_callback = &findJniLocalsLeak;
    data.thread_id = getThreadData()->thread_id;

    debug("Starting self check\n");
    startTimer(&t, 1);
	err = (*gdata->jvmti)->FollowReferences(gdata->jvmti, 0, NULL, NULL, &heap_callbacks, &data);

	stopTimer(&t, "Self check");
	if (data.jniLocalRefs > 0)
	{
		alert("Found %d leaking JNI local references\n", data.jniLocalRefs);
	}
}

