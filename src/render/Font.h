#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ot {

// Appends 2D overlay triangles (position + color, 6 floats per vertex) for
// `text`, with the first glyph's top-left at NDC coordinate (x, y). `scale`
// is the glyph cell height in NDC units.
void buildText(std::vector<float>& out, const std::string& text,
               float x, float y, float scale, const glm::vec3& color);

// Total width of `text` in NDC units at the given scale.
float textWidth(const std::string& text, float scale);

} // namespace ot
