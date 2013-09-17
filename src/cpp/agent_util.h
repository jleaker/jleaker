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

#ifndef __AGENT_UTIL_H__
#define __AGENT_UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

#include "jni.h"
#include "jvmti.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#	define PATH_SEPARATOR ";"
#else
#	define PATH_SEPARATOR ":"
#endif

void  alert(const char * format, ...);
void debug(const char * format, ...);
void  fatal_error(const char * format, ...);
void  check_jvmti_error(jvmtiEnv *jvmti, jvmtiError errnum, const char *str);
void  deallocate(jvmtiEnv *jvmti, void *ptr);
void *allocate(jvmtiEnv *jvmti, jint len);
char* get_class_name(jvmtiEnv* jvmti, JNIEnv* env, jclass klass);
int establish_connection(int port);
void close_connection();
void echo(const char * format, ...);
void begin_xml_output();
void complete_xml_element(const char* tag, ...);
void open_xml_element(const char* tag, ...);
void close_xml_element(const char* tag);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif

