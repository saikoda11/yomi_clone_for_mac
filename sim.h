#pragma once
// Deterministic fight simulation for "Second Guess".
// Both players pick an action at the same time, then the world runs frame by
// frame until at least one fighter can act again (design doc, step 4).

#include "raylib.h"
#include <vector>

namespace sg {

constexpr float FLOOR_Y     = 640.0f;
constexpr float STAGE_LEFT  = 60.0f;
constexpr float STAGE_RIGHT = 1410.0f;
constexpr float GRAVITY     = 0.9f;
constexpr int   MAX_HP      = 1000;
constexpr int   MAX_METER   = 100;
constexpr int   BOLT_COST   = 25;
constexpr int   BURST_COST  = 60;

enum class Action {
    Continue,
    // ground
    Wait, WalkF, WalkB, DashF, DashB, JumpN, JumpF, JumpB,
    Jab, Heavy, Sweep, Rising, Grab, Block, Bolt, Burst,
    // air
    Drift, AirSlash, DiveKick, AirDashF, AirDashB,
    // internal (never offered to players)
    Land, Throw,
    COUNT
};

struct HitData {
    int damage = 0;
    int hitstun = 0;
    Vector2 kb = {0, 0};      // knockback, x is relative to attacker facing
    bool knockdown = false;    // forces a knockdown on landing
    Rectangle box = {0, 0, 0, 0}; // relative to feet, facing right, y up is negative
};

struct MoveData {
    const char *name;
    const char *desc;
    bool air;                  // used in the air
    int total;                 // total frames (-1: until landing)
    int activeStart, activeEnd;// [start, end) hitbox frames, -1 if none
    int invulStart, invulEnd;  // [start, end) invulnerable frames, -1 if none
    int cost;                  // meter cost
    bool grab;
    HitData hit;
};

const MoveData &moveData(Action a);

enum class FState { Free, Acting, Hitstun, Blockstun, Knockdown, Grabbed };

struct Fighter {
    Vector2 pos = {0, 0};      // feet position
    Vector2 vel = {0, 0};
    int facing = 1;            // +1 right, -1 left
    int hp = MAX_HP;
    int meter = 30;
    FState state = FState::Free;
    Action move = Action::Continue;
    int frame = 0;             // frame within current move
    int timer = 0;             // stun / knockdown / landing timer
    bool grounded = true;
    bool connected = false;    // current move already hit something
    bool launched = false;     // knocked into the air -> knockdown on landing
    bool airDashUsed = false;
    bool ko = false;
    int combo = 0;             // hits taken in the current combo
};

struct Projectile {
    Vector2 pos;
    float vx;
    int owner;
    int life;
};

enum class EventType { Hit, Block, Grab, Tech, Clash, Burst, Land, Ko, Whiff };

struct Event {
    EventType type;
    Vector2 pos;
    int damage;
    int target;
};

struct World {
    Fighter f[2];
    std::vector<Projectile> proj;
    int hitstop = 0;
    int frame = 0;
};

World newWorld();

// Actions player p may choose right now. Empty => no decision (auto-continue).
std::vector<Action> availableActions(const World &w, int p);

// Apply chosen actions at the start of a turn (Continue = keep doing what you do).
void beginTurn(World &w, Action a0, Action a1);

// Advance one frame.
void step(World &w, std::vector<Event> *events = nullptr);

// True when someone can act again (end of the turn).
bool turnOver(const World &w);
bool anyKo(const World &w);

// Runs a whole turn on a copy. Snapshots of every frame are optionally stored.
World simulateTurn(World w, Action a0, Action a1, std::vector<World> *snaps = nullptr,
                   std::vector<Event> *events = nullptr, int maxFrames = 400);

// Frames until fighter p can act (0 if free now).
int framesUntilFree(const Fighter &f);

bool isInvulnerable(const Fighter &f);
Rectangle hurtbox(const Fighter &f);
bool activeHitbox(const Fighter &f, Rectangle &out);
Rectangle worldBox(const Fighter &f, Rectangle local);

} // namespace sg
