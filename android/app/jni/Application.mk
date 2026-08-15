# OpenTournament - Android native build configuration.

# Primary ABI for now; add 'armeabi-v7a' if 32-bit devices are needed.
APP_ABI := arm64-v8a

# Minimum runtime API level.
APP_PLATFORM := android-24

# Use libc++ statically linked into the app shared library.
APP_STL := c++_static

# C++20 with exceptions/RTTI enabled for future engine work.
APP_CPPFLAGS := -std=c++20 -fexceptions -frtti
APP_CFLAGS := -std=c11
