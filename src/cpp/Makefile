OSNAME ?= linux

# Source lists
ifeq ($(ARCH_STR),)
LIBNAME=jleaker
else
LIBNAME=jleaker-$(ARCH_STR)
endif
SOURCES=jleaker.c agent_util.c bitmask_set.c jvm_reference.c data_struct.c jobject_print.c leak_detect.c allocator.c ini.c strmap.c

# Solaris Sun C Compiler Version 5.5
ifeq ($(OSNAME), solaris)
    # Sun Solaris Compiler options needed
    COMMON_FLAGS=-mt -KPIC
    # Options that help find errors
    COMMON_FLAGS+= -Xa -v -xstrconst -xc99=%none
    # Check LIBARCH for any special compiler options
    LIBARCH=$(shell uname -p)
    ifeq ($(LIBARCH), sparc)
        COMMON_FLAGS+=-xarch=v8 -xregs=no%appl
    endif
    ifeq ($(LIBARCH), sparcv9)
        COMMON_FLAGS+=-xarch=v9 -xregs=no%appl
    endif
    ifeq ($(OPT), true)
        CFLAGS=-xO2 $(COMMON_FLAGS) 
    else
        CFLAGS=-g $(COMMON_FLAGS)
    endif
    # Object files needed to create library
    OBJECTS=$(patsubst %,obj/%, $(SOURCES:%.c=%.o))
    # Library name and options needed to build it
    LIBRARY=bin/lib$(LIBNAME).so
    LDFLAGS=-z defs -ztext
    # Libraries we are dependent on
    LIBRARIES= -lc
    # Building a shared library
    LINK_SHARED=$(LINK.c) -G -o $@
endif

# Linux GNU C Compiler
ifeq ($(OSNAME), linux)
    # GNU Compiler options needed to build it
    COMMON_FLAGS=-fno-strict-aliasing -fPIC -fno-omit-frame-pointer
    # Options that help find errors
    COMMON_FLAGS+= -W -Wall  -Wno-unused -Wno-parentheses
    ifeq ($(OPT), true)
        CFLAGS=-O2 $(COMMON_FLAGS) 
    else
        CFLAGS=-g $(COMMON_FLAGS) 
    endif
    # Object files needed to create library
    OBJECTS=$(patsubst %,obj/$(ARCH_STR)/%, $(SOURCES:%.c=%.o))
    # Library name and options needed to build it
    LIBRARY=bin/lib$(LIBNAME).so
    LDFLAGS=-Wl,-soname=$(LIBRARY) -static-libgcc -mimpure-text
    # Libraries we are dependent on
    LIBRARIES=-lc
    # Building a shared library
    LINK_SHARED=$(LINK.c) -shared -o $@
endif

ifeq ($(OSNAME), win32)
win32:
	@echo This project is built on Windows using Visual Studio 2010
	@/bin/false
endif

VPATH := $(PWD)

ifeq ($(DEBUG),1)
	DEBUG_OPT := -g -O0
endif

# Common -I options
CFLAGS += -I.
CFLAGS += -Wall -Werror $(DEBUG_OPT) -I$(JDK)/include -I$(JDK)/include/$(OSNAME)
CFLAGS += $(CC_ARCH_FLAGS)

# Default rule
ifeq ($(JDK),)
all:
	@echo JDK variable for JDK home was not set
	@/bin/false
else
all: build_java
	export ARCH_STR=`$(JDK)/bin/java -classpath ../../ant/dist/jleaker.jar com.intel.swiss.sws.netstar.application.jleaker.build.GetJavaArch` && $(MAKE) lib 
endif

build_java:
	cd ../../ant && ant

lib: $(LIBRARY)
	
obj/$(ARCH_STR)/%.o: %.c
	if [ ! -d obj/$(ARCH_STR) ]; then mkdir -p obj/$(ARCH_STR); fi
	$(COMPILE.c) $< -o $@
	
# Build native library
$(LIBRARY): $(OBJECTS) bin
	$(LINK_SHARED) $(DEBUG_OPT) $(OBJECTS) $(LIBRARIES)

bin:
	mkdir $@

# Cleanup the built bits
clean:
	rm -rf bin obj

# Compilation rule only needed on Windows
ifeq ($(OSNAME), win32)
%.obj: %.c
	$(COMPILE.c) $<
endif

