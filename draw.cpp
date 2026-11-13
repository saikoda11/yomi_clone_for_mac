#include "draw.h"

#include <algorithm>
#include <cmath>

namespace sg {

const Color PLAYER_COLORS[2] = {{255, 96, 92, 255}, {78, 168, 255, 255}};

namespace {

// Joint order. "F" = front limb (toward the opponent), "B" = back limb.
enum J { HEAD, NECK, HIP, FE, FH, BE, BH, FK, FF, BK, BF, NJ };

struct Pose {
    Vector2 p[NJ];
};

Pose mk(std::initializer_list<float> v) {
    Pose r{};
    auto it = v.begin();
    for (int i = 0; i < NJ; ++i) {
        r.p[i].x = *it++;
        r.p[i].y = *it++;
    }
    return r;
}

// Local coordinates: feet at (0,0), facing right, up is negative y.
const Pose IDLE = mk({4, -127, 2, -110, 0, -66, 18, -88, 28, -104, -4, -88, 10, -100, 12, -34, 20, 0, -10, -34, -18, 0});
const Pose WALK_A = mk({6, -127, 4, -110, 0, -66, 18, -88, 28, -104, -4, -88, 10, -100, 18, -34, 28, 0, -4, -36, -22, 0});
const Pose WALK_B = mk({6, -127, 4, -110, 0, -66, 18, -88, 28, -104, -4, -88, 10, -100, 6, -36, -6, 0, 12, -34, 18, 0});
const Pose CROUCH = mk({8, -100, 6, -84, 0, -44, 20, -66, 28, -82, -2, -64, 12, -78, 22, -26, 20, 0, -14, -24, -18, 0});
const Pose DASH = mk({26, -112, 18, -98, 0, -60, 34, -84, 44, -96, -16, -80, -30, -72, 22, -32, 36, 0, -16, -26, -40, -6});
const Pose AIR = mk({2, -127, 0, -110, 0, -66, 18, -96, 28, -110, -16, -96, -24, -108, 14, -50, 4, -28, -10, -48, -16, -24});
const Pose JAB = mk({10, -126, 6, -109, 0, -66, 40, -104, 72, -108, -2, -88, 12, -100, 16, -34, 26, 0, -12, -34, -22, 0});
const Pose HEAVY_W = mk({-8, -124, -8, -107, -4, -66, -26, -96, -38, -114, -14, -84, 0, -96, 10, -34, 18, 0, -14, -34, -24, 0});
const Pose HEAVY = mk({24, -120, 18, -104, 4, -64, 58, -100, 104, -96, -10, -90, -26, -98, 30, -32, 46, 0, -14, -30, -32, 0});
const Pose SWEEP = mk({-24, -64, -16, -52, -6, -30, -24, -38, -34, -10, 6, -42, 16, -22, 34, -16, 104, -8, -24, -14, -10, 0});
const Pose RISING = mk({10, -142, 6, -124, 0, -76, 24, -152, 30, -196, -10, -102, -18, -90, 14, -40, 6, -6, -10, -36, -16, 0});
const Pose GRAB = mk({14, -122, 10, -106, 0, -64, 38, -98, 62, -92, 30, -86, 56, -80, 14, -34, 24, 0, -14, -32, -24, 0});
const Pose THROW = mk({-14, -124, -10, -108, 0, -64, -30, -128, -10, -150, -40, -112, -18, -144, 18, -34, 26, 0, -16, -32, -26, 0});
const Pose BLOCK = mk({-2, -124, -2, -107, -2, -64, 14, -104, 20, -130, 10, -96, 18, -120, 14, -32, 22, 0, -14, -32, -22, 0});
const Pose BOLT = mk({12, -124, 8, -108, 0, -66, 38, -100, 68, -100, 32, -96, 64, -94, 16, -34, 28, 0, -14, -32, -26, 0});
const Pose AIRSLASH = mk({8, -122, 4, -106, 0, -66, 42, -96, 86, -70, -14, -96, -24, -106, 18, -48, 14, -24, -8, -50, -18, -30});
const Pose DIVE = mk({-10, -118, -6, -102, 0, -62, -20, -92, -30, -104, 10, -90, -6, -100, 26, -36, 54, -4, -12, -50, -24, -34});
const Pose HURT = mk({-16, -118, -12, -102, -2, -62, -2, -84, 10, -74, -28, -92, -40, -100, 10, -32, 14, 0, -14, -32, -20, 0});
const Pose HURT_AIR = mk({-34, -100, -26, -88, 0, -60, -12, -100, 4, -118, -44, -76, -60, -86, 20, -40, 32, -14, 8, -36, 16, -6});
const Pose DOWN = mk({-58, -12, -42, -14, 0, -10, -28, -26, -18, -6, -36, -4, -22, -2, 20, -20, 46, -6, 22, -8, 50, -2});
const Pose BURST = mk({0, -130, 0, -112, 0, -66, 26, -128, 46, -152, -26, -128, -46, -152, 18, -34, 32, 0, -18, -34, -32, 0});

Pose lerp(const Pose &a, const Pose &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    t = t * t * (3 - 2 * t);
    Pose r;
    for (int i = 0; i < NJ; ++i) r.p[i] = {a.p[i].x + (b.p[i].x - a.p[i].x) * t, a.p[i].y + (b.p[i].y - a.p[i].y) * t};
    return r;
}

// Three-phase attack animation: wind up, hold the strike, recover.
Pose attack(const Fighter &f, const Pose &base, const Pose &wind, const Pose &ext) {
    const MoveData &m = moveData(f.move);
    int s = m.activeStart, e = m.activeEnd, total = m.total;
    if (f.frame < s) return lerp(base, wind, (f.frame + 1) / float(s));
    if (f.frame < e) return ext;
    return lerp(ext, base, (f.frame - e) / float(std::max(1, total - e)));
}

Pose poseFor(const Fighter &f, int clock) {
    using A = Action;
    switch (f.state) {
    case FState::Free: {
        if (!f.grounded) return AIR;
        Pose p = IDLE;
        float b = std::sin(clock * 0.08f) * 2.0f;
        for (int i : {HEAD, NECK, FE, FH, BE, BH}) p.p[i].y += b;
        return p;
    }
    case FState::Hitstun: return f.grounded ? HURT : HURT_AIR;
    case FState::Blockstun: return BLOCK;
    case FState::Grabbed: return HURT_AIR;
    case FState::Knockdown:
        if (!f.ko && f.timer < 12) return lerp(CROUCH, DOWN, f.timer / 12.0f);
        return DOWN;
    case FState::Acting: break;
    }

    switch (f.move) {
    case A::WalkF:
    case A::WalkB: return lerp(WALK_A, WALK_B, 0.5f + 0.5f * std::sin(f.frame * 0.55f));
    case A::DashF: return f.frame < 10 ? DASH : lerp(DASH, IDLE, (f.frame - 10) / 4.0f);
    case A::DashB: return lerp(CROUCH, IDLE, f.frame / 16.0f);
    case A::JumpN:
    case A::JumpF:
    case A::JumpB: return f.frame < 3 ? CROUCH : AIR;
    case A::Jab: return attack(f, IDLE, IDLE, JAB);
    case A::Heavy: return attack(f, IDLE, HEAVY_W, HEAVY);
    case A::Sweep: return attack(f, IDLE, CROUCH, SWEEP);
    case A::Rising: return attack(f, IDLE, CROUCH, RISING);
    case A::Grab: return attack(f, IDLE, IDLE, GRAB);
    case A::Block: return BLOCK;
    case A::Bolt:
        if (f.frame < 10) return lerp(IDLE, HEAVY_W, f.frame / 10.0f);
        if (f.frame < 18) return BOLT;
        return lerp(BOLT, IDLE, (f.frame - 18) / 14.0f);
    case A::Burst:
        if (f.frame < 6) return lerp(IDLE, CROUCH, f.frame / 6.0f);
        if (f.frame < 18) return BURST;
        return lerp(BURST, f.grounded ? IDLE : AIR, (f.frame - 18) / 10.0f);
    case A::AirSlash: return attack(f, AIR, AIR, AIRSLASH);
    case A::DiveKick: return f.frame < 4 ? lerp(AIR, DIVE, f.frame / 4.0f) : DIVE;
    case A::AirDashF: return f.frame < 9 ? DASH : AIR;
    case A::AirDashB: return AIR;
    case A::Land: return lerp(CROUCH, IDLE, f.frame / float(std::max(1, f.timer)));
    case A::Throw: return f.frame < 14 ? GRAB : lerp(THROW, IDLE, (f.frame - 14) / 16.0f);
    default: return f.grounded ? IDLE : AIR;
    }
}

Color shade(Color c, float k) {
    return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a};
}

Color withAlpha(Color c, float a) {
    c.a = (unsigned char)std::clamp(c.a * a, 0.0f, 255.0f);
    return c;
}

void limb(Vector2 a, Vector2 b, float thick, Color c) {
    DrawLineEx(a, b, thick, c);
    DrawCircleV(b, thick / 2, c);
}

} // namespace

void drawStage(int screenW, int screenH) {
    // sky
    DrawRectangleGradientV(0, 0, screenW, (int)FLOOR_Y, {22, 24, 38, 255}, {58, 48, 78, 255});
    // distant moon and pillars
    DrawCircle(screenW * 3 / 4, 170, 70, {255, 214, 170, 40});
    DrawCircle(screenW * 3 / 4, 170, 52, {255, 220, 180, 70});
    for (int i = 0; i < 9; ++i) {
        float x = 70.0f + i * 170.0f;
        float h = 140.0f + (i * 73 % 5) * 45.0f;
        DrawRectangleRec({x, FLOOR_Y - h, 46, h}, {36, 32, 54, 255});
        DrawRectangleRec({x - 8, FLOOR_Y - h - 12, 62, 12}, {44, 40, 64, 255});
    }
    // floor
    DrawRectangle(0, (int)FLOOR_Y, screenW, screenH - (int)FLOOR_Y, {28, 26, 38, 255});
    float vx = screenW / 2.0f;
    for (int x = -1200; x <= screenW + 1200; x += 90) {
        Vector2 a = {(float)x, FLOOR_Y};
        Vector2 b = {x + (x - vx) * 0.9f, (float)screenH};
        DrawLineEx(a, b, 1.5f, {60, 56, 82, 255});
    }
    for (int i = 1; i < 8; ++i) {
        float y = FLOOR_Y + i * i * 4.5f;
        DrawLineEx({0, y}, {(float)screenW, y}, 1.0f, {52, 48, 72, 255});
    }
    DrawLineEx({0, FLOOR_Y}, {(float)screenW, FLOOR_Y}, 3, {120, 110, 160, 255});
    // stage walls
    for (float wx : {STAGE_LEFT - 28.0f, STAGE_RIGHT + 28.0f}) {
        DrawRectangleRec({wx - 4, FLOOR_Y - 420, 8, 420}, {90, 82, 124, 160});
        DrawCircleV({wx, FLOOR_Y - 420}, 8, {150, 140, 200, 200});
    }
}

void drawFighter(const Fighter &f, int index, float alpha, Color tint, int clock) {
    Pose p = poseFor(f, clock);
    Color base = tint.a > 0 ? tint : PLAYER_COLORS[index];
    bool ghost = alpha < 0.99f;

    float shakeX = 0;
    if (!ghost && f.state == FState::Hitstun && f.timer > 0) shakeX = (clock % 4 < 2 ? -2.0f : 2.0f);

    Vector2 w[NJ];
    for (int i = 0; i < NJ; ++i) w[i] = {f.pos.x + p.p[i].x * f.facing + shakeX, f.pos.y + p.p[i].y};

    // shadow
    if (!ghost) {
        float h = std::clamp((FLOOR_Y - f.pos.y) / 300.0f, 0.0f, 0.7f);
        DrawEllipse((int)f.pos.x, (int)FLOOR_Y + 2, 34 * (1 - h), 7 * (1 - h), {0, 0, 0, 90});
    }

    Color front = withAlpha(base, alpha);
    Color back = withAlpha(shade(base, 0.62f), alpha);
    float t = 7.0f;

    bool invul = isInvulnerable(f) && f.state == FState::Acting;
    if (invul && !ghost) {
        Color glow = {255, 255, 255, (unsigned char)(120 + 80 * std::sin(clock * 0.6f))};
        float g = t + 6;
        limb(w[NECK], w[BE], g, glow); limb(w[BE], w[BH], g, glow);
        limb(w[HIP], w[BK], g, glow); limb(w[BK], w[BF], g, glow);
        limb(w[NECK], w[HIP], g, glow);
        limb(w[NECK], w[FE], g, glow); limb(w[FE], w[FH], g, glow);
        limb(w[HIP], w[FK], g, glow); limb(w[FK], w[FF], g, glow);
        DrawCircleV(w[HEAD], 17, glow);
    }

    // back limbs
    limb(w[NECK], w[BE], t, back);
    limb(w[BE], w[BH], t, back);
    limb(w[HIP], w[BK], t, back);
    limb(w[BK], w[BF], t, back);
    // torso + head
    limb(w[NECK], w[HIP], t + 2, front);
    DrawCircleV(w[NECK], (t + 2) / 2, front);
    DrawCircleV(w[HEAD], 13, front);
    if (!ghost) {
        Vector2 eye = {w[HEAD].x + 5.0f * f.facing, w[HEAD].y - 2};
        DrawCircleV(eye, 2.5f, {20, 20, 30, 255});
    }
    // front limbs
    limb(w[NECK], w[FE], t, front);
    limb(w[FE], w[FH], t, front);
    limb(w[HIP], w[FK], t, front);
    limb(w[FK], w[FF], t, front);

    // guard shield
    bool guarding = (f.state == FState::Acting && f.move == Action::Block) || f.state == FState::Blockstun;
    if (guarding && !ghost) {
        Vector2 c = {f.pos.x + 10.0f * f.facing, f.pos.y - 70};
        float a0 = f.facing > 0 ? -60.0f : 120.0f;
        DrawRing(c, 58, 64, a0, a0 + 120, 24, {150, 220, 255, 170});
    }
}

void drawProjectiles(const World &w, float alpha) {
    for (const auto &p : w.proj) {
        Color c = PLAYER_COLORS[p.owner];
        for (int i = 6; i >= 1; --i) {
            Vector2 tp = {p.pos.x - p.vx * i * 1.6f, p.pos.y};
            DrawCircleV(tp, 12.0f - i, withAlpha(c, alpha * (0.5f - i * 0.07f)));
        }
        DrawCircleV(p.pos, 18, withAlpha(c, alpha * 0.35f));
        DrawCircleV(p.pos, 12, withAlpha(c, alpha));
        DrawCircleV(p.pos, 6, withAlpha(WHITE, alpha));
    }
}

void drawBoxes(const World &w) {
    for (int i = 0; i < 2; ++i) {
        const Fighter &f = w.f[i];
        Color hc = isInvulnerable(f) ? Color{255, 255, 255, 140} : Color{80, 255, 140, 160};
        DrawRectangleLinesEx(hurtbox(f), 2, hc);
        Rectangle hb;
        if (activeHitbox(f, hb)) {
            DrawRectangleRec(hb, {255, 60, 60, 70});
            DrawRectangleLinesEx(hb, 2, {255, 60, 60, 220});
        }
    }
}

void Fx::clear() {
    parts.clear();
    texts.clear();
    rings.clear();
    shake = 0;
}

void Fx::onEvent(const Event &e, const World &w) {
    auto burst = [&](Vector2 at, int n, Color c, float speed, float size) {
        for (int i = 0; i < n; ++i) {
            float ang = GetRandomValue(0, 628) / 100.0f;
            float sp = speed * GetRandomValue(30, 100) / 100.0f;
            float life = GetRandomValue(16, 30);
            parts.push_back({at, {std::cos(ang) * sp, std::sin(ang) * sp}, life, life, size, c});
        }
    };
    switch (e.type) {
    case EventType::Hit: {
        Color c = PLAYER_COLORS[1 - e.target];
        burst(e.pos, 16 + e.damage / 6, c, 9, 4);
        burst(e.pos, 6, WHITE, 6, 3);
        rings.push_back({e.pos, 18, 18, 30.0f + e.damage * 0.4f, WHITE});
        texts.push_back({{e.pos.x, e.pos.y - 30}, std::to_string(e.damage), 50, {255, 240, 200, 255}, 30});
        shake = std::max(shake, 3.0f + e.damage / 14.0f);
        break;
    }
    case EventType::Block:
        burst(e.pos, 10, {150, 220, 255, 255}, 6, 3);
        texts.push_back({{e.pos.x, e.pos.y - 30}, "BLOCK", 40, {150, 220, 255, 255}, 24});
        shake = std::max(shake, 2.0f);
        break;
    case EventType::Grab:
        texts.push_back({{e.pos.x, e.pos.y - 40}, "GRAB!", 45, {255, 220, 120, 255}, 28});
        rings.push_back({e.pos, 14, 14, 40, {255, 220, 120, 255}});
        break;
    case EventType::Tech:
        texts.push_back({{e.pos.x, e.pos.y - 40}, "TECH", 45, {220, 220, 255, 255}, 28});
        burst(e.pos, 14, {220, 220, 255, 255}, 7, 3);
        break;
    case EventType::Clash:
        burst(e.pos, 20, {255, 255, 200, 255}, 8, 4);
        rings.push_back({e.pos, 16, 16, 45, {255, 255, 200, 255}});
        texts.push_back({{e.pos.x, e.pos.y - 30}, "CLASH", 40, {255, 255, 200, 255}, 24});
        break;
    case EventType::Burst:
        rings.push_back({e.pos, 24, 24, 140, PLAYER_COLORS[e.target]});
        rings.push_back({e.pos, 18, 18, 90, WHITE});
        texts.push_back({{e.pos.x, e.pos.y - 110}, "BURST", 50, PLAYER_COLORS[e.target], 30});
        shake = std::max(shake, 6.0f);
        break;
    case EventType::Land:
        for (int i = 0; i < 12; ++i) {
            float dir = i % 2 ? 1.0f : -1.0f;
            float life = GetRandomValue(18, 30);
            parts.push_back({{e.pos.x, FLOOR_Y - 2},
                             {dir * GetRandomValue(10, 40) / 10.0f, -GetRandomValue(5, 20) / 10.0f},
                             life, life, 5, {150, 140, 180, 255}});
        }
        shake = std::max(shake, 3.0f);
        break;
    case EventType::Ko: shake = 16; break;
    case EventType::Whiff: break;
    }
    (void)w;
}

void Fx::update() {
    for (auto &p : parts) {
        p.pos.x += p.vel.x;
        p.pos.y += p.vel.y;
        p.vel.x *= 0.9f;
        p.vel.y = p.vel.y * 0.9f + 0.15f;
        p.life -= 1;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle &p) { return p.life <= 0; }), parts.end());
    for (auto &t : texts) {
        t.pos.y -= 0.8f;
        t.life -= 1;
    }
    texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText &t) { return t.life <= 0; }), texts.end());
    for (auto &r : rings) r.life -= 1;
    rings.erase(std::remove_if(rings.begin(), rings.end(), [](const Ring &r) { return r.life <= 0; }), rings.end());
    shake *= 0.85f;
    if (shake < 0.1f) shake = 0;
}

void Fx::drawWorld() const {
    for (const auto &r : rings) {
        float k = 1 - r.life / r.maxLife;
        float rad = r.radius * (0.3f + 0.7f * k);
        DrawRing(r.pos, rad - 4, rad, 0, 360, 48, withAlpha(r.color, 1 - k));
    }
    for (const auto &p : parts) {
        float k = p.life / p.maxLife;
        DrawCircleV(p.pos, p.size * k + 1, withAlpha(p.color, k));
    }
    for (const auto &t : texts) {
        float k = std::min(1.0f, t.life / 20.0f);
        int w = MeasureText(t.text.c_str(), t.size);
        DrawText(t.text.c_str(), (int)t.pos.x - w / 2 + 2, (int)t.pos.y + 2, t.size, withAlpha(BLACK, k * 0.6f));
        DrawText(t.text.c_str(), (int)t.pos.x - w / 2, (int)t.pos.y, t.size, withAlpha(t.color, k));
    }
}

} // namespace sg
