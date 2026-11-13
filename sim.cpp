#include "sim.h"

#include <algorithm>
#include <cmath>

namespace sg {

namespace {

using A = Action;

HitData hit(int dmg, int hs, Vector2 kb, bool kd, Rectangle box) {
    HitData h;
    h.damage = dmg;
    h.hitstun = hs;
    h.kb = kb;
    h.knockdown = kd;
    h.box = box;
    return h;
}

// name, desc, air, total, activeStart, activeEnd, invulStart, invulEnd, cost, grab, hit
const MoveData MOVES[] = {
    {"Continue", "Keep doing what you are doing.", false, 1, -1, -1, -1, -1, 0, false, {}},
    {"Wait", "Hold still for a moment. Short, safe, reactive.", false, 6, -1, -1, -1, -1, 0, false, {}},
    {"Walk Fwd", "Step toward the opponent.", false, 12, -1, -1, -1, -1, 0, false, {}},
    {"Walk Back", "Step away from the opponent.", false, 12, -1, -1, -1, -1, 0, false, {}},
    {"Dash Fwd", "Fast burst of ground speed.", false, 14, -1, -1, -1, -1, 0, false, {}},
    {"Backdash", "Slide back. Invulnerable for the first 7 frames.", false, 16, -1, -1, 0, 7, 0, false, {}},
    {"Jump Up", "Leap straight up. Dodges low attacks. Act again in the air.", false, 10, -1, -1, -1, -1, 0, false, {}},
    {"Jump Fwd", "Leap toward the opponent. Can cross over them.", false, 10, -1, -1, -1, -1, 0, false, {}},
    {"Jump Back", "Leap away from the opponent.", false, 10, -1, -1, -1, -1, 0, false, {}},
    {"Jab", "Fast poke. Low damage, links into itself up close.", false, 15, 4, 7, -1, -1, 0, false,
     hit(40, 16, {5, 0}, false, {16, -122, 62, 26})},
    {"Heavy", "Slow lunging strike. Big damage, pops them up.", false, 34, 12, 16, -1, -1, 0, false,
     hit(120, 24, {9, -7}, false, {22, -118, 92, 44})},
    {"Sweep", "Long low kick. Knocks down. Loses to jumps.", false, 30, 9, 13, -1, -1, 0, false,
     hit(70, 22, {4, -4}, true, {10, -28, 104, 28})},
    {"Rising", "Invulnerable anti-air uppercut. Huge recovery if it misses.", false, 38, 5, 11, 0, 9, 0, false,
     hit(100, 30, {3, -16}, false, {-6, -205, 74, 150})},
    {"Grab", "Short-range throw. Beats Block, loses to any attack.", false, 28, 5, 7, -1, -1, 0, true,
     hit(110, 30, {7, -11}, true, {12, -120, 58, 80})},
    {"Block", "Guard against attacks from the front. Grabs go right through.", false, 20, -1, -1, -1, -1, 0, false, {}},
    {"Bolt", "Throw an energy bolt across the stage. Costs 25 meter.", false, 32, -1, -1, -1, -1, BOLT_COST, false,
     hit(60, 20, {6, -3}, false, {0, 0, 0, 0})},
    {"Burst", "Invulnerable shockwave. Usable even while being hit. Costs 60 meter.", false, 28, 6, 10, 0, 18, BURST_COST, false,
     hit(20, 26, {12, -10}, true, {-100, -180, 200, 200})},
    {"Drift", "Fall until you land. No landing lag.", true, -1, -1, -1, -1, -1, 0, false, {}},
    {"Air Slash", "Wide aerial swipe. Some landing lag.", true, 22, 6, 11, -1, -1, 0, false,
     hit(70, 20, {5, -5}, false, {8, -112, 80, 72})},
    {"Dive Kick", "Rocket down at an angle. Bounces off on hit, big lag if blocked.", true, -1, 4, 9999, -1, -1, 0, false,
     hit(80, 18, {7, -2}, false, {4, -40, 58, 48})},
    {"Airdash Fwd", "Dart forward through the air. Once per jump.", true, 12, -1, -1, -1, -1, 0, false, {}},
    {"Airdash Back", "Dart backward through the air. Once per jump.", true, 12, -1, -1, -1, -1, 0, false, {}},
    {"Landing", "", false, -1, -1, -1, -1, -1, 0, false, {}},
    {"Throw", "", false, 30, -1, -1, 0, 30, 0, false, {}},
};
static_assert(sizeof(MOVES) / sizeof(MOVES[0]) == (size_t)Action::COUNT, "move table out of sync");

int sgn(float v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

void clampMeter(Fighter &f) { f.meter = std::clamp(f.meter, 0, MAX_METER); }

bool noGravity(const Fighter &f) {
    if (f.state == FState::Grabbed) return true;
    if (f.state != FState::Acting) return false;
    switch (f.move) {
    case A::Burst: return f.frame < 16;
    case A::AirDashF:
    case A::AirDashB: return f.frame < 9;
    case A::DiveKick: return true;
    default: return false;
    }
}

void becomeFree(Fighter &f) {
    f.state = FState::Free;
    f.move = A::Continue;
    f.frame = 0;
    f.timer = 0;
    f.combo = 0;
    f.launched = false;
}

void applyHit(World &w, int a, int d, const HitData &h, int dir, Vector2 at, std::vector<Event> *ev,
              bool unblockable) {
    Fighter &A = w.f[a];
    Fighter &D = w.f[d];

    bool blocking = !unblockable && D.grounded &&
                    ((D.state == FState::Acting && D.move == Action::Block && D.facing == -dir) ||
                     D.state == FState::Blockstun);
    if (blocking) {
        D.state = FState::Blockstun;
        D.move = A::Continue;
        D.timer = h.hitstun * 7 / 10 + 2;
        D.vel = {dir * h.kb.x * 0.9f, 0};
        D.meter += 5;
        A.meter += 2;
        clampMeter(A);
        clampMeter(D);
        w.hitstop = 4;
        if (ev) ev->push_back({EventType::Block, at, 0, d});
        return;
    }

    float scale = std::max(0.3f, 1.0f - 0.12f * D.combo);
    int dmg = std::max(1, (int)std::lround(h.damage * scale));
    D.hp = std::max(0, D.hp - dmg);
    D.combo++;
    D.state = FState::Hitstun;
    D.move = A::Continue;
    D.frame = 0;
    D.timer = std::max(8, h.hitstun - 2 * (D.combo - 1));
    D.vel = {dir * h.kb.x, h.kb.y};
    D.facing = -dir;
    if (h.kb.y < 0 || !D.grounded) {
        D.grounded = false;
        D.launched = true;
    }
    if (h.knockdown && D.grounded) {
        D.vel.y = -5;
        D.grounded = false;
        D.launched = true;
    }
    A.meter += dmg / 4;
    D.meter += dmg / 6;
    clampMeter(A);
    clampMeter(D);
    w.hitstop = 5 + dmg / 25;

    // At the wall the attacker gets pushed back instead.
    if ((D.pos.x <= STAGE_LEFT + 2 && dir < 0) || (D.pos.x >= STAGE_RIGHT - 2 && dir > 0))
        A.vel.x -= dir * h.kb.x * 0.7f;

    if (ev) ev->push_back({EventType::Hit, at, dmg, d});

    if (D.hp == 0 && !D.ko) {
        D.ko = true;
        D.timer = 99999;
        D.launched = true;
        if (D.grounded || D.vel.y > -8) D.vel.y = -9;
        D.grounded = false;
        w.hitstop = 24;
        if (ev) ev->push_back({EventType::Ko, at, 0, d});
    }
}

void startMove(World &w, int p, Action a) {
    Fighter &f = w.f[p];
    const Fighter &o = w.f[1 - p];
    if (a == A::Continue) {
        if (f.state != FState::Free) return;
        a = f.grounded ? A::Wait : A::Drift;
    }
    const MoveData &m = moveData(a);
    if (f.state == FState::Free || a == A::Burst) {
        int d = sgn(o.pos.x - f.pos.x);
        if (d) f.facing = d;
    }
    f.meter -= m.cost;
    clampMeter(f);
    f.state = FState::Acting;
    f.move = a;
    f.frame = 0;
    f.timer = 0;
    f.connected = false;
    if (a == A::Burst) {
        f.vel = {0, 0};
        f.launched = false;
        f.combo = 0;
    }
    if (a == A::AirDashF || a == A::AirDashB) f.airDashUsed = true;
}

void land(Fighter &f, int p, std::vector<Event> *ev) {
    f.grounded = true;
    f.vel.y = 0;
    f.airDashUsed = false;
    if (f.state == FState::Hitstun && f.launched) {
        f.state = FState::Knockdown;
        f.timer = f.ko ? 99999 : 34;
        f.vel.x *= 0.4f;
        f.launched = false;
        if (ev) ev->push_back({EventType::Land, f.pos, 0, p});
    } else if (f.state == FState::Acting && moveData(f.move).air) {
        int lag = 0;
        switch (f.move) {
        case A::DiveKick: lag = f.connected ? 4 : 13; break;
        case A::AirSlash: lag = 7; break;
        case A::AirDashF:
        case A::AirDashB: lag = 4; break;
        default: lag = 0;
        }
        if (lag > 0) {
            f.move = A::Land;
            f.frame = 0;
            f.timer = lag;
        } else {
            becomeFree(f);
        }
    } else if (f.state == FState::Hitstun) {
        f.launched = false;
    }
}

void logic(World &w, int p, std::vector<Event> *ev) {
    Fighter &f = w.f[p];
    Fighter &o = w.f[1 - p];
    bool friction = f.grounded;
    if (f.state == FState::Acting) {
        int fr = f.frame;
        switch (f.move) {
        case A::WalkF:
            f.vel.x = f.facing * 4.5f;
            friction = false;
            break;
        case A::WalkB:
            f.vel.x = -f.facing * 3.5f;
            friction = false;
            break;
        case A::DashF:
            if (fr == 0) f.vel.x = f.facing * 15.0f;
            f.vel.x *= 0.9f;
            friction = false;
            break;
        case A::DashB:
            if (fr == 0) f.vel.x = -f.facing * 12.0f;
            f.vel.x *= 0.88f;
            friction = false;
            break;
        case A::JumpN:
        case A::JumpF:
        case A::JumpB:
            if (fr == 3) {
                int d = f.move == A::JumpF ? 1 : (f.move == A::JumpB ? -1 : 0);
                f.vel = {f.facing * d * 5.5f, -17.0f};
                f.grounded = false;
                friction = false;
            }
            break;
        case A::Heavy:
            if (fr == 10) f.vel.x += f.facing * 6.0f;
            break;
        case A::Rising:
            if (fr == 5) f.vel.x += f.facing * 3.0f;
            break;
        case A::Bolt:
            if (fr == 10) w.proj.push_back({{f.pos.x + f.facing * 50.0f, f.pos.y - 100.0f}, f.facing * 9.0f, p, 150});
            break;
        case A::Burst:
            if (fr < 16) f.vel = {0, 0};
            if (fr == 6 && ev) ev->push_back({EventType::Burst, {f.pos.x, f.pos.y - 80}, 0, p});
            break;
        case A::DiveKick:
            if (fr < 4)
                f.vel = {f.vel.x * 0.5f, 0};
            else if (!f.connected)
                f.vel = {f.facing * 9.0f, 13.0f};
            break;
        case A::AirDashF:
            if (fr < 9) f.vel = {f.facing * 10.0f, 0};
            break;
        case A::AirDashB:
            if (fr < 9) f.vel = {-f.facing * 9.0f, 0};
            break;
        case A::Throw:
            if (o.state == FState::Grabbed) {
                if (fr < 14) {
                    o.pos = {f.pos.x + f.facing * 52.0f, f.pos.y};
                    o.vel = {0, 0};
                } else if (fr == 14) {
                    o.state = FState::Free; // release, then hit
                    o.grounded = true;
                    applyHit(w, p, 1 - p, moveData(A::Grab).hit, f.facing, {o.pos.x, o.pos.y - 80}, ev, true);
                }
            }
            break;
        default: break;
        }
    }
    if (friction && f.grounded) f.vel.x *= 0.75f;
}

void integrate(World &w, int p, std::vector<Event> *ev) {
    Fighter &f = w.f[p];
    if (f.state == FState::Grabbed) return;
    if (!f.grounded && !noGravity(f)) f.vel.y += GRAVITY;
    f.pos.x += f.vel.x;
    f.pos.y += f.vel.y;
    if (f.pos.y >= FLOOR_Y) {
        f.pos.y = FLOOR_Y;
        if (!f.grounded) land(f, p, ev);
        f.grounded = true;
        f.vel.y = 0;
    }
    if (f.pos.x < STAGE_LEFT || f.pos.x > STAGE_RIGHT) {
        f.pos.x = std::clamp(f.pos.x, STAGE_LEFT, STAGE_RIGHT);
        bool bounce = f.state == FState::Hitstun && !f.grounded;
        f.vel.x = bounce ? -f.vel.x * 0.35f : 0;
    }
}

void pushApart(World &w) {
    Fighter &a = w.f[0];
    Fighter &b = w.f[1];
    if (a.state == FState::Grabbed || b.state == FState::Grabbed) return;
    const float minD = 46.0f;
    float dx = b.pos.x - a.pos.x;
    if (std::fabs(dx) >= minD || std::fabs(b.pos.y - a.pos.y) >= 100.0f) return;
    int s = dx > 0 ? 1 : (dx < 0 ? -1 : a.facing);
    float push = (minD - std::fabs(dx)) / 2;
    a.pos.x -= s * push;
    b.pos.x += s * push;
    a.pos.x = std::clamp(a.pos.x, STAGE_LEFT, STAGE_RIGHT);
    b.pos.x = std::clamp(b.pos.x, STAGE_LEFT, STAGE_RIGHT);
    if (std::fabs(b.pos.x - a.pos.x) < minD) {
        bool aWall = a.pos.x <= STAGE_LEFT || a.pos.x >= STAGE_RIGHT;
        if (aWall) b.pos.x = a.pos.x + s * minD;
        else a.pos.x = b.pos.x - s * minD;
    }
}

Vector2 overlapCenter(Rectangle a, Rectangle b) {
    float x0 = std::max(a.x, b.x), x1 = std::min(a.x + a.width, b.x + b.width);
    float y0 = std::max(a.y, b.y), y1 = std::min(a.y + a.height, b.y + b.height);
    return {(x0 + x1) / 2, (y0 + y1) / 2};
}

void detectHits(World &w, std::vector<Event> *ev) {
    struct Pending {
        int a;
        bool grab;
        Vector2 at;
    };
    std::vector<Pending> hits;
    for (int p = 0; p < 2; ++p) {
        Fighter &A = w.f[p];
        Fighter &D = w.f[1 - p];
        Rectangle hb;
        if (A.connected || !activeHitbox(A, hb)) continue;
        if (isInvulnerable(D)) continue;
        Rectangle hurt = hurtbox(D);
        if (!CheckCollisionRecs(hb, hurt)) continue;
        bool grab = moveData(A.move).grab;
        if (grab && !(D.grounded && (D.state == FState::Free || D.state == FState::Acting))) continue;
        hits.push_back({p, grab, overlapCenter(hb, hurt)});
    }

    bool anyStrike = false;
    for (auto &h : hits) anyStrike |= !h.grab;

    if (hits.size() == 2 && hits[0].grab && hits[1].grab) {
        // Both grabbed at once: tech, push apart.
        for (int p = 0; p < 2; ++p) {
            Fighter &f = w.f[p];
            f.state = FState::Blockstun;
            f.move = A::Continue;
            f.timer = 16;
            f.vel = {-f.facing * 8.0f, 0};
            f.connected = true;
        }
        w.hitstop = 8;
        if (ev) ev->push_back({EventType::Tech, hits[0].at, 0, -1});
        return;
    }

    for (auto &h : hits) {
        Fighter &A = w.f[h.a];
        Fighter &D = w.f[1 - h.a];
        if (h.grab) {
            if (anyStrike) continue; // strikes beat grabs
            A.connected = true;
            A.move = A::Throw;
            A.frame = 0;
            D.state = FState::Grabbed;
            D.move = A::Continue;
            D.vel = {0, 0};
            w.hitstop = 6;
            if (ev) ev->push_back({EventType::Grab, h.at, 0, 1 - h.a});
            continue;
        }
        A.connected = true;
        int dir = A.facing;
        bool unblockable = false;
        if (A.move == A::Burst) {
            int s = sgn(D.pos.x - A.pos.x);
            dir = s ? s : A.facing;
            unblockable = true;
        }
        applyHit(w, h.a, 1 - h.a, moveData(A.move).hit, dir, h.at, ev, unblockable);
        if (A.move == A::DiveKick) {
            // Bounce off on hit or block.
            A.vel = {-A.facing * 4.0f, -10.0f};
            A.move = A::Drift;
            A.frame = 0;
        }
    }

    // Projectiles
    for (size_t i = 0; i < w.proj.size();) {
        Projectile &pr = w.proj[i];
        int t = 1 - pr.owner;
        Fighter &T = w.f[t];
        Rectangle pb = {pr.pos.x - 14, pr.pos.y - 14, 28, 28};
        if (!isInvulnerable(T) && CheckCollisionRecs(pb, hurtbox(T))) {
            int dir = sgn(pr.vx);
            applyHit(w, pr.owner, t, moveData(A::Bolt).hit, dir, pr.pos, ev, false);
            w.proj.erase(w.proj.begin() + i);
            continue;
        }
        ++i;
    }
}

void moveProjectiles(World &w, std::vector<Event> *ev) {
    for (auto &p : w.proj) {
        p.pos.x += p.vx;
        p.life--;
    }
    // projectile vs projectile
    for (size_t i = 0; i < w.proj.size(); ++i)
        for (size_t j = i + 1; j < w.proj.size(); ++j)
            if (w.proj[i].owner != w.proj[j].owner && std::fabs(w.proj[i].pos.x - w.proj[j].pos.x) < 28 &&
                std::fabs(w.proj[i].pos.y - w.proj[j].pos.y) < 28 && w.proj[i].life > 0 && w.proj[j].life > 0) {
                if (ev) ev->push_back({EventType::Clash, w.proj[i].pos, 0, -1});
                w.proj[i].life = 0;
                w.proj[j].life = 0;
            }
    w.proj.erase(std::remove_if(w.proj.begin(), w.proj.end(),
                                [](const Projectile &p) {
                                    return p.life <= 0 || p.pos.x < STAGE_LEFT - 40 || p.pos.x > STAGE_RIGHT + 40;
                                }),
                 w.proj.end());
}

void advance(World &w) {
    for (int p = 0; p < 2; ++p) {
        Fighter &f = w.f[p];
        Fighter &o = w.f[1 - p];
        switch (f.state) {
        case FState::Acting: {
            f.frame++;
            int total = f.move == A::Land ? f.timer : moveData(f.move).total;
            if (total > 0 && f.frame >= total) becomeFree(f);
            break;
        }
        case FState::Hitstun:
        case FState::Blockstun:
            if (--f.timer <= 0) becomeFree(f);
            break;
        case FState::Knockdown:
            if (--f.timer <= 0) becomeFree(f);
            break;
        case FState::Grabbed:
            if (!(o.state == FState::Acting && o.move == A::Throw)) {
                becomeFree(f);
                f.grounded = f.pos.y >= FLOOR_Y;
            }
            break;
        case FState::Free: break;
        }
    }
}

} // namespace

const MoveData &moveData(Action a) { return MOVES[(int)a]; }

World newWorld() {
    World w;
    w.f[0].pos = {535, FLOOR_Y};
    w.f[0].facing = 1;
    w.f[1].pos = {935, FLOOR_Y};
    w.f[1].facing = -1;
    return w;
}

std::vector<Action> availableActions(const World &w, int p) {
    const Fighter &f = w.f[p];
    std::vector<Action> out;
    if (f.ko || w.f[1 - p].ko) return out;
    if (f.state == FState::Free) {
        if (f.grounded) {
            out = {A::Wait, A::WalkF, A::WalkB, A::DashF, A::DashB, A::JumpN, A::JumpF, A::JumpB,
                   A::Jab,  A::Heavy, A::Sweep, A::Rising, A::Grab, A::Block};
            if (f.meter >= BOLT_COST) out.push_back(A::Bolt);
        } else {
            out = {A::Drift, A::AirSlash, A::DiveKick};
            if (!f.airDashUsed) {
                out.push_back(A::AirDashF);
                out.push_back(A::AirDashB);
            }
        }
        if (f.meter >= BURST_COST) out.push_back(A::Burst);
    } else if ((f.state == FState::Hitstun || f.state == FState::Blockstun) && f.meter >= BURST_COST) {
        out = {A::Continue, A::Burst};
    }
    return out;
}

void beginTurn(World &w, Action a0, Action a1) {
    startMove(w, 0, a0);
    startMove(w, 1, a1);
}

void step(World &w, std::vector<Event> *events) {
    w.frame++;
    if (w.hitstop > 0) {
        w.hitstop--;
        return;
    }
    for (int p = 0; p < 2; ++p) logic(w, p, events);
    for (int p = 0; p < 2; ++p) integrate(w, p, events);
    pushApart(w);
    moveProjectiles(w, events);
    detectHits(w, events);
    advance(w);
}

bool turnOver(const World &w) {
    return w.hitstop == 0 && (w.f[0].state == FState::Free || w.f[1].state == FState::Free);
}

bool anyKo(const World &w) { return w.f[0].ko || w.f[1].ko; }

World simulateTurn(World w, Action a0, Action a1, std::vector<World> *snaps, std::vector<Event> *events,
                   int maxFrames) {
    beginTurn(w, a0, a1);
    if (snaps) snaps->push_back(w);
    for (int i = 0; i < maxFrames; ++i) {
        step(w, events);
        if (snaps) snaps->push_back(w);
        if (turnOver(w) || anyKo(w)) break;
    }
    return w;
}

int framesUntilFree(const Fighter &f) {
    switch (f.state) {
    case FState::Free: return 0;
    case FState::Acting: {
        if (f.move == A::Land) return f.timer - f.frame;
        int t = moveData(f.move).total;
        return t > 0 ? t - f.frame : 20;
    }
    case FState::Hitstun: return f.timer + (f.launched ? 34 : 0);
    case FState::Blockstun:
    case FState::Knockdown: return f.timer;
    case FState::Grabbed: return 40;
    }
    return 0;
}

bool isInvulnerable(const Fighter &f) {
    if (f.state == FState::Knockdown || f.state == FState::Grabbed) return true;
    if (f.state != FState::Acting) return false;
    const MoveData &m = moveData(f.move);
    return m.invulStart >= 0 && f.frame >= m.invulStart && f.frame < m.invulEnd;
}

Rectangle hurtbox(const Fighter &f) {
    if (f.state == FState::Knockdown) return {f.pos.x - 55, f.pos.y - 26, 110, 26};
    if (f.state == FState::Acting && f.move == A::Sweep) return {f.pos.x - 30, f.pos.y - 75, 60, 75};
    if (!f.grounded) return {f.pos.x - 20, f.pos.y - 128, 40, 110};
    return {f.pos.x - 20, f.pos.y - 132, 40, 132};
}

Rectangle worldBox(const Fighter &f, Rectangle b) {
    float x = f.facing > 0 ? f.pos.x + b.x : f.pos.x - b.x - b.width;
    return {x, f.pos.y + b.y, b.width, b.height};
}

bool activeHitbox(const Fighter &f, Rectangle &out) {
    if (f.state != FState::Acting) return false;
    const MoveData &m = moveData(f.move);
    if (m.activeStart < 0 || m.hit.box.width <= 0) return false;
    if (f.frame < m.activeStart || f.frame >= m.activeEnd) return false;
    out = worldBox(f, m.hit.box);
    return true;
}

} // namespace sg
