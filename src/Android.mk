LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dirtysanta
LOCAL_SRC_FILES := dirtysanta.c
# Extra CFLAGS:
#	-DBACKUP_ALL_NONKDZ	To backup portions not found in KDZ files.
#	-DBACKUP_ALL	To include everything except /system due to size.
#	-DBACKUP_ALL_BACKUPS	To backup even backup areas.
LOCAL_CFLAGS := -Wall
LOCAL_C_INCLUDES := include
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

