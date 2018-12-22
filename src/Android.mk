LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := split_system
LOCAL_SRC_FILES := split_system.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -Wl,-dynamic-linker,/sbin/linker64
LOCAL_LDLIBS := -lz
include $(BUILD_EXECUTABLE)

