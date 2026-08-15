#include "render/Mesh.h"
#include "render/GLHeaders.h"

namespace ot {

namespace {

void configureAttributes() {
    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

} // namespace

void Mesh::upload(const std::vector<float>& vertices) {
    m_vertexCount = static_cast<int>(vertices.size() / 6);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    configureAttributes();

    glBindVertexArray(0);
}

void Mesh::uploadLines(const std::vector<float>& vertices) {
    m_lineCount = static_cast<int>(vertices.size() / 6);

    glGenVertexArrays(1, &m_lineVao);
    glBindVertexArray(m_lineVao);

    glGenBuffers(1, &m_lineVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_DYNAMIC_DRAW);

    configureAttributes();

    glBindVertexArray(0);
}

void Mesh::render() const {
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);
}

void Mesh::renderLines() const {
    glBindVertexArray(m_lineVao);
    glDrawArrays(GL_LINES, 0, m_lineCount);
    glBindVertexArray(0);
}

void Mesh::destroy() {
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_lineVbo) {
        glDeleteBuffers(1, &m_lineVbo);
        m_lineVbo = 0;
    }
    if (m_lineVao) {
        glDeleteVertexArrays(1, &m_lineVao);
        m_lineVao = 0;
    }
    m_vertexCount = 0;
    m_lineCount = 0;
}

} // namespace ot
