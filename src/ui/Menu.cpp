#include "ui/Menu.h"

#include "render/Font.h"

namespace ot {

namespace {

const glm::vec3 kTitleColor(1.0f, 1.0f, 1.0f);
const glm::vec3 kItemColor(0.62f, 0.66f, 0.72f);
const glm::vec3 kSelectedColor(0.25f, 1.0f, 0.45f);
const glm::vec3 kHintColor(0.42f, 0.46f, 0.52f);

void pushCentered(std::vector<float>& out, const std::string& text,
                  float cx, float y, float scale, const glm::vec3& color) {
    const float w = textWidth(text, scale);
    buildText(out, text, cx - w * 0.5f, y, scale, color);
}

} // namespace

void Menu::reset() {
    m_items = {"PLAY OFFLINE", "JOIN SERVER", "GENERATE MAP", "QUIT"};
    m_selected = 0;
    m_ipEntry = false;
    m_ip = "127.0.0.1";
}

void Menu::move(int delta) {
    const int n = static_cast<int>(m_items.size());
    m_selected = (m_selected + delta + n) % n;
}

MenuAction Menu::activate() {
    if (m_ipEntry) {
        m_ipEntry = false;
        return MenuAction::JoinServer;
    }
    switch (m_selected) {
        case 0: return MenuAction::PlayOffline;
        case 1: m_ipEntry = true; return MenuAction::None;
        case 2: return MenuAction::GenerateMap;
        default: return MenuAction::Quit;
    }
}

MenuAction Menu::cancel() {
    if (m_ipEntry) {
        m_ipEntry = false;
        return MenuAction::None;
    }
    return MenuAction::Quit;
}

MenuAction Menu::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        const auto sc = event.key.keysym.scancode;
        if (m_ipEntry) {
            if (sc == SDL_SCANCODE_BACKSPACE && !m_ip.empty()) {
                m_ip.pop_back();
            } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                return activate();
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                m_ipEntry = false;
                return MenuAction::None;
            }
        } else {
            if (sc == SDL_SCANCODE_W || sc == SDL_SCANCODE_UP) {
                move(-1);
            } else if (sc == SDL_SCANCODE_S || sc == SDL_SCANCODE_DOWN) {
                move(1);
            } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
                       sc == SDL_SCANCODE_SPACE) {
                return activate();
            } else if (sc == SDL_SCANCODE_ESCAPE) {
                return cancel();
            }
        }
    } else if (event.type == SDL_TEXTINPUT && m_ipEntry) {
        for (const char* p = event.text.text; *p && m_ip.size() < 21; ++p) {
            const char c = *p;
            if ((c >= '0' && c <= '9') || c == '.') {
                m_ip.push_back(c);
            }
        }
    } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        const auto b = event.cbutton.button;
        if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            move(-1);
        } else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            move(1);
        } else if (b == SDL_CONTROLLER_BUTTON_A) {
            return activate();
        } else if (b == SDL_CONTROLLER_BUTTON_B) {
            return cancel();
        }
    }
    return MenuAction::None;
}

void Menu::buildVertices(std::vector<float>& out) const {
    out.clear();

    // Title.
    pushCentered(out, "OPEN TOURNAMENT", 0.0f, 0.66f, 0.14f, kTitleColor);

    if (m_ipEntry) {
        pushCentered(out, "ENTER SERVER IP", 0.0f, 0.28f, 0.08f, kSelectedColor);
        pushCentered(out, m_ip + "_", 0.0f, 0.10f, 0.10f, kTitleColor);
        pushCentered(out, "ENTER: CONNECT   ESC: BACK", 0.0f, -0.82f, 0.05f, kHintColor);
        return;
    }

    const float itemScale = 0.08f;
    const float step = 0.14f;
    const float top = 0.22f;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const float y = top - static_cast<float>(i) * step;
        const bool selected = static_cast<int>(i) == m_selected;
        const std::string label = (selected ? "> " : "  ") + m_items[i];
        pushCentered(out, label, 0.0f, y, itemScale,
                     selected ? kSelectedColor : kItemColor);
    }

    pushCentered(out, "W/S: SELECT   ENTER: OK   ESC: QUIT", 0.0f, -0.82f, 0.05f, kHintColor);
}

} // namespace ot
