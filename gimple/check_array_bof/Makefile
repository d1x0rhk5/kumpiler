PLUGIN_NAME = myplugin
PLUGIN_SO = $(PLUGIN_NAME).so

GCC_PLUGIN_DIR := $(shell gcc -print-file-name=plugin)
GCC_PLUGIN_INC := $(GCC_PLUGIN_DIR)/include
GCC_PLUGIN_BASE := $(GCC_PLUGIN_DIR)/..

all:
	g++ -fPIC -shared -o $(PLUGIN_SO) myplugin.cc \
	    -I$(GCC_PLUGIN_INC) -I$(GCC_PLUGIN_BASE) \
	    -DBUILDING_GCC -D_GNU_SOURCE -fno-rtti
	gcc -O0 main.c -fplugin=./$(PLUGIN_SO)

clean:
	rm -f $(PLUGIN_SO) a.out
