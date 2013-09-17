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

#include "agent_util.h"
#include "data_struct.h"
#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#endif
#include <errno.h>
#include "allocator.h"

extern GlobalData* gdata;
int xml_depth = 0;

/* ------------------------------------------------------------------- */
/* Generic C utility functions */

/* Send message to stdout or whatever the data output location is */
void
alert(const char * format, ...)
{
    va_list ap;

    va_start(ap, format);
    (void)vfprintf(stdout, format, ap);
    fflush(stdout);
    va_end(ap);
}

/* Send message to stderr or whatever the error output location is and exit  */
void
fatal_error(const char * format, ...)
{
    va_list ap;

    va_start(ap, format);
    (void)vfprintf(stderr, format, ap);
    (void)fflush(stderr);
    va_end(ap);
    exit(3);
}

void myPrintf(OutputStream f, const char * format, ...)
{
    va_list ap;

    va_start(ap, format);
	if (f.type == OUTPUT_TYPE_FILE)
	{
		(void)vfprintf(f.handle.file, format, ap);
		fflush(f.handle.file);
	} 
#ifdef WIN32
	else if (f.type == OUTPUT_TYPE_WIN32SOCK)
	{
	    int l = vsnprintf( 0, 0, format, ap );
		char* buf = (char*) malloc( l + 1 );
	    va_end(ap);
		va_start(ap, format);
		vsnprintf( buf, l, format, ap );
		send(f.handle.windowSocket, buf, l, 0 );
		free(buf);
	}
#endif
    va_end(ap);
}

void myFlush(OutputStream f)
{
	if (f.type == OUTPUT_TYPE_FILE)
	{
		fflush(f.handle.file);
	} 
}

/* ------------------------------------------------------------------- */
/* Generic JVMTI utility functions */

/* Every JVMTI interface returns an error code, which should be checked
 *   to avoid any cascading errors down the line.
 *   The interface GetErrorName() returns the actual enumeration constant
 *   name, making the error messages much easier to understand.
 */
void
check_jvmti_error(jvmtiEnv *jvmti, jvmtiError errnum, const char *str)
{
    if ( errnum != JVMTI_ERROR_NONE ) {
	char       *errnum_str;
	
	errnum_str = NULL;
	(void)(*jvmti)->GetErrorName(jvmti, errnum, &errnum_str);
	
	fatal_error("ERROR: JVMTI: %d(%s): %s\n", errnum, 
		(errnum_str==NULL?"Unknown":errnum_str),
		(str==NULL?"":str));
    }
}

/* All memory allocated by JVMTI must be freed by the JVMTI Deallocate
 *   interface.
 */
void
deallocate(jvmtiEnv *jvmti, void *ptr)
{
    jvmtiError error;
    
    error = (*jvmti)->Deallocate(jvmti, ptr);
    check_jvmti_error(jvmti, error, "Cannot deallocate memory");
}

/* Allocation of JVMTI managed memory */
void *
allocate(jvmtiEnv *jvmti, jint len)
{
    jvmtiError error;
    void      *ptr;
    
    error = (*jvmti)->Allocate(jvmti, len, (unsigned char **)&ptr);
    check_jvmti_error(jvmti, error, "Cannot allocate memory");
    return ptr;
}

char* get_class_name(jvmtiEnv* jvmti, JNIEnv* env, jclass klass)
{
	jstring name;
	char* classname;
	if (NULL == klass)
	{
		fatal_error("klass is NULL at %s:%d\n", __FILE__, __LINE__);
	}
	name = (jstring)(*env)->CallObjectMethod(env, klass, getThreadData()->metGetClassName);
	if (NULL == name)
	{
		char* sig;
	    jint err = (*jvmti)->GetClassSignature(jvmti, klass, &sig, NULL);
	    check_jvmti_error(jvmti, err, "get class signature");
	    if (sig == NULL)
	    {
	        fatal_error("ERROR: No class signature found\n");
	    }
		classname = myStrdup(sig);
	    deallocate(jvmti, sig);
	    return classname;
	}
	else
	{
		const char* classnameJava;

		classnameJava = (*env)->GetStringUTFChars(env, name, NULL);
		classname = myStrdup(classnameJava);
		(*env)->ReleaseStringUTFChars(env, name, classnameJava);
		(*env)->DeleteLocalRef(env, name);
	}


    return classname;
}

char* escapeString(const char* s)
{
	int j, i, quotes = 0;
	char* res;
	for (i = 0; s[i]; i++)
	{
		if (s[i] == '"') quotes++;
	}
	if (0 == quotes)
	{
		return (char*)s;
	}
	res = (char*)myAlloc(1 + i + 5 * quotes);
	for (i = 0, j = 0; s[i]; i++)
	{
		if (s[i] == '"')
		{
			strncpy(res + j, "&quot;", 6);
			j += 6;
		}
		else
		{
			res[j++] = s[i];
		}
	}
	res[j] = 0;
	return res;
}

void begin_xml_output()
{
	echo("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n");
}

void apply_xml_identation(OutputStream f)
{
	int depth;
	for (depth = 0; depth < xml_depth; depth++) myPrintf(f, "\t");
}

void echo_xml_element(const char* tag, const char* endTag, va_list ap)
{
	OutputStream f = getThreadData()->outputStream;
	const char* attr;

	apply_xml_identation(f);
	myPrintf(f, "<%s", tag);
    while (NULL != (attr = va_arg(ap, const char*)))
    {
    	const char* value = va_arg(ap, const char*);
    	char* newValue = escapeString(value);

    	myPrintf(f, " %s=\"%s\"", attr, newValue);
    	if (value != newValue)
    	{
    		myFree(newValue);
    	}
    }
    myPrintf(f, "%s>\n", endTag);
    myFlush(f);
}

void complete_xml_element(const char* tag, ...)
{
    va_list ap;
    va_start(ap, tag);
    echo_xml_element(tag, " /", ap);
    va_end(ap);
}

void open_xml_element(const char* tag, ...)
{
    va_list ap;
    va_start(ap, tag);
    echo_xml_element(tag, "", ap);
    va_end(ap);
    xml_depth++;
}

void close_xml_element(const char* tag)
{
	OutputStream f = getThreadData()->outputStream;
    xml_depth--;
	apply_xml_identation(f);
	myPrintf(f, "</%s>\n", tag);
	myFlush(f);
}

void echo(const char * format, ...)
{
	OutputStream f = getThreadData()->outputStream;
    va_list ap;

    va_start(ap, format);
    myPrintf(f, format, ap);
    myFlush(f);
    va_end(ap);
}

void debug(const char * format, ...)
{
    if (gdata->debug)
    {
	    va_list ap;

	    va_start(ap, format);
	    (void)vfprintf(stdout, format, ap);
	    fflush(stdout);
	    va_end(ap);
    }
}

#ifdef WIN32
int establish_connection(int port)
{
    struct sockaddr_in serv_addr;
    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int iResult;
	WSADATA wsaData;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0) {
	    alert("WSAStartup failed: %d\n", iResult);
	    return 0;
	}
    xml_depth = 0;
	if (sockfd < 0)
	{
		alert("Failed to create socket: %d\n", errno);
		return 0;
	}
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(port);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
    {
        alert("Failed to connect: %d\n",errno);
        return 0;
    }
	getThreadData()->outputStream.handle.windowSocket = sockfd;
	getThreadData()->outputStream.type = OUTPUT_TYPE_WIN32SOCK;
    return 1;
}
#else
int establish_connection(int port)
{
    struct sockaddr_in serv_addr;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    xml_depth = 0;
	if (sockfd < 0)
	{
		alert("Failed to create socket: %d\n", errno);
		return 0;
	}
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(port);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
    {
        alert("Failed to connect: %d\n",errno);
        return 0;
    }
    getThreadData()->outputStream.handle.file = fdopen(sockfd, "w+");
    if (NULL == getThreadData()->outputStream.handle.file)
    {
    	alert("fdopen failed: %d\n", errno);
    	return 0;
    }
    return 1;
}
#endif

void close_connection()
{
	OutputStream f = getThreadData()->outputStream;
	getThreadData()->outputStream.handle.file = stdout;
	getThreadData()->outputStream.type = OUTPUT_TYPE_FILE;

	if (f.type == OUTPUT_TYPE_FILE)
	{
		if (f.handle.file != stdout)
		{
			fclose(f.handle.file);
		}
	}
#ifdef WIN32
	else if (f.type == OUTPUT_TYPE_WIN32SOCK)
	{
		closesocket(f.handle.windowSocket);
	}
#endif
}

/* ------------------------------------------------------------------- */
