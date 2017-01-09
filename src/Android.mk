LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dirtysanta
LOCAL_SRC_FILES := dirtysanta.c
LOCAL_CFLAGS := -Wall
LOCAL_C_INCLUDES := include
LOCAL_LDFLAGS := -llog
include $(BUILD_EXECUTABLE)

