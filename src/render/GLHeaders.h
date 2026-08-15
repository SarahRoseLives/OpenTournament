#pragma once

#include "core/Platform.h"

#if OT_PLATFORM_ANDROID
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
