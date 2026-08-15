LOCAL_PATH := $(call my-dir)

# Keep a reference to this jni directory before SDL's Android.mk
# (which resets LOCAL_PATH) is included.
OT_JNI_DIR := $(LOCAL_PATH)

# Build SDL2 (vendored at the repository root).
include $(OT_JNI_DIR)/../../../third_party/SDL2/Android.mk

# Build enet static library.
include $(OT_JNI_DIR)/src/enet/Android.mk

# Build the game.
include $(OT_JNI_DIR)/src/Android.mk
