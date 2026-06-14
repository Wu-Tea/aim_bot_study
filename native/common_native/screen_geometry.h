#pragma once

namespace common_native {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Box2f {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct ScreenSize {
    float width = 0.0f;
    float height = 0.0f;
};

}  // namespace common_native
