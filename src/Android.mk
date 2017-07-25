LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := kdzwriter
LOCAL_SRC_FILES := kdzwriter.c kdz.c md5.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDLIBS := -lz
include $(BUILD_EXECUTABLE)

