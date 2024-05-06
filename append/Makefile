# Copyright 2013-2023, MACHBASE Corporation or its subsidiaries.
# All rights reserved.

include $(MACHBASE_HOME)/install/machbase_env.mk

GCCVERSIONGTEQ7 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 7)

ifeq "$(GCCVERSIONGTEQ7)" "1"
    LD_FLAGS += -no-pie
endif

INCLUDES += $(INC_OPT)/$(MACHBASE_HOME)/include

all : sample_append


sample_append : sample_append.o
	$(LD_CC) $(LD_FLAGS) $(LD_OUT_OPT)$@ $< $(LIB_OPT)machbasecli$(LIB_AFT)  $(LIBDIR_OPT)$(MACHBASE_HOME)/lib $(LD_LIBS)

sample_append.o : sample_append.c
	$(COMPILE.cc) $(CC_FLAGS) $(INCLUDES) $(CC_OUT_OPT)$@ $<

clean :
	rm -f *.o sample_append
