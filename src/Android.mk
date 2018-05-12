LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dirtysanta
LOCAL_SRC_FILES := dirtysanta.c
# Extra CFLAGS:
#	-DBACKUP_ALL_NONKDZ	To backup portions not found in KDZ files.
#	-DBACKUP_ALL	To include everything except /system due to size.
#	-DBACKUP_ALL_BACKUPS	To backup even backup areas.
LOCAL_CFLAGS := -Wall
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := syncfile
LOCAL_SRC_FILES := syncfile.c
LOCAL_CFLAGS := -Wall
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := kdzwriter
LOCAL_SRC_FILES := kdzwriter.c kdz.c md5.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -Wl,-dynamic-linker,/sbin/linker64
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := fix-h990-modem
LOCAL_SRC_FILES := fix-h990-modem.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -static
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE := rmOP
LOCAL_SRC_FILES := rmOP.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -Wl,-dynamic-linker,/sbin/linker64
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
include $(BUILD_EXECUTABLE)

