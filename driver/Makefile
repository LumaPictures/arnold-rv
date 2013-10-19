#Makefile for Luma Shaders
.DEFAULT_GOAL := all

BASE_LIBNAME := rvdriver

variant :=

ifdef DEBUG
	echo "building debug"
	CFLAGS += -g -DDEBUG
	variant := $(variant).debug
else
	CFLAGS += -O3 -DNDEBUG
endif

UNAME := $(shell uname)

top_build_dir := build

ARNOLD_VER3 = $(ARNOLD_MAJOR_VERSION).$(ARNOLD_MINOR_VERSION).$(ARNOLD_REVISION_VERSION)
build_dir     := ${top_build_dir}/${ARNOLD_VER3}/${OS_ARCH}${variant}

RELEASE = $(REPO_PATH)/arnold/shaders/published/$(ARNOLD_VER3)/`uname`-`uname -m`

INCLUDE = 	-I$(ARNOLD_HOME)/include 		\
			-I.

CFLAGS += -c -Wall -Wsign-compare  -fvisibility=hidden -fPIC $(INCLUDE)

# options found in mental images makefile that are not in ours
#	-DNV_CG -D__NO_CTYPE -DHYPERTHREAD -D__NO_CTYPE -DHYPERTHREAD -D_STLP_LITTLE_ENDIAN=1 -D_STLP_SHORT_STRING_SZ=32 -D_STLP_REAL_LOCALE_IMPLEMENTED

CC = /usr/local/gcc-4.1.2/bin/g++
CC = g++

LINK = /usr/local/gcc-4.1.2/bin/g++
LINK = g++

LIBS = -lai -lboost_thread-mt -lboost_system-mt

LIBDIR = 	-L$(ARNOLD_HOME)/bin

LDFLAGS = -shared --whole-archive -Bsymbolic  $(LIBDIR)

ifeq ($(UNAME), Linux)
	CFLAGS += -D_LINUX
	LDFLAGS += -export-dynamic
	LIBEXT = so
endif
ifeq ($(UNAME), Darwin)
	CFLAGS += -D_DARWIN -DOSMac_
	LDFLAGS += -dynamiclib
	LIBEXT = dylib
endif

SOURCES := rvdriver.cpp

SOURCE_FILENAMES := $(notdir $(SOURCES))
OBJS := $(SOURCE_FILENAMES:%.cpp=${build_dir}/%.o)
#OBJS := $(SOURCES:%.cpp=${build_dir}/%.o)

LIBNAME = ${BASE_LIBNAME}.${LIBEXT}

FINAL_LIB := ${build_dir}/${LIBNAME}

BUILD_INFO_FILE := ${top_build_dir}/eclipseBuildInfo

# RECIPES

.PHONY : all debug clean ${BUILD_INFO_FILE} 


debug:
	${MAKE} DEBUG=1


all: ${FINAL_LIB}
	# This is just so that the last-built library is always in a certain place,
	# so that in eclipse we can always point to a single place in our run/debug
	# configuration, regardless of whether we're in release or debug config
	cp -f ${FINAL_LIB} $(top_build_dir)/$(LIBNAME)

debug:
	${MAKE} DEBUG=1



${build_dir}/_buildDirMarker :
	mkdir -p ${build_dir}
	touch $@

# This is just here so that we can state myCppFile.h as a dependency for every
# myCppFile.cpp, even if myCppFile.h doesn't exist
%.h :
	echo "Dummy rule for header files - $@"

${build_dir}/%.o : %.cpp %.h ${build_dir}/_buildDirMarker
	$(CC) -o $@ $(CFLAGS) $<

${FINAL_LIB}: $(OBJS)
	$(LINK) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
	(if [ -z "${DEBUG}" ] ; \
	then \
		echo "Copying to $(RELEASE)" ; \
		mkdir -p $(RELEASE) ; \
		cp -f $@ $(RELEASE) ; \
	fi)

clean:
	rm -rf $(top_build_dir)