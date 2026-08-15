#include "map/QuakeMap.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sstream>

#include "game/BrushCollisionWorld.h"

namespace ot::map {

namespace {

struct Tok {
    enum Kind { Punct, String, Word } kind = Word;
    std::string text;
    char punct = 0;
};

std::vector<Tok> tokenize(const std::string& s) {
    std::vector<Tok> toks;
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
            Tok t;
            t.kind = Tok::Punct;
            t.punct = c;
            toks.push_back(t);
            ++i;
            continue;
        }
        if (c == '"') {
            ++i;
            const size_t start = i;
            while (i < n && s[i] != '"') ++i;
            Tok t;
            t.kind = Tok::String;
            t.text = s.substr(start, i - start);
            toks.push_back(t);
            if (i < n) ++i;
            continue;
        }
        const size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(s[i])) &&
               s[i] != '{' && s[i] != '}' && s[i] != '(' && s[i] != ')' &&
               s[i] != '[' && s[i] != ']' && s[i] != '"') {
            ++i;
        }
        Tok t;
        t.kind = Tok::Word;
        t.text = s.substr(start, i - start);
        toks.push_back(t);
    }
    return toks;
}

// Quake (Z-up) -> our engine (Y-up): x -> x, y -> z, z -> y.
glm::vec3 convert(const glm::vec3& q) {
    return glm::vec3(q.x, q.z, q.y);
}

bool isPunct(const Tok& t, char c) {
    return t.kind == Tok::Punct && t.punct == c;
}

bool isSpecialTexture(const std::string& name) {
    static const char* kSpecial[] = {
        "trigger", "clip", "skip", "hint", "origin", "areaportal", "nodraw",
        "mist", "ladder", "playerclip", "monsterclip",
    };
    // Use only the base name (after any directory).
    const size_t slash = name.find_last_of("/\\");
    const std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    for (const char* s : kSpecial) {
        if (base == s) {
            return true;
        }
    }
    return false;
}

void makeBasis(const glm::vec3& n, glm::vec3& t, glm::vec3& s) {
    const glm::vec3 ref = (std::fabs(n.z) < 0.9f) ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    t = glm::normalize(glm::cross(n, ref));
    s = glm::cross(n, t);
}

std::vector<glm::vec3> clipPolygon(const std::vector<glm::vec3>& poly, const Plane& plane) {
    std::vector<glm::vec3> result;
    if (poly.empty()) {
        return result;
    }
    const float eps = 0.1f;
    glm::vec3 a = poly.back();
    float da = glm::dot(plane.normal, a) - plane.dist;
    for (const glm::vec3& b : poly) {
        const float db = glm::dot(plane.normal, b) - plane.dist;
        const bool inA = da <= eps;
        const bool inB = db <= eps;
        if (inA) {
            result.push_back(a);
        }
        if (inA != inB) {
            const float denom = da - db;
            const float t = std::fabs(denom) > 1e-6f ? da / denom : 0.0f;
            result.push_back(a + (b - a) * t);
        }
        a = b;
        da = db;
    }
    return result;
}

} // namespace

int QuakeMapData::materialIndex(const std::string& name) {
    for (size_t i = 0; i < materials.size(); ++i) {
        if (materials[i] == name) {
            return static_cast<int>(i);
        }
    }
    materials.push_back(name);
    return static_cast<int>(materials.size()) - 1;
}

bool parseQuakeMap(const std::string& text, QuakeMapData& out) {
    const std::vector<Tok> toks = tokenize(text);
    size_t p = 0;

    auto parseVec = [&](glm::vec3& v) -> bool {
        if (p + 4 >= toks.size() || !isPunct(toks[p], '(')) return false;
        ++p;
        const float x = std::stof(toks[p].text);
        const float y = std::stof(toks[p + 1].text);
        const float z = std::stof(toks[p + 2].text);
        p += 3;
        if (!isPunct(toks[p], ')')) return false;
        ++p;
        v = convert(glm::vec3(x, y, z));
        return true;
    };

    auto parseBrush = [&](Brush& brush) -> bool {
        if (p >= toks.size() || !isPunct(toks[p], '{')) return false;
        ++p;
        brush.bmin = glm::vec3(1e30f);
        brush.bmax = glm::vec3(-1e30f);
        while (p < toks.size()) {
            if (isPunct(toks[p], '}')) {
                ++p;
                return true;
            }
            glm::vec3 p1, p2, p3;
            if (!parseVec(p1) || !parseVec(p2) || !parseVec(p3)) return false;
            if (p >= toks.size() || toks[p].kind == Tok::Punct) return false;
            const std::string tex = toks[p].text;
            ++p;
            // Skip the UV brackets and remaining numbers until the next plane.
            while (p < toks.size() && !isPunct(toks[p], '(') && !isPunct(toks[p], '}')) {
                ++p;
            }
            for (const glm::vec3* q : {&p1, &p2, &p3}) {
                brush.bmin = glm::min(brush.bmin, *q);
                brush.bmax = glm::max(brush.bmax, *q);
            }
            const glm::vec3 e1 = p2 - p1;
            const glm::vec3 e2 = p3 - p1;
            glm::vec3 n = glm::cross(e1, e2);
            const float len = glm::length(n);
            if (len < 1e-6f) {
                continue;
            }
            n /= len;
            Plane plane;
            plane.normal = n;
            plane.dist = glm::dot(n, p1);
            brush.planes.push_back(plane);
            brush.textures.push_back(tex);
        }
        return false;
    };

    while (p < toks.size()) {
        if (!isPunct(toks[p], '{')) {
            return false;
        }
        ++p;
        std::string classname;
        glm::vec3 origin(0.0f);
        float angle = 0.0f;
        bool hasOrigin = false;
        std::vector<Brush> brushes;

        while (p < toks.size()) {
            if (isPunct(toks[p], '}')) {
                ++p;
                break;
            }
            if (isPunct(toks[p], '{')) {
                Brush brush;
                if (!parseBrush(brush)) return false;
                brushes.push_back(std::move(brush));
                continue;
            }
            if (toks[p].kind != Tok::String) {
                return false;
            }
            const std::string key = toks[p].text;
            ++p;
            if (p >= toks.size() || toks[p].kind != Tok::String) {
                return false;
            }
            const std::string value = toks[p].text;
            ++p;

            if (key == "classname") {
                classname = value;
            } else if (key == "origin") {
                std::istringstream ss(value);
                glm::vec3 q;
                if (ss >> q.x >> q.y >> q.z) {
                    origin = convert(q);
                    hasOrigin = true;
                }
            } else if (key == "angle") {
                angle = std::stof(value);
            }
        }

        const bool isTrigger = classname.compare(0, 8, "trigger_") == 0;
        if (!isTrigger) {
            for (auto& b : brushes) {
                b.entityClass = classname;
                out.brushes.push_back(std::move(b));
            }
        }
        if (hasOrigin &&
            (classname == "info_player_deathmatch" || classname == "info_player_start" ||
             classname == "info_player_intermission")) {
            SpawnPoint sp;
            sp.position = origin;
            sp.yaw = angle;
            out.spawns.push_back(sp);
        }
    }
    return true;
}

void triangulateBrushes(QuakeMapData& data, TriangleMesh& out) {
    out.positions.clear();
    out.materialIndex.clear();
    out.normals.clear();

    const float BIG = 131072.0f;

    for (Brush& brush : data.brushes) {
        if (brush.planes.size() < 4) {
            continue;
        }
        glm::vec3 bmin(1e30f);
        glm::vec3 bmax(-1e30f);
        for (size_t i = 0; i < brush.planes.size(); ++i) {
            if (isSpecialTexture(brush.textures[i])) {
                continue;
            }
            const Plane& face = brush.planes[i];
            glm::vec3 t, s;
            makeBasis(face.normal, t, s);
            const glm::vec3 center = face.normal * face.dist;

            std::vector<glm::vec3> poly;
            poly.reserve(16);
            poly.push_back(center + (t + s) * BIG);
            poly.push_back(center + (-t + s) * BIG);
            poly.push_back(center + (-t - s) * BIG);
            poly.push_back(center + (t - s) * BIG);

            for (size_t j = 0; j < brush.planes.size() && poly.size() >= 3; ++j) {
                if (j == i) {
                    continue;
                }
                poly = clipPolygon(poly, brush.planes[j]);
            }
            if (poly.size() < 3) {
                continue;
            }
            for (const glm::vec3& v : poly) {
                bmin = glm::min(bmin, v);
                bmax = glm::max(bmax, v);
            }
            const int mat = data.materialIndex(brush.textures[i]);
            for (size_t k = 1; k + 1 < poly.size(); ++k) {
                out.positions.push_back(poly[0]);
                out.positions.push_back(poly[k]);
                out.positions.push_back(poly[k + 1]);
                out.materialIndex.push_back(mat);
                out.normals.push_back(face.normal);
            }
        }
        brush.bmin = bmin;
        brush.bmax = bmax;
    }
}

void buildBrushCollision(const QuakeMapData& data, BrushCollisionWorld& out) {
    for (const Brush& brush : data.brushes) {
        // Only worldspawn is static solid geometry. func_group/func_detail
        // brushes overlap the playable space and require CSG to resolve, so
        // they are excluded from the additive collision approximation.
        if (brush.entityClass != "worldspawn") {
            continue;
        }
        if (brush.bmin.x > brush.bmax.x) {
            continue; // no visible faces (e.g. clip/trigger), skip
        }
        std::vector<glm::vec3> normals;
        std::vector<float> dists;
        normals.reserve(brush.planes.size());
        dists.reserve(brush.planes.size());
        for (const Plane& plane : brush.planes) {
            normals.push_back(plane.normal);
            dists.push_back(plane.dist);
        }
        out.addBrush(normals, dists, AABB{brush.bmin, brush.bmax});
    }
}

} // namespace ot::map