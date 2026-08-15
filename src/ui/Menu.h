#pragma once

#include <SDL.h>

#include <string>
#include <vector>

namespace ot {

enum class MenuAction {
    None,
    PlayOffline,
    JoinServer,
    GenerateMap,
    Quit,
};

// Simple keyboard/controller-driven main menu. Owns selection state and an
// optional server-IP entry field; produces overlay vertex data for rendering.
class Menu {
public:
    void reset();

    // Process a single SDL event. Returns an action when the user confirms a
    // selection (or requests to quit).
    MenuAction handleEvent(const SDL_Event& event);

    // Build 2D overlay triangles (position + color) for the whole menu.
    void buildVertices(std::vector<float>& out) const;

    bool enteringIp() const { return m_ipEntry; }
    const std::string& serverIp() const { return m_ip; }

private:
    void move(int delta);
    MenuAction activate();
    MenuAction cancel();

    std::vector<std::string> m_items;
    int m_selected = 0;
    bool m_ipEntry = false;
    std::string m_ip = "127.0.0.1";
};

} // namespace ot
