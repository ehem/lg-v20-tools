LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := fix-h990-modem
LOCAL_SRC_FILES := fix-h990-modem.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -static
include $(BUILD_EXECUTABLE)

