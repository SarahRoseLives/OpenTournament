LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

# Paths are relative to this directory (jni/src); four levels up is the repo root.
SDL_PATH  := $(LOCAL_PATH)/../../../../third_party/SDL2
GLM_PATH  := $(LOCAL_PATH)/../../../../third_party/glm
ENET_INC  := $(LOCAL_PATH)/../../../../third_party/enet/include
OT_SRC    := $(LOCAL_PATH)/../../../../src

LOCAL_C_INCLUDES := $(OT_SRC) $(SDL_PATH)/include $(GLM_PATH) $(ENET_INC)

LOCAL_SRC_FILES := \
    ../../../../src/main.cpp \
    ../../../../src/render/Renderer.cpp \
    ../../../../src/render/Camera.cpp \
    ../../../../src/render/Mesh.cpp \
    ../../../../src/render/Font.cpp \
    ../../../../src/ui/Menu.cpp \
    ../../../../src/input/Input.cpp \
    ../../../../src/game/Level.cpp \
    ../../../../src/game/Player.cpp \
    ../../../../src/game/Weapon.cpp \
    ../../../../src/game/CollisionWorld.cpp \
    ../../../../src/game/BrushCollisionWorld.cpp \
    ../../../../src/net/Server.cpp \
    ../../../../src/net/Client.cpp \
    ../../../../src/map/QuakeMap.cpp \
    ../../../../src/map/OtMap.cpp

LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_STATIC_LIBRARIES := enet

LOCAL_LDLIBS := -lGLESv3 -lGLESv2 -lOpenSLES -llog -landroid

include $(BUILD_SHARED_LIBRARY)
