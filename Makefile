#
# File Integrity Tracker (fit)
# 
# Copyright (c) 2022, Stone Steps Inc.
#

SHELL := /bin/bash

# remove all standard suffix rules
.SUFFIXES:

.PHONY: all clean

SRCDIR := src

# build all output in ./build, unless asked otherwise
ifeq ($(strip $(BLDDIR)),)
BLDDIR := build
endif

SRCS := fit.cpp file_tree_walker.cpp file_tracker.cpp exif_reader.cpp \
        print_stream.cpp sqlite.cpp sha256/sha256.c unicode.cpp

LIBS := sqlite3 pthread stdc++fs exiv2 exiv2-xmp expat z

OBJS := $(patsubst %.c,%.o,$(filter %.c,$(SRCS))) \
	$(patsubst %.cpp,%.o,$(filter %.cpp,$(SRCS)))

DEPS := $(OBJS:.o=.d)

# compiler options shared between C and C++ source
CCFLAGS_COMMON := -Werror -pedantic

ifdef $(DEBUG)
CCFLAGS_COMMON += -g
endif

CFLAGS := -std=gnu99 \
	$(CCFLAGS_COMMON)

CXXFLAGS := -std=c++17 \
	$(CCFLAGS_COMMON) \
	-fexceptions \
	-DRAPIDJSON_HAS_STDSTRING \
	-DRAPIDJSON_HAS_CXX11_RVALUE_REFS \
	-DRAPIDJSON_HAS_CXX11_NOEXCEPT \
	-DRAPIDJSON_HAS_CXX11_RANGE_FOR


ifeq ($(findstring -g,$(CXXFLAGS)),)
CXXFLAGS += -O3
endif

ifeq ($(findstring -g,$(CFLAGS)),)
CFLAGS += -O3
endif

# if CI pipeline build number is provided, add it to the build
ifdef GH_BUILD_NUMBER
CXXFLAGS += -DBUILD_NUMBER=$(GH_BUILD_NUMBER)
endif

$(BLDDIR)/fit: $(addprefix $(BLDDIR)/,$(OBJS)) | $(BLDDIR) 
	$(CXX) -o $@ $(addprefix -L,$(LIBDIRS)) \
		$(addprefix $(BLDDIR)/,$(OBJS)) \
		$(addprefix -l,$(LIBS)) 

$(BLDDIR): 
	@mkdir $(BLDDIR)

clean:
	@echo 'Removing object files...'
	@rm -f $(addprefix $(BLDDIR)/, $(OBJS))
	@echo 'Removing dependency files...'
	@rm -f $(addprefix $(BLDDIR)/, $(DEPS))
	@echo 'Removing binaries...'
	@rm -f $(BLDDIR)/fit
	@echo 'Done'

# C/C++ compile rules
$(BLDDIR)/%.o : $(SRCDIR)/%.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(addprefix -I,$(INCDIRS)) $< -o $@

$(BLDDIR)/%.o : $(SRCDIR)/%.cpp
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $(addprefix -I,$(INCDIRS)) $< -o $@

# dependencies
$(BLDDIR)/%.d : $(SRCDIR)/%.c
	@if [ ! -e $(@D) ]; then mkdir -p $(@D); fi
	set -e; $(CC) -MM $(CPPFLAGS) $(CFLAGS) $(addprefix -I,$(INCDIRS)) $< | \
	sed 's|^[ \t]*$(*F)\.o|$(BLDDIR)/$*.o $(BLDDIR)/$*.d|g' > $@

$(BLDDIR)/%.d : $(SRCDIR)/%.cpp
	@if [ ! -e $(@D) ]; then mkdir -p $(@D); fi
	set -e; $(CXX) -MM $(CPPFLAGS) $(CXXFLAGS) $(addprefix -I,$(INCDIRS)) $< | \
	sed 's|^[ \t]*$(*F)\.o|$(BLDDIR)/$*.o $(BLDDIR)/$*.d|g' > $@

ifeq ($(MAKECMDGOALS),)
include $(addprefix $(BLDDIR)/, $(DEPS))
else ifneq ($(filter $(BLDDIR)/fit,$(MAKECMDGOALS)),)
include $(addprefix $(BLDDIR)/, $(DEPS))
endif
