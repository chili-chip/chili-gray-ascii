#pragma once
// Wind and grass. No drawing here, so the self-test can run it headless.
//
// The wind is a field w(x, t): a steady breeze, slow swell, and gusts that
// travel across the dunes from left to right. Every blade of grass is a
// damped spring pushed by the wind at its own x, so a gust shows as a wave
// rolling through the grass before it reaches you.
//
// A "pressed" blade was stepped on: it leans against the wind and barely
// moves. That is the clue the detective reads with the torch.

#include <cmath>
#include <cstdint>

namespace wind {

constexpr int MAX_GUSTS = 4;

struct Gust {
    float x = 0;      // centre, world px
    float speed = 0;  // px per second, to the right
    float width = 1;  // px
    float power = 0;  // added wind at the centre
};

struct Field {
    Gust gusts[MAX_GUSTS];
    float t = 0;
    uint32_t seed = 12345;

    float rnd() {  // 0..1
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return float(seed & 0xffffff) / float(0x1000000);
    }

    void spawn(Gust &g, float left_edge) {
        g.width = 30.0f + rnd() * 50.0f;
        g.speed = 55.0f + rnd() * 45.0f;
        g.power = 0.35f + rnd() * 0.75f;
        g.x = left_edge - g.width * 2.0f - rnd() * 260.0f;
    }

    void reset(float left_edge) {
        for(int i = 0; i < MAX_GUSTS; i++) {
            spawn(gusts[i], left_edge);
            gusts[i].x += 300.0f * float(i);  // spread over the screen at start
        }
    }

    // left/right: the part of the world that matters now (camera +- margin)
    void step(float dt, float left_edge, float right_edge) {
        t += dt;
        for(auto &g : gusts) {
            g.x += g.speed * dt;
            if(g.x - g.width * 2.0f > right_edge) spawn(g, left_edge);
        }
    }

    // Wind strength at world x: about 0.2 calm .. 1.6 in a strong gust.
    float at(float x) const {
        float w = 0.32f + 0.12f * sinf(t * 0.23f) + 0.05f * sinf(x * 0.071f + t * 2.1f);
        for(const auto &g : gusts) {
            float d = (x - g.x) / g.width;
            if(d > -3.0f && d < 3.0f) w += g.power * expf(-d * d);
        }
        return w;
    }
};

struct Blade {
    float x = 0;         // world x of the root
    float height = 12;   // px
    float angle = 0;     // radians, + leans right (with the wind)
    float vel = 0;
    float stiff = 1;     // how far the wind bends it
    float phase = 0;     // flutter phase
    float rest = 0;      // lean with no wind (blades in a tuft fan out)
    bool pressed = false;
};

constexpr float SPRING = 34.0f;
constexpr float DAMPING = 3.2f;
constexpr float PRESSED_ANGLE = -0.55f;  // against the wind
constexpr float MAX_ANGLE = 1.25f;

// Where the blade wants to be with this wind.
inline float target_angle(const Blade &b, float w, float t) {
    if(b.pressed) return PRESSED_ANGLE + 0.03f * sinf(t * 1.7f + b.phase);
    return b.rest + w * 0.55f * b.stiff + 0.07f * w * sinf(t * 7.0f + b.phase);
}

inline void step_blade(Blade &b, float w, float t, float dt) {
    float target = target_angle(b, w, t);
    float k = b.pressed ? SPRING * 3.0f : SPRING;
    float acc = k * (target - b.angle) - DAMPING * b.vel;
    b.vel += acc * dt;
    b.angle += b.vel * dt;
    if(b.angle > MAX_ANGLE) { b.angle = MAX_ANGLE; b.vel = 0; }
    if(b.angle < -MAX_ANGLE) { b.angle = -MAX_ANGLE; b.vel = 0; }
}

// Horizontal offset of a point at fraction f (0 root .. 1 tip) of the
// blade: the tip bends most, like a real stem.
inline float bend_offset(const Blade &b, float f) {
    return sinf(b.angle) * b.height * f * f;
}

// Which ASCII character draws the piece of stem between two offsets that
// are `step` px apart vertically.
inline char stem_char(float dx) {
    if(dx > 1.6f) return '/';
    if(dx > 0.55f) return ')';
    if(dx < -1.6f) return '\\';
    if(dx < -0.55f) return '(';
    return '|';
}

}  // namespace wind
