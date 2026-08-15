#include "render/Font.h"

namespace ot {

namespace {

struct Glyph {
    const char* r[7]; // 7 rows, each 5 columns of '.' / '#'
};

const Glyph& glyphFor(char c) {
    static const Glyph kNone = {".....", ".....", ".....", ".....", ".....", ".....", "....."};
    switch (c) {
        case ' ': return kNone;
        case '0': { static const Glyph g = {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}; return g; }
        case '1': { static const Glyph g = {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}; return g; }
        case '2': { static const Glyph g = {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}; return g; }
        case '3': { static const Glyph g = {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."}; return g; }
        case '4': { static const Glyph g = {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}; return g; }
        case '5': { static const Glyph g = {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}; return g; }
        case '6': { static const Glyph g = {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}; return g; }
        case '7': { static const Glyph g = {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}; return g; }
        case '8': { static const Glyph g = {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}; return g; }
        case '9': { static const Glyph g = {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}; return g; }
        case 'A': { static const Glyph g = {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}; return g; }
        case 'B': { static const Glyph g = {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}; return g; }
        case 'C': { static const Glyph g = {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}; return g; }
        case 'D': { static const Glyph g = {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}; return g; }
        case 'E': { static const Glyph g = {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}; return g; }
        case 'F': { static const Glyph g = {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}; return g; }
        case 'G': { static const Glyph g = {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}; return g; }
        case 'H': { static const Glyph g = {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}; return g; }
        case 'I': { static const Glyph g = {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}; return g; }
        case 'J': { static const Glyph g = {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}; return g; }
        case 'K': { static const Glyph g = {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}; return g; }
        case 'L': { static const Glyph g = {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}; return g; }
        case 'M': { static const Glyph g = {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}; return g; }
        case 'N': { static const Glyph g = {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}; return g; }
        case 'O': { static const Glyph g = {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}; return g; }
        case 'P': { static const Glyph g = {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}; return g; }
        case 'Q': { static const Glyph g = {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}; return g; }
        case 'R': { static const Glyph g = {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}; return g; }
        case 'S': { static const Glyph g = {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}; return g; }
        case 'T': { static const Glyph g = {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}; return g; }
        case 'U': { static const Glyph g = {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}; return g; }
        case 'V': { static const Glyph g = {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}; return g; }
        case 'W': { static const Glyph g = {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}; return g; }
        case 'X': { static const Glyph g = {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}; return g; }
        case 'Y': { static const Glyph g = {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}; return g; }
        case 'Z': { static const Glyph g = {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}; return g; }
        case '.': { static const Glyph g = {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}; return g; }
        case ',': { static const Glyph g = {".....", ".....", ".....", ".....", ".##..", ".##..", ".#..."}; return g; }
        case ':': { static const Glyph g = {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}; return g; }
        case '-': { static const Glyph g = {".....", ".....", ".....", "#####", ".....", ".....", "....."}; return g; }
        case '/': { static const Glyph g = {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}; return g; }
        case '(': { static const Glyph g = {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}; return g; }
        case ')': { static const Glyph g = {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}; return g; }
        case '!': { static const Glyph g = {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}; return g; }
        case '?': { static const Glyph g = {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}; return g; }
        case '_': { static const Glyph g = {".....", ".....", ".....", ".....", ".....", ".....", "#####"}; return g; }
        case '+': { static const Glyph g = {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}; return g; }
        case '=': { static const Glyph g = {".....", ".....", "#####", ".....", "#####", ".....", "....."}; return g; }
        case '>': { static const Glyph g = {"#....", ".#...", "..#..", "...#.", "..#..", ".#...", "#...."}; return g; }
        case '<': { static const Glyph g = {"....#", "...#.", "..#..", ".#...", "..#..", "...#.", "....#"}; return g; }
        default: return kNone;
    }
}

void pushQuad(std::vector<float>& out, float x0, float y0, float x1, float y1,
              const glm::vec3& c) {
    const float verts[6][6] = {
        {x0, y0, 0, c.r, c.g, c.b},
        {x1, y0, 0, c.r, c.g, c.b},
        {x1, y1, 0, c.r, c.g, c.b},
        {x0, y0, 0, c.r, c.g, c.b},
        {x1, y1, 0, c.r, c.g, c.b},
        {x0, y1, 0, c.r, c.g, c.b},
    };
    for (int i = 0; i < 6; ++i) {
        out.insert(out.end(), verts[i], verts[i] + 6);
    }
}

} // namespace

void buildText(std::vector<float>& out, const std::string& text,
               float x, float y, float scale, const glm::vec3& color) {
    const float px = scale / 7.0f;     // cell unit
    const float advance = 6.0f * px;   // 5 columns + 1 spacing column

    float cx = x;
    for (const char ch : text) {
        const Glyph& g = glyphFor(ch);
        for (int r = 0; r < 7; ++r) {
            for (int c = 0; c < 5; ++c) {
                if (g.r[r][c] == '#') {
                    const float x0 = cx + c * px;
                    const float x1 = x0 + px;
                    const float y0 = y - (r + 1) * px;
                    const float y1 = y - r * px;
                    pushQuad(out, x0, y0, x1, y1, color);
                }
            }
        }
        cx += advance;
    }
}

float textWidth(const std::string& text, float scale) {
    const float advance = 6.0f * (scale / 7.0f);
    return advance * static_cast<float>(text.size());
}

} // namespace ot
