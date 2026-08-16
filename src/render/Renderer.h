#pragma once

#include <glm/glm.hpp>

struct SDL_Window;

namespace ot {

class Mesh;

class Renderer {
public:
    bool init(SDL_Window* window);
    void shutdown();

    void beginFrame();
    void draw(const Mesh& mesh, const glm::mat4& mvp);
    void drawLines(const Mesh& mesh, const glm::mat4& mvp);
    void drawOverlay(const Mesh& mesh);
    void bindTexture(unsigned int texture);
    void endFrame();

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    unsigned int m_program = 0;
    int m_mvpLoc = -1;
    unsigned int m_whiteTex = 0;
    unsigned int m_boundTex = 0;
};

} // namespace ot
