/*
 * %W% %E%
 * 
 * Copyright (c) 2006, Oracle and/or its affiliates. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * -Redistribution of source code must retain the above copyright notice, this
 *  list of conditions and the following disclaimer.
 * 
 * -Redistribution in binary form must reproduce the above copyright notice, 
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 * 
 * Neither the name of Oracle or the names of contributors may 
 * be used to endorse or promote products derived from this software without 
 * specific prior written permission.
 * 
 * This software is provided "AS IS," without a warranty of any kind. ALL 
 * EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES, INCLUDING
 * ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED. SUN MICROSYSTEMS, INC. ("SUN")
 * AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE
 * AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS
 * DERIVATIVES. IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST 
 * REVENUE, PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, 
 * INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY 
 * OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE, 
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * You acknowledge that this software is not designed, licensed or intended
 * for use in the design, construction, operation or maintenance of any
 * nuclear facility.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#ifdef WIN32
#	include <time.h>
#else
#	include <sys/time.h>
#endif

#include "jvmti.h"
#include "allocator.h"
#include "agent_util.h"
#include "bitmask_set.h"
#include "data_struct.h"
#include "leak_detect.h"
#include "jobject_print.h"
#include "ini.h"

GlobalData globalData, *gdata = &globalData;

/* Enter agent monitor protected section */
static void
enterAgentMonitor(jvmtiEnv *jvmti)
{
    jvmtiError err;
    
    err = (*jvmti)->RawMonitorEnter(jvmti, gdata->lock);
    check_jvmti_error(jvmti, err, "raw monitor enter");
}

/* Exit agent monitor protected section */
static void
exitAgentMonitor(jvmtiEnv *jvmti)
{
    jvmtiError err;
    
    err = (*jvmti)->RawMonitorExit(jvmti, gdata->lock);
    check_jvmti_error(jvmti, err, "raw monitor exit");
}


/* Callback for JVMTI_EVENT_VM_DEATH */
static void JNICALL 
vmDeath(jvmtiEnv *jvmti, __UNUSED__ JNIEnv *env)
{
    /* Disable events and dump the heap information */
    enterAgentMonitor(jvmti); {

        gdata->vmDeathCalled = JNI_TRUE;

    } exitAgentMonitor(jvmti);
}


static int ini_handler(__UNUSED__ void* userData, const char* section, const char* name, const char* value)
{
	if (0 == strcmp(section, "ignore_classes"))
	{
		sm_put(gdata->ignore_classes, name, (void*)1);
	}
	else if (0 == strcmp(section, "ignore_referenced_by"))
	{
		char* endptr, *classname;
		IgnoreField* old = NULL;
		int v = strtol(value, &endptr, 10);
		int idx, nameLen;
       	IgnoreField* newElement;

       	if (*endptr != '\0')
       	{
       		alert("Error: Bad value for [%s] %s which is (%s)\n", section, name, value);
       		return 0;
       	}
       	nameLen = strlen(name);
       	for (idx = nameLen; idx >= 0 && name[idx] != '.'; idx--);
       	if (idx < 0)
       	{
       		alert("Error: Cannot find class name for %s\n", name);
       		return 0;
       	}
       	if (nameLen-idx == 1)
       	{
       		alert("Error: Cannot find field name for %s\n", name);
       		return 0;
       	}
		newElement = (IgnoreField*)myAlloc(sizeof(IgnoreField));
       	memset(newElement, 0, sizeof(*newElement));
       	newElement->fieldName = (char*)myAlloc(nameLen-idx);
       	strcpy(newElement->fieldName, name+idx+1);
       	classname = (char*)myAlloc(idx + 1);
       	strncpy(classname, name, idx);
       	classname[idx] = 0;
       	newElement->threshold = v;

    	debug("Parsing class name [%s], field [%s], limit %d\n", classname, newElement->fieldName, v);

       	sm_get(gdata->ignore_referenced_by, classname, (void**)&old);
       	newElement->next = old;
		sm_put(gdata->ignore_referenced_by, classname, newElement);
		myFree(classname);
	}
	return 1;
}

static void free_class_limit(__UNUSED__ void* ptr)
{
}

static void free_referenced_by_limit(void* ptr)
{
	IgnoreField* e = (IgnoreField*)ptr;
	while (e != NULL)
	{
		IgnoreField* next = e->next;
		myFree(e->fieldName);
		myFree(e);
		e = next;
	}
}

static int parse_agent_options(char *options)
{
    char *next;
    char *all_conf_files = NULL;

    gdata->tcp_port = 0;
    gdata->size_threshold = 500;
    gdata->reference_chain_length = 0;
    gdata->max_fan_in = 5;
	gdata->debug = 0;
	gdata->self_check = 0;
	gdata->num_elements_to_dump = 5;
	gdata->run_gc = JNI_TRUE;
	gdata->show_unreachables = JNI_FALSE;
	gdata->ignore_classes = sm_new(10, free_class_limit);
	gdata->ignore_referenced_by = sm_new(10, free_referenced_by_limit);

    /* Parse options and set flags in gdata */
    if ( options==NULL )
    {
        return 1;
    }

    alert("Start memory leak detection (options are %s)\n", options);

    next = strtok(options, ",=");

    /* While not at the end of the options string, process this option. */
    while ( next != NULL )
    {
    	if (strcmp(next,"tcp_port") == 0) {
    		char *endptr;
        	next = strtok(NULL, ",");
        	gdata->tcp_port = strtol(next, &endptr, 10);
        	if (*endptr != '\0')
        	{
        		alert("Error: Bad TCP port %s\n", next);
        		return 0;
        	}
        	debug("jleaker: Using tcp_port=%d\n", gdata->tcp_port);
    	}
    	else if (strcmp(next,"size_threshold") == 0)
    	{
    		char *endptr;
        	next = strtok(NULL, ",");
        	gdata->size_threshold = strtol(next, &endptr, 10);
        	if (*endptr != '\0')
        	{
        		alert("Error: Bad size_threshold %s\n", next);
        		return 0;
        	}
        	debug("jleaker: Using size_threshold=%d\n", gdata->size_threshold);
    	}
    	else if (strcmp(next,"reference_chain_length") == 0)
    	{
    		char *endptr;
        	next = strtok(NULL, ",");
        	gdata->reference_chain_length = strtol(next, &endptr, 10);
        	if (*endptr != '\0')
        	{
        		alert("Error: Bad reference_chain_length %s\n", next);
        		return 0;
        	}
        	debug("jleaker: Using reference_chain_length=%d\n", gdata->reference_chain_length);
    	}
    	else if (strcmp(next,"max_fan_in") == 0)
    	{
    		char *endptr;
        	next = strtok(NULL, ",");
        	gdata->max_fan_in = strtol(next, &endptr, 10);
        	if (*endptr != '\0')
        	{
        		alert("Error: Bad max_fan_in %s\n", next);
        		return 0;
        	}
        	debug("jleaker: Using max_fan_in=%d\n", gdata->max_fan_in);
    	}
    	else if (strcmp(next,"debug") == 0)
    	{
    		gdata->debug = 1;
        	debug("jleaker: Using debug=true\n");
    	}
    	else if (strcmp(next,"self_check") == 0)
    	{
    		gdata->self_check = 1;
        	debug("jleaker: Using self_check=true\n");
    	}
    	else if (strcmp(next,"no_gc") == 0)
    	{
    		gdata->run_gc = JNI_FALSE;
        	debug("jleaker: Using no_gc=true\n");
    	}
    	else if (strcmp(next,"consider_local_references") == 0)
    	{
    		gdata->consider_local_references = JNI_TRUE;
        	debug("jleaker: Using consider_local_references=true\n");
    	}
    	else if (strcmp(next,"conf_file") == 0)
    	{
    		all_conf_files = strtok(NULL, ",");
        	debug("jleaker: Using conf_file=%s\n", all_conf_files);
    	}
    	else if (strcmp(next,"show_unreachables") == 0)
    	{
    		gdata->show_unreachables = JNI_TRUE;
        	debug("jleaker: Using show_unreachables=true\n");
    	}
    	else
    	{
    		/* We got a non-empty token and we don't know what it is. */
    		alert("ERROR: Unknown option: %s\n", next);
    		return 0;
    	}
    	next = strtok(NULL, ",=");
    }

    if (NULL != all_conf_files)
    {
        next = strtok(all_conf_files, PATH_SEPARATOR);
        while (NULL != next)
        {
        	if (ini_parse(next, ini_handler, NULL) < 0)
        	{
        		alert("Can't load '%s'\n", next);
        		return 0;
        	}
        	next = strtok(NULL, PATH_SEPARATOR);
        }
    }

    return 1;
}

#ifndef JNICALL
#define JNICALL
#endif

#ifdef WIN32
//FIXME: this "test and set" operation needs to be atomic
static jboolean __sync_lock_test_and_set(jboolean* x,jboolean y) 
{
	jboolean z = *x;
	*x = y;
	return z;
}
#endif

static void JNICALL dumperThreadMain(__UNUSED__ jvmtiEnv* jvmti, JNIEnv* jni_env, __UNUSED__ void* arg)
{
	char* internalLeaksString;

    if (JNI_FALSE != __sync_lock_test_and_set(&gdata->dumpInProgress, JNI_TRUE))
    {
    	alert("Another dump is already in progress");
    	return;
    }
	gdata->numberOfLeaks = 0;
	initThreadData(jni_env);

	if (gdata->run_gc)
	{
		jvmtiError err;
    	debug("jleaker: Running garbage collection\n");
    	err = (*jvmti)->ForceGarbageCollection(jvmti);
    	if (err) alert("jleaker: Failed to run GC\n");
	}

    establish_connection(gdata->tcp_port);

    begin_xml_output();
    open_xml_element("memory-leaks", NULL);

	startTimer(&getThreadData()->timer, 0);

	tagAllMapsAndCollections();
	findLeaksInTaggedObjects();

	close_xml_element("memory-leaks");

	close_connection();

	stopTimer(&getThreadData()->timer, "Finished leak detection");
    releaseThreadData();
    
	internalLeaksString = findInternalMallocsLeaks();
    if (NULL != internalLeaksString)
    {
		alert("Internal jleaker error: %s\n", internalLeaksString);
		free(internalLeaksString);
    }
    if (gdata->self_check)
    {
    	selfCheck();
    }
	gdata->dumpInProgress = JNI_FALSE;
}

static void startDumperThread()
{
    JNIEnv *env = NULL;
    jclass thread_class;
    jmethodID thread_ctor;
    jstring thread_name;
    jobject thread;

    jint rc = (*gdata->vm)->GetEnv(gdata->vm, (void**)&env, JNI_VERSION_1_2);
    check_jvmti_error(gdata->jvmti, rc, "Get Env");

    thread_class = (*env)->FindClass(env, "java/lang/Thread");
    thread_ctor = (*env)->GetMethodID(env, thread_class, "<init>", "(Ljava/lang/String;)V");
    thread_name = (*env)->NewStringUTF(env, "JLeakerDumper");
    thread = (*env)->NewObject(env, thread_class, thread_ctor, thread_name);
    (*gdata->jvmti)->RunAgentThread(gdata->jvmti, thread, dumperThreadMain, NULL, JVMTI_THREAD_MAX_PRIORITY);
    (*env)->DeleteLocalRef(env, thread);
    (*env)->DeleteLocalRef(env, thread_name);
    (*env)->DeleteLocalRef(env, thread_class);
}

/* Callback for JVMTI_EVENT_VM_INIT */
static void JNICALL
vmInit(jvmtiEnv *jvmti, __UNUSED__ JNIEnv *env, __UNUSED__ jthread thread)
{
    enterAgentMonitor(jvmti); {
        /* Indicate VM has started */
        gdata->vmStarted = JNI_TRUE;
        startDumperThread();

    } exitAgentMonitor(jvmti);
}


static jint initAgent(JavaVM *vm, char *options)
{
	jint                rc;
	jvmtiError          err;
	jvmtiCapabilities   capabilities;
	jvmtiEventCallbacks callbacks;
	jvmtiEnv           *jvmti;

	if (JNI_FALSE == __sync_lock_test_and_set(&gdata->vmStarted, JNI_TRUE))
	{
		/* Get JVMTI environment */
		jvmti = NULL;
		rc = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION);
		if (rc != JNI_OK)
		{
			fatal_error("ERROR: Unable to create jvmtiEnv, error=%d\n", rc);
			return 1;
		}
		if ( jvmti == NULL )
		{
			fatal_error("ERROR: No jvmtiEnv* returned from GetEnv\n");
		}

		initAllocator(gdata->self_check);
		initClassesToCheck();
		gdata->jvmti = jvmti;
		gdata->vm = vm;
		gdata->vmDeathCalled = JNI_FALSE;

		/* Get/Add JVMTI capabilities */
		memset(&capabilities, 0, sizeof(capabilities));
		capabilities.can_tag_objects = 1;
		capabilities.can_get_source_file_name  = 1;
		capabilities.can_get_line_numbers  = 1;
		err = (*jvmti)->AddCapabilities(jvmti, &capabilities);
		check_jvmti_error(jvmti, err, "add capabilities");

		/* Create the raw monitor */
		err = (*jvmti)->CreateRawMonitor(jvmti, "agent lock", &(gdata->lock));
		check_jvmti_error(jvmti, err, "create raw monitor");

		/* Set callbacks and enable event notifications */
		memset(&callbacks, 0, sizeof(callbacks));
		callbacks.VMDeath                 = &vmDeath;
		callbacks.VMInit           = &vmInit;

		err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
		check_jvmti_error(jvmti, err, "set event callbacks");
		err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
				JVMTI_EVENT_VM_DEATH, NULL);
		check_jvmti_error(jvmti, err, "set event notifications");
		err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
				JVMTI_EVENT_VM_INIT, NULL);
		check_jvmti_error(jvmti, err, "Cannot set event notification");
	}
    
    if (!parse_agent_options(options))
    {
    	return 1;
    }

    return 0;
}


/* Agent_OnLoad() is called first, we prepare for a VM_INIT event here. */
JNIEXPORT jint JNICALL
Agent_OnLoad(__UNUSED__ JavaVM *vm, __UNUSED__ char *options, __UNUSED__ void *reserved)
{
/*	return initAgent(vm, options); */
	return 0;
}

JNIEXPORT jint JNICALL
Agent_OnAttach(JavaVM *vm, char *options, __UNUSED__ void *reserved)
{
	jint rc = initAgent(vm, options);
	if (0 != rc)
	{
		return rc;
	}
    startDumperThread();
    return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(__UNUSED__ JavaVM *vm)
{
}

