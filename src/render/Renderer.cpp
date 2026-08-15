#include "render/Renderer.h"
#include "render/GLHeaders.h"
#include "render/Mesh.h"

#include <SDL.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

namespace ot {

#if OT_PLATFORM_ANDROID
static const char* kVertexShaderSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kFragmentShaderSrc = R"(#version 300 es
precision mediump float;
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";
#else
static const char* kVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kFragmentShaderSrc = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";
#endif

static unsigned int compileShader(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::printf("[ot] shader compile error: %s\n", log);
    }
    return shader;
}

bool Renderer::init(SDL_Window* window) {
    m_window = window;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#if OT_PLATFORM_ANDROID
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    m_glContext = SDL_GL_CreateContext(window);
    if (!m_glContext) {
        std::printf("[ot] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }

#if !OT_PLATFORM_ANDROID
    glewExperimental = GL_TRUE;
    GLenum glewResult = glewInit();
    if (glewResult != GLEW_OK) {
        std::printf("[ot] glewInit failed: %s\n",
                    reinterpret_cast<const char*>(glewGetErrorString(glewResult)));
        return false;
    }
#endif

    unsigned int vs = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    int linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        std::printf("[ot] program link error: %s\n", log);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_mvpLoc = glGetUniformLocation(m_program, "uMVP");

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    std::printf("[ot] renderer initialized\n");
    return true;
}

void Renderer::beginFrame() {
    glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::draw(const Mesh& mesh, const glm::mat4& mvp) {
    glUseProgram(m_program);
    glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    mesh.render();
}

void Renderer::drawLines(const Mesh& mesh, const glm::mat4& mvp) {
    glUseProgram(m_program);
    glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    mesh.renderLines();
}

void Renderer::drawOverlay(const Mesh& mesh) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_program);
    glUniformMatrix4fv(m_mvpLoc, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
    mesh.render();
    glEnable(GL_DEPTH_TEST);
}

void Renderer::endFrame() {
    SDL_GL_SwapWindow(m_window);
}

void Renderer::shutdown() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_glContext) {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
}

} // namespace ot
