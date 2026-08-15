#pragma once

#if defined(__ANDROID__)
#define OT_PLATFORM_ANDROID 1
#define OT_PLATFORM_WINDOWS 0
#elif defined(_WIN32)
#define OT_PLATFORM_WINDOWS 1
#define OT_PLATFORM_ANDROID 0
#else
#error "Unsupported platform"
#endif
