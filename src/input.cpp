//
// Created by super on 27/10/2025.
//

#include "input.h"

namespace cpt {
    input::input() : keys{} {
    }

    void input::updateKeys(KEY key, KEY_STATE action) {
        this->keys[static_cast<int>(key)] = action;
    }

    void input::updateMousePos(double x, double y) {
        //if (ImGui::GetIO().WantCaptureMouse) return;
        this->mouseX = x;
        this->mouseY = y;
    }

    bool input::keyHeld(KEY key) const {
        //if (ImGui::GetIO().WantCaptureKeyboard) return false;
        const auto idx = static_cast<int>(key);
        return keys[idx] == KEY_STATE::PRESSED || keys[idx] == KEY_STATE::REPEAT;
    }

    void input::resetKeys() {
        for (auto& key : keys) {
            key = KEY_STATE::RELEASED;
        }
    }

    void input::getMousePos(double &x, double &y) const {
        x = mouseX;
        y = mouseY;
    }
} // cpt