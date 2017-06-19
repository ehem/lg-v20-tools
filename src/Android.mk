LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := kdzwriter
LOCAL_SRC_FILES := kdzwriter.c kdz.c
LOCAL_CFLAGS := -Wall
include $(BUILD_EXECUTABLE)

