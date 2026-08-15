#pragma once

#include <SDL.h>

#include <glm/glm.hpp>

namespace ot {

class Input {
public:
    bool init();
    void shutdown();

    void handleEvent(const SDL_Event& event);

    // x = strafe (-1..1), y = forward (-1..1).
    glm::vec2 moveAxis() const;

    // Per-frame look deltas (radians-ish) combining mouse + right stick.
    glm::vec2 lookDelta(float dt) const;

    // Raw right-stick position (x right, y up), deadzone applied.
    glm::vec2 rightStickAxis() const;

    bool jumpHeld() const;

    // Right trigger / left mouse button.
    bool fireHeld() const;

    // Left trigger / right mouse button (aim down sights).
    bool aimHeld() const;

private:
    float controllerAxis(int axis) const;
    void openController(int index);

    SDL_GameController* m_controller = nullptr;
    int m_deadzone = 8000;
};

} // namespace ot
