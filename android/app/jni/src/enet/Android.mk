LOCAL_PATH := $(call my-dir)

# enet static library (vendored at the repository root).
ENET_INC := $(LOCAL_PATH)/../../../../../third_party/enet/include

include $(CLEAR_VARS)
LOCAL_MODULE := enet
LOCAL_C_INCLUDES := $(ENET_INC)
LOCAL_SRC_FILES := \
    ../../../../../third_party/enet/callbacks.c \
    ../../../../../third_party/enet/compress.c \
    ../../../../../third_party/enet/host.c \
    ../../../../../third_party/enet/list.c \
    ../../../../../third_party/enet/packet.c \
    ../../../../../third_party/enet/peer.c \
    ../../../../../third_party/enet/protocol.c \
    ../../../../../third_party/enet/unix.c
LOCAL_CFLAGS := \
    -DHAS_FCNTL=1 -DHAS_POLL=1 -DHAS_GETADDRINFO=1 -DHAS_GETNAMEINFO=1 \
    -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1 -DHAS_SOCKLEN_T=1 -DHAS_MSGHDR_FLAGS=1 \
    -DHAS_OFFSETOF=1
include $(BUILD_STATIC_LIBRARY)
