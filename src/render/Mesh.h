#pragma once

#include <vector>

namespace ot {

// Interleaved vertex data: position (3 floats) + color (3 floats).
class Mesh {
public:
    void upload(const std::vector<float>& vertices);
    void uploadLines(const std::vector<float>& vertices);
    void render() const;
    void renderLines() const;
    void destroy();

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    int m_vertexCount = 0;

    unsigned int m_lineVao = 0;
    unsigned int m_lineVbo = 0;
    int m_lineCount = 0;
};

} // namespace ot
