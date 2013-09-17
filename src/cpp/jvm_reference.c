#include "jvm_reference.h"
#include "allocator.h"
#include "agent_util.h"
#include <stdint.h>
#include <classfile_constants.h>

static const char* UNKNOWN_FIELD_NAME = "<Unknown>";

extern GlobalData* gdata;

static void fillClassInMemoryNode(MemoryNode* node)
{
	JNIEnv* env = getThreadData()->jni;

	if (NULL == node)
	{
		fatal_error("node is null at %s:%d\n",__FILE__,__LINE__);
	}
	if (JNI_FALSE != node->dead)
	{
		return;
	}
	if (NULL != node->klass)
	{
		return;
	}
	if (NULL == node->obj)
	{
		jobject* obj_ptr = NULL;
		jlong* tag_ptr = NULL;
		jint count, err;
		jlong tag = (jlong)(intptr_t)node;

		err = (*gdata->jvmti)->GetObjectsWithTags(gdata->jvmti, 1, &tag, &count, &obj_ptr, &tag_ptr);
		check_jvmti_error(gdata->jvmti, err, "get objects with tags");

		if (0 == count)
		{
			node->dead = JNI_TRUE;
			if (NULL != obj_ptr) deallocate(gdata->jvmti, obj_ptr);
			if (NULL != tag_ptr) deallocate(gdata->jvmti, tag_ptr);
			return;
		}
		if (1 != count)
		{
			fatal_error("Count from GetObjectsWithTags is %d\n", (int)count);
		}
		node->obj = *obj_ptr;
		deallocate(gdata->jvmti, obj_ptr);
		deallocate(gdata->jvmti, tag_ptr);
	}
	node->klass = (*env)->GetObjectClass(env, node->obj);

	node->classname = get_class_name(gdata->jvmti, env, node->klass);
}

static jboolean findInterfaceInList(InterfaceList* lstIntr, jclass intr)
{
	InterfaceList** last = &lstIntr->next;
	InterfaceList* iter = *last;
	jmethodID metEquals = getThreadData()->metEquals;
	JNIEnv* env = getThreadData()->jni;
	while (NULL != iter)
	{
		if ((*env)->CallBooleanMethod(env, iter->intr, metEquals, intr))
		{
			(*env)->DeleteLocalRef(env, intr);
			return JNI_TRUE;
		}
		last = &iter->next;
		iter = *last;
	}
	*last = myAlloc(sizeof(**last));
	memset(*last, 0, sizeof(**last));
	(*last)->intr = intr;
	return JNI_FALSE;
}

static void removeImplementedInterfacesFromIndex(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, jint* idx, InterfaceList* lstIntr)
{
	jint intr_count, err;
	jclass *intr_ptr = NULL;
	int i;

	err = (*jvmti)->GetImplementedInterfaces(jvmti, klass, &intr_count, &intr_ptr);
	check_jvmti_error(jvmti, err, "get implemented interfaces");

	for (i = 0; i < intr_count; i++)
	{
		jint field_cnt;
		jfieldID* dummy = NULL;
		if (findInterfaceInList(lstIntr, intr_ptr[i]))
		{
			continue;
		}
		removeImplementedInterfacesFromIndex(jvmti, env, intr_ptr[i], idx, lstIntr);
		err = (*jvmti)->GetClassFields(jvmti, intr_ptr[i], &field_cnt, &dummy);
		check_jvmti_error(jvmti, err, "get class fields");

		deallocate(jvmti, dummy);
		*idx -= field_cnt;
	}
	deallocate(jvmti, intr_ptr);
}

jboolean getFieldOffsetInternal(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, char* name, jint* idx, InterfaceList* lstIntr)
{
	jint tmp_idx = 0, count, err;
	jclass super;
	jfieldID* all_fields = NULL;
	int i;

	removeImplementedInterfacesFromIndex(jvmti, env, klass, &tmp_idx, lstIntr);
	*idx = *idx-tmp_idx;

	super = (*env)->GetSuperclass(env, klass);
	if (NULL != super)
	{
		if (getFieldOffsetInternal(jvmti, env, super, name, idx, lstIntr))
		{
			return JNI_TRUE;
		}
	}

	err = (*jvmti)->GetClassFields(jvmti, klass, &count, &all_fields);
	check_jvmti_error(jvmti, err, "get class fields");
	for (i = 0; i < count; i++)
	{
		char* fieldname;
		err = (*jvmti)->GetFieldName(jvmti, klass, all_fields[i], &fieldname, NULL, NULL);
		check_jvmti_error(jvmti, err, "get field name");
		if (0 == strcmp(name, fieldname))
		{
			deallocate(jvmti, fieldname);
			deallocate(jvmti, all_fields);
			*idx += i;
			return JNI_TRUE;
		}
		deallocate(jvmti, fieldname);
	}
	deallocate(jvmti, all_fields);
	*idx += count;
	return JNI_FALSE;
}

static void freeInterfaceList(JNIEnv* env, InterfaceList* lstIntr)
{
	while (NULL != lstIntr)
	{
		InterfaceList* next = lstIntr->next;
		if (0L != (jlong)(intptr_t)lstIntr->intr)
		{
			(*env)->DeleteLocalRef(env, lstIntr->intr);
		}
		myFree(lstIntr);
		lstIntr = next;
	}
}

jint getFieldOffset(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, char* name)
{
	InterfaceList* lstIntr = myAlloc(sizeof(*lstIntr));
	jint idx = 0;
	jboolean res;
	memset(lstIntr, 0, sizeof(*lstIntr));
	res = getFieldOffsetInternal(jvmti, env, klass, name, &idx, lstIntr);
	freeInterfaceList(env, lstIntr);
	if (res)
	{
		return idx;
	}
	return -1;
}

static char* getFieldNameByIndexInternal(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, jint* idx, InterfaceList* lstIntr)
{
	jfieldID* all_fields, fieldID = NULL;
	jint count, err;
	jclass super;
	char* field;

	removeImplementedInterfacesFromIndex(jvmti, env, klass, idx, lstIntr);
	super = (*env)->GetSuperclass(env, klass);
	if (NULL != super)
	{
		field = getFieldNameByIndexInternal(jvmti, env, super, idx, lstIntr);
		if (NULL != field)
		{
			return field;
		}
	}

	err = (*jvmti)->GetClassFields(jvmti, klass, &count, &all_fields);
	check_jvmti_error(jvmti, err, "get class fields");
	if (*idx < 0)
	{
		alert("Index is %d, count is %d at %s:%d\n", *idx, count, __FILE__, __LINE__);
		return NULL;
	}
	if (*idx < count)
	{
		fieldID = all_fields[(int)*idx];
	}
	deallocate(jvmti, all_fields);

	if (NULL != fieldID)
	{
		char* fieldname;
		err = (*jvmti)->GetFieldName(jvmti, klass, fieldID, &fieldname, NULL, NULL);
		check_jvmti_error(jvmti, err, "get field name");
		field = myStrdup(fieldname);

		deallocate(jvmti, fieldname);
		return field;
	}
	*idx -= count;
	return NULL;
}

static char* getFieldNameByIndex(jvmtiEnv* jvmti, JNIEnv* env, jclass klass, jint* idx)
{
	InterfaceList* lstIntr = myAlloc(sizeof(*lstIntr));
	char* res;
	memset(lstIntr, 0, sizeof(*lstIntr));
	res = getFieldNameByIndexInternal(jvmti, env, klass, idx, lstIntr);
	freeInterfaceList(env, lstIntr);
	return res;
}

static void printSingleReference(jvmtiEnv* jvmti, JNIEnv* env, MemoryReferer *ref)
{
	int sz;
	jint err, idx;
	char* msg = NULL;

	if (NULL != ref->node)
	{
		fillClassInMemoryNode(ref->node);
	}
	switch (ref->kind)
	{
	case JVMTI_HEAP_REFERENCE_STATIC_FIELD:
	{
		char* fieldname;
		char* classname = get_class_name(jvmti, env, ref->node->obj);
		int freeFieldname = 1;

		idx = ref->info.field.index;
		fieldname = getFieldNameByIndex(jvmti, env, ref->node->obj, &idx);
		if (NULL == fieldname)
		{
			freeFieldname = 0;
			fieldname = (char*)UNKNOWN_FIELD_NAME;
		}

		sz = strlen(classname) + strlen(fieldname) + 32;
		msg = (char*)myAlloc(sizeof(*msg) * sz);

		snprintf(msg, sz, "%s.%s (Static)", classname, fieldname);
		if (freeFieldname)
		{
			myFree(fieldname);
		}
		myFree(classname);
	}
	break;
	case JVMTI_HEAP_REFERENCE_FIELD:
	{
		char* fieldname;
		int freeFieldname = 1;

		idx = ref->info.field.index;
		fieldname = getFieldNameByIndex(jvmti, env, ref->node->klass, &idx);
		if (NULL == fieldname)
		{
			freeFieldname = 0;
			fieldname = (char*)UNKNOWN_FIELD_NAME;
		}

		sz = strlen(ref->node->classname) + strlen(fieldname) + 32;
		msg = (char*)myAlloc(sizeof(*msg) * sz);

		snprintf(msg, sz, "%s.%s", ref->node->classname, fieldname);
		if (freeFieldname)
		{
			myFree(fieldname);
		}
	}
	break;
	case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT:
	{
		idx = ref->info.array.index;
		sz = 48 + strlen(ref->node->classname);
		msg = (char*)myAlloc(sizeof(*msg) * sz);
		snprintf(msg, sz, "%s element number %d", ref->node->classname, (int)idx);
	}
	break;
	case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
	{
		idx = ref->info.constant_pool.index;
		sz = 16;
		msg = (char*)myAlloc(sizeof(*msg) * sz);
		snprintf(msg, sz, "#%d", (int)idx);
	}
	break;
	case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
	{
		char* methodName;
		char* methodDesc;
		char* classname;
		jint modifiers, slot = ref->info.stack_local.slot;
		jclass myClass;
		jmethodID method = ref->info.stack_local.method;
		long threadID = ref->info.stack_local.thread_id;

		err = (*jvmti)->GetMethodDeclaringClass(jvmti, method, &myClass);
		check_jvmti_error(jvmti, err, "GetMethodDeclaringClass");

		err = (*jvmti)->GetMethodModifiers(jvmti, method, &modifiers);
		check_jvmti_error(jvmti, err, "GetMethodModifiers");

		classname = get_class_name(jvmti, env, myClass);

		err = (*jvmti)->GetMethodName(jvmti, method, &methodName, &methodDesc, NULL);
		check_jvmti_error(jvmti, err, "get method name");

		sz = 64 + strlen(methodDesc) + strlen(methodName) + strlen(classname);
		msg = myAlloc(sizeof(*msg) * sz);
		if (!(modifiers&JVM_ACC_STATIC) && (0 == slot))
		{
			snprintf(msg, sz, "'this' reference on invocation of method %s.%s%s (thread ID %lx)", classname, methodName, methodDesc, threadID);
		}
		else
		{
			jint argSize;
			(*jvmti)->GetArgumentsSize(jvmti, method, &argSize);
			check_jvmti_error(jvmti, err, "GetArgumentsSize");
			if (slot < argSize)
			{
				snprintf(msg, sz, "Argument number %d for method invocation of %s.%s%s (thread ID %lx)", (int)slot, classname, methodName, methodDesc, threadID);
			}
			else
			{
				slot -= argSize;
				snprintf(msg, sz, "Local variable from method %s.%s%s (thread ID %lx, local arg #%d)", classname, methodName, methodDesc, threadID, (int)slot);
			}
		}
		(*env)->DeleteLocalRef(env, myClass);
		deallocate(jvmti, methodName);
		deallocate(jvmti, methodDesc);
		myFree(classname);
	}
	break;
	case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
	{
		char* methodName;
		char* methodDesc;
		jmethodID metID = ref->info.jni_local.method;
		long threadID = (long)ref->info.jni_local.thread_id;

		if (0 == metID)
		{
			sz = 64;
			msg = myAlloc(sizeof(*msg) * sz);
			snprintf(msg, sz, "JNI Local variable from unknown method (thread ID %lx)", threadID);
		}
		else
		{
			err = (*jvmti)->GetMethodName(jvmti, metID, &methodName, &methodDesc, NULL);
			check_jvmti_error(jvmti, err, "get method name (jni)");

			sz = 64 + strlen(methodDesc) + strlen(methodName);
			msg = myAlloc(sizeof(*msg) * sz);
			snprintf(msg, sz, "JNI Local variable from method %s%s (thread id %lx)", methodName, methodDesc, threadID);
			deallocate(jvmti, methodName);
			deallocate(jvmti, methodDesc);
		}
	}
	case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
	{
		msg = myStrdup("JNI global reference");
	}
	break;
	case JVMTI_HEAP_REFERENCE_SYSTEM_CLASS:
	{
		msg = myStrdup("System class heap root reference");
	}
	break;
	case JVMTI_HEAP_REFERENCE_MONITOR:
	{
		msg = myStrdup("Monitor heap root reference");
	}
	break;
	case JVMTI_HEAP_REFERENCE_THREAD:
	{
		msg = myStrdup("Thread heap root reference");
	}
	break;
	case JVMTI_HEAP_REFERENCE_OTHER:
	{
		msg = myStrdup("Unknown heap root reference");
	}
	break;
	default:
		break;
	}

	if (NULL != msg)
	{
		complete_xml_element("reference", "location", msg, NULL);
		myFree(msg);
	}
}

static OrderedReferences* generateReferencesChainForNode(jvmtiEnv* jvmti, JNIEnv* env, MemoryNode* node, int leakNumber)
{
	OrderedReferences* orderedRefs = NULL;
	if (node->visited != JNI_FALSE)
	{
		return NULL;
	}
	if (!bitMapSet_contains(node->leaks_related, leakNumber))
	{
		return NULL;
	}
	node->visited = JNI_TRUE;
	fillClassInMemoryNode(node);
	if (JNI_FALSE == node->dead)
	{
		MemoryReferer* ref = node->start;
		while (NULL != ref)
		{
			if (JNI_FALSE == shouldConsiderThisReference(ref->kind))
			{
				//do nothing
			}
			else if (JNI_FALSE == isRootReference(ref->kind))
			{
				if (NULL == ref->node)
				{
					fatal_error("Node is null. kind is %d\n", ref->kind);
				}
				orderedRefs = generateReferencesChainForNode(jvmti, env, ref->node, leakNumber);
				if (NULL == orderedRefs)
				{
					//we didn't find any reference chain to root in the context of this leak - lets not scan it again
					bitMapSet_remove(node->leaks_related, leakNumber);
				}
				else
				{
					OrderedReferences* first = orderedRefs->next;

					orderedRefs->next = (OrderedReferences*)myAlloc(sizeof(*orderedRefs));
					orderedRefs->next->next = first;
					orderedRefs->next->ref = ref;
					break;
				}
			}
			else
			{
				orderedRefs = (OrderedReferences*)myAlloc(sizeof(*orderedRefs));
				orderedRefs->next = orderedRefs;
				orderedRefs->ref = ref;
				break;
			}

			ref = ref->next;
		}
	}
	node->visited = JNI_FALSE;
	return orderedRefs;
}


static void printAllReferencesToNode(jvmtiEnv* jvmti, JNIEnv* env, MemoryNode* node)
{
	MemoryReferer *ref = node->start;

	open_xml_element("references", NULL);
	while (NULL != ref)
	{
		printSingleReference(jvmti, env, ref);
		ref = ref->next;
	}
	close_xml_element("references");
}

static void printReferenceChainToNode(jvmtiEnv* jvmti, JNIEnv* env, OrderedReferences* orderedRefs)
{
	OrderedReferences* last = orderedRefs;
	OrderedReferences* next = orderedRefs->next;
	open_xml_element("reference-chain-to-root", NULL);
	do
	{
		orderedRefs = next;
		next = orderedRefs->next;
		printSingleReference(jvmti, env, orderedRefs->ref);
		myFree(orderedRefs);
	}
	while (orderedRefs != last);
	close_xml_element("reference-chain-to-root");
}


void printReferencesChainForLeakingNodes(LeakingNodes* lstLeaks, int isNeedReferences)
{
	LeakingNodes* leak = lstLeaks;
	jvmtiEnv* jvmti = gdata->jvmti;
	JNIEnv* env = getThreadData()->jni;

	while (NULL != leak)
	{
		char leakSizeStr[32];
		MemoryNode* n = leak->node;

		OrderedReferences* orderedRefs = NULL;
		if (isNeedReferences)
		{
			orderedRefs = generateReferencesChainForNode(jvmti, env, n, leak->leakNumber);
		}
		if (NULL != orderedRefs || gdata->show_unreachables)
		{
			fillClassInMemoryNode(n);
			snprintf(leakSizeStr, sizeof(leakSizeStr), "%d", leak->leak_size);
			open_xml_element("leaking-object","class", n->classname, "size", leakSizeStr, NULL);
			(*leak->print_fn)(n->obj);
			if (isNeedReferences)
			{
				if (NULL == orderedRefs)
				{
					printAllReferencesToNode(jvmti, env, n);
				}
				else
				{
					printReferenceChainToNode(jvmti, env, orderedRefs);
				}
			}
			close_xml_element("leaking-object");
		}
		else
		{
			debug("jleaker: ignore leak in class %s\n", n->classname);
		}
		leak = leak->next;
	}
}

jboolean localReferenceOfThisThread(jvmtiHeapReferenceKind kind, const jvmtiHeapReferenceInfo* info)
{
	switch (kind)
	{
	case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
		return info->stack_local.thread_id == getThreadData()->thread_id;
	case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
		return info->jni_local.thread_id == getThreadData()->thread_id;
	default:
		return JNI_FALSE;
	}

}

jboolean shouldConsiderThisReference(jvmtiHeapReferenceKind kind)
{
	switch (kind)
	{
	case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
	case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
		return gdata->consider_local_references;
	default:
		return JNI_TRUE;
	}
}

jboolean isRootReference(jvmtiHeapReferenceKind kind)
{
	switch (kind)
	{
	case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
	case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
	case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
	case JVMTI_HEAP_REFERENCE_STATIC_FIELD:
	case JVMTI_HEAP_REFERENCE_MONITOR:
	case JVMTI_HEAP_REFERENCE_THREAD:
	case JVMTI_HEAP_REFERENCE_OTHER:
	case JVMTI_HEAP_REFERENCE_SYSTEM_CLASS:
	case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
		return JNI_TRUE;
	default:
		return JNI_FALSE;
	}
}

