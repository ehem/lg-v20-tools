LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := rmOP
LOCAL_SRC_FILES := rmOP.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -Wl,-dynamic-linker,/sbin/linker64
LOCAL_LDLIBS := -lz
include $(BUILD_EXECUTABLE)

