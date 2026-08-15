#include "input/Input.h"

#include <cmath>
#include <cstdio>

namespace ot {

bool Input::init() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            openController(i);
            break;
        }
    }
    return true;
}

void Input::shutdown() {
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
}

void Input::openController(int index) {
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
    }
    m_controller = SDL_GameControllerOpen(index);
    if (m_controller) {
        std::printf("[ot] controller opened: %s\n", SDL_GameControllerName(m_controller));
    }
}

void Input::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        openController(event.cdevice.which);
    } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        if (m_controller &&
            SDL_GameControllerGetJoystick(m_controller) &&
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_controller)) ==
                event.cdevice.which) {
            SDL_GameControllerClose(m_controller);
            m_controller = nullptr;
        }
    }
}

float Input::controllerAxis(int axis) const {
    if (!m_controller) {
        return 0.0f;
    }
    const Sint16 raw = SDL_GameControllerGetAxis(m_controller,
                                                 static_cast<SDL_GameControllerAxis>(axis));
    float value = static_cast<float>(raw) / 32768.0f;
    if (std::fabs(value) * 32768.0f < m_deadzone) {
        value = 0.0f;
    }
    return value;
}

glm::vec2 Input::moveAxis() const {
    glm::vec2 axis(0.0f);

    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    if (kb[SDL_SCANCODE_W] || kb[SDL_SCANCODE_UP]) axis.y += 1.0f;
    if (kb[SDL_SCANCODE_S] || kb[SDL_SCANCODE_DOWN]) axis.y -= 1.0f;
    if (kb[SDL_SCANCODE_D] || kb[SDL_SCANCODE_RIGHT]) axis.x += 1.0f;
    if (kb[SDL_SCANCODE_A] || kb[SDL_SCANCODE_LEFT]) axis.x -= 1.0f;

    axis.x += controllerAxis(SDL_CONTROLLER_AXIS_LEFTX);
    axis.y -= controllerAxis(SDL_CONTROLLER_AXIS_LEFTY); // stick up = forward

    const float len = glm::length(axis);
    if (len > 1.0f) {
        axis /= len;
    }
    return axis;
}

glm::vec2 Input::lookDelta(float dt) const {
    glm::vec2 delta(0.0f);

    int mouseX = 0;
    int mouseY = 0;
    SDL_GetRelativeMouseState(&mouseX, &mouseY);

    constexpr float kMouseSensitivity = 0.0022f;
    delta.x += static_cast<float>(mouseX) * kMouseSensitivity;
    delta.y -= static_cast<float>(mouseY) * kMouseSensitivity;

    constexpr float kStickSensitivity = 2.8f; // rad/s at full deflection
    delta.x += controllerAxis(SDL_CONTROLLER_AXIS_RIGHTX) * kStickSensitivity * dt;
    delta.y -= controllerAxis(SDL_CONTROLLER_AXIS_RIGHTY) * kStickSensitivity * dt;

    return delta;
}

bool Input::jumpHeld() const {
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    if (kb[SDL_SCANCODE_SPACE]) {
        return true;
    }
    if (m_controller &&
        SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_A)) {
        return true;
    }
    return false;
}

bool Input::fireHeld() const {
    const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    if (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        return true;
    }
    if (m_controller &&
        controllerAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f) {
        return true;
    }
    return false;
}

bool Input::aimHeld() const {
    const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
    if (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
        return true;
    }
    if (m_controller &&
        controllerAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5f) {
        return true;
    }
    return false;
}

} // namespace ot
