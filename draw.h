#pragma once
#include "raylib.h"
#include "sim.h"

#include <string>
#include <vector>

namespace sg {

extern const Color PLAYER_COLORS[2];

void drawStage(int screenW, int screenH);
// alpha < 1 draws a translucent ghost; tint overrides the player color when a > 0.
void drawFighter(const Fighter &f, int index, float alpha = 1.0f, Color tint = {0, 0, 0, 0}, int clock = 0);
void drawProjectiles(const World &w, float alpha = 1.0f);
void drawBoxes(const World &w);

struct Particle {
    Vector2 pos, vel;
    float life, maxLife, size;
    Color color;
};

struct FloatText {
    Vector2 pos;
    std::string text;
    float life;
    Color color;
    int size;
};

struct Ring {
    Vector2 pos;
    float life, maxLife, radius;
    Color color;
};

// Purely visual effects (not part of the deterministic simulation).
struct Fx {
    std::vector<Particle> parts;
    std::vector<FloatText> texts;
    std::vector<Ring> rings;
    float shake = 0;

    void onEvent(const Event &e, const World &w);
    void update();
    void drawWorld() const;
    void clear();
};

} // namespace sg
