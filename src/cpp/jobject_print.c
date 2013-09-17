#include "jobject_print.h"
#include "data_struct.h"
#include "agent_util.h"
#include "allocator.h"

extern GlobalData* gdata;

#define COLLECTION_OFFSET 0
#define MAP_OFFSET 1
#define MULTIMAP_OFFSET 2

#define FIND_CLASS(offset) (getThreadData()->sizeableClasses[offset].klass)

static void printCollectionUsingMethod(jobject collection, void (*printSingleObject)(jobject, jmethodID, void*), void* user_data)
{
	jmethodID metIterator, metHasNext, metNext, metToString;
	jclass classIterator, classObject;
	jobject iterator;
	JNIEnv* env = getThreadData()->jni;
	int limit = gdata->num_elements_to_dump;


	metIterator = (*env)->GetMethodID(env, FIND_CLASS(COLLECTION_OFFSET), "iterator", "()Ljava/util/Iterator;");
	classIterator = (*env)->FindClass(env, "java/util/Iterator");
	classObject = (*env)->FindClass(env, "java/lang/Object");
	metHasNext = (*env)->GetMethodID(env, classIterator, "hasNext", "()Z");
	metNext = (*env)->GetMethodID(env, classIterator, "next", "()Ljava/lang/Object;");
	metToString = (*env)->GetMethodID(env, classObject, "toString", "()Ljava/lang/String;");

	iterator = (*env)->CallObjectMethod(env, collection, metIterator);
	while ((limit-- > 0) && (JNI_FALSE != (*env)->CallBooleanMethod(env, iterator, metHasNext)))
	{
		jobject obj = (*env)->CallObjectMethod(env, iterator, metNext);

		(*printSingleObject)(obj, metToString, user_data);

		(*env)->DeleteLocalRef(env, obj);
	}
	(*env)->DeleteLocalRef(env, iterator);
}

void printNormalObject(jobject obj, jmethodID metToString, __UNUSED__ void* user_data)
{
	const char* utf = NULL;
	jstring objString = NULL;
	JNIEnv* env = getThreadData()->jni;

	if (NULL != obj)
	{
		objString = (jstring)(*env)->CallObjectMethod(env, obj, metToString);
	}

	if (NULL != objString)
	{
		utf = (*env)->GetStringUTFChars(env, objString, NULL);
	}

	complete_xml_element("collection-element", "value", utf, NULL);

	if (NULL != utf)
	{
		(*env)->ReleaseStringUTFChars(env, objString, utf);
	}
	if (NULL != objString)
	{
		(*env)->DeleteLocalRef(env, objString);
	}
}

void printEntryObject(jobject obj, jmethodID metToString, void* user_data)
{
	struct entryMethods* entryMet = (struct entryMethods*)user_data;
	JNIEnv* env = getThreadData()->jni;
	jobject key, value;
	jstring objKeyString = NULL, objValueString = NULL;
	const char* utfKey = NULL, *utfValue = NULL;

	key = (*env)->CallObjectMethod(env, obj, entryMet->getKey);
	value = (*env)->CallObjectMethod(env, obj, entryMet->getValue);

	if (NULL != key)
	{
		objKeyString = (jstring)(*env)->CallObjectMethod(env, key, metToString);
	}
	if (NULL != value)
	{
		objValueString = (jstring)(*env)->CallObjectMethod(env, value, metToString);
	}

	if (NULL != objKeyString)
	{
		utfKey = (*env)->GetStringUTFChars(env, objKeyString, NULL);
	}
	if (NULL != objValueString)
	{
		utfValue = (*env)->GetStringUTFChars(env, objValueString, NULL);
	}

	complete_xml_element("map-element", "key", utfKey, "value", utfValue, NULL);

	if (NULL != utfKey)
	{
		(*env)->ReleaseStringUTFChars(env, objKeyString, utfKey);
	}
	if (NULL != utfValue)
	{
		(*env)->ReleaseStringUTFChars(env, objValueString, utfValue);
	}
	if (NULL != objKeyString)
	{
		(*env)->DeleteLocalRef(env, objKeyString);
	}
	if (NULL != objValueString)
	{
		(*env)->DeleteLocalRef(env, objValueString);
	}
	if (NULL != key)
	{
		(*env)->DeleteLocalRef(env, key);
	}
	if (NULL != value)
	{
		(*env)->DeleteLocalRef(env, value);
	}
}

void printCollection(jobject collection)
{
	open_xml_element("collection", NULL);
	printCollectionUsingMethod(collection, &printNormalObject, NULL);
	close_xml_element("collection");
}

void printMap(jobject map)
{
	JNIEnv* env = getThreadData()->jni;
	jmethodID metEntrySet;
	jclass classEntry;
	jobject entrySet;
	struct entryMethods entryMet;

	classEntry = (*env)->FindClass(env, "java/util/Map$Entry");
	metEntrySet = (*env)->GetMethodID(env, FIND_CLASS(MAP_OFFSET), "entrySet", "()Ljava/util/Set;");
	entryMet.getKey = (*env)->GetMethodID(env, classEntry, "getKey", "()Ljava/lang/Object;");
	entryMet.getValue = (*env)->GetMethodID(env, classEntry, "getValue", "()Ljava/lang/Object;");
	entrySet = (*env)->CallObjectMethod(env, map, metEntrySet);
	open_xml_element("map", NULL);
	if (NULL == entrySet)
	{
		complete_xml_element("error", "message", "Failed to invoke entrySet() on this map", NULL);
		(*env)->ExceptionClear(env);
	}
	else
	{
		printCollectionUsingMethod(entrySet, &printEntryObject, &entryMet);
	}
	close_xml_element("map");
	(*env)->DeleteLocalRef(env, entrySet);
	(*env)->DeleteLocalRef(env, classEntry);
}

void printMultiMap(jobject map)
{
	JNIEnv* env = getThreadData()->jni;
	jmethodID metEntries;
	jclass classEntry;
	jobject entries;
	struct entryMethods entryMet;

	classEntry = (*env)->FindClass(env, "java/util/Map$Entry");
	metEntries = (*env)->GetMethodID(env, FIND_CLASS(MULTIMAP_OFFSET), "entries", "()Ljava/util/Collection;");
	entryMet.getKey = (*env)->GetMethodID(env, classEntry, "getKey", "()Ljava/lang/Object;");
	entryMet.getValue = (*env)->GetMethodID(env, classEntry, "getValue", "()Ljava/lang/Object;");
	entries = (*env)->CallObjectMethod(env, map, metEntries);
	open_xml_element("multi-map", NULL);
	if (NULL == entries)
	{
		complete_xml_element("error", "message", "Failed to invoke entries() on this multi-map", NULL);
		(*env)->ExceptionClear(env);
	}
	else
	{
		printCollectionUsingMethod(entries, &printEntryObject, &entryMet);
	}
	close_xml_element("multi-map");
	(*env)->DeleteLocalRef(env, entries);
	(*env)->DeleteLocalRef(env, classEntry);
}

static SizeableClassDescriptor classDescriptors[] =
{
		{"java/util/Collection", &printCollection},
		{"java/util/Map", &printMap},
		{"com/google/common/collect/Multimap", &printMultiMap}
};

void initClassesToCheck()
{
	gdata->sizeableClassesNum = sizeof(classDescriptors)/sizeof(classDescriptors[0]);
	gdata->sizeableClasses = classDescriptors;
}

