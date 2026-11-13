// Second Guess — a simultaneous-turn stick figure fighting game.
//
// Each turn both players secretly pick an action. The fight then plays out
// frame by frame until at least one fighter can act again. While planning you
// see a ghost preview of how your move plays out against the move you expect
// your opponent to pick.

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "raylib.h"

#include "ai.h"
#include "constants.h"
#include "draw.h"
#include "sim.h"

using namespace sg;

namespace {

constexpr int PANEL_H = 210;
constexpr int PANEL_Y = HEIGHT - PANEL_H;
constexpr int GRID_COLS = 5;
constexpr float BTN_W = 142, BTN_H = 34, BTN_GAP = 6;
constexpr float INFO_X = 24 + GRID_COLS * (BTN_W + BTN_GAP) + 16;

const char *HOTKEY_LABELS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
                               "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
const int HOTKEYS[] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN,
                       KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_Q, KEY_W, KEY_E, KEY_R,
                       KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P};

const Color PANEL_BG = {14, 14, 24, 238};
const Color TEXT = {236, 234, 244, 255};
const Color DIM = {150, 146, 172, 255};
const Color GOOD = {120, 230, 150, 255};
const Color BAD = {255, 120, 110, 255};

enum class Screen { Title, Planning, Playing, RoundOver, MatchOver };

Color categoryColor(Action a) {
    switch (a) {
    case Action::Jab:
    case Action::Heavy:
    case Action::Sweep:
    case Action::Rising:
    case Action::AirSlash:
    case Action::DiveKick: return {255, 120, 90, 255};
    case Action::Grab: return {255, 200, 90, 255};
    case Action::Block:
    case Action::Continue: return {110, 210, 230, 255};
    case Action::Bolt:
    case Action::Burst: return {190, 130, 255, 255};
    default: return {140, 150, 190, 255};
    }
}

Color alpha(Color c, float a) {
    c.a = (unsigned char)std::clamp(a * 255.0f, 0.0f, 255.0f);
    return c;
}

void textCentered(const std::string &s, int cx, int y, int size, Color c) {
    int w = MeasureText(s.c_str(), size);
    DrawText(s.c_str(), cx - w / 2, y, size, c);
}

void textShadow(const std::string &s, int x, int y, int size, Color c) {
    DrawText(s.c_str(), x + 2, y + 2, size, {0, 0, 0, 160});
    DrawText(s.c_str(), x, y, size, c);
}

int drawWrapped(const std::string &text, int x, int y, int size, int maxW, Color c, int maxLines = 3) {
    std::istringstream ss(text);
    std::string word, line;
    int lines = 0;
    auto flush = [&]() {
        if (line.empty() || lines >= maxLines) return;
        DrawText(line.c_str(), x, y + lines * (size + 4), size, c);
        lines++;
        line.clear();
    };
    while (ss >> word) {
        std::string trial = line.empty() ? word : line + " " + word;
        if (MeasureText(trial.c_str(), size) > maxW) {
            flush();
            line = word;
        } else {
            line = trial;
        }
    }
    flush();
    return lines;
}

bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool clicked(Rectangle r) { return hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

void drawButton(Rectangle r, const std::string &label, const char *key, bool selected, Color accent) {
    bool hov = hovered(r);
    Color bg = selected ? Color{(unsigned char)(accent.r / 3), (unsigned char)(accent.g / 3), (unsigned char)(accent.b / 3), 255}
                        : (hov ? Color{44, 42, 62, 255} : Color{30, 29, 44, 255});
    DrawRectangleRounded(r, 0.25f, 6, bg);
    DrawRectangleRounded({r.x, r.y, 5, r.height}, 0.5f, 4, accent);
    if (selected) DrawRectangleRoundedLinesEx(r, 0.25f, 6, 2, accent);
    DrawText(label.c_str(), (int)r.x + 14, (int)(r.y + r.height / 2 - 8), 17, selected ? WHITE : TEXT);
    if (key) {
        int kw = MeasureText(key, 12);
        DrawText(key, (int)(r.x + r.width - kw - 7), (int)r.y + 5, 12, DIM);
    }
}

struct Game {
    Screen screen = Screen::Title;
    bool vsCpu = true;
    World world = newWorld();
    Fx fx;
    Camera2D cam = {{WIDTH / 2.0f, PANEL_Y - 34.0f}, {WIDTH / 2.0f, FLOOR_Y}, 0.0f, 1.0f};

    // planning
    int planner = 0;
    bool needs[2] = {false, false};
    Action chosen[2] = {Action::Continue, Action::Continue};
    Action lastPick[2] = {Action::Wait, Action::Wait};
    std::vector<Action> options, oppOptions;
    int sel = 0, predict = 0;
    bool previewDirty = true;
    std::vector<World> preview;
    size_t previewStop = 0;
    std::vector<Event> previewEvents;

    // playback
    std::string reveal[2];
    int koTimer = -1;
    int slowTick = 0;

    // match
    int turn = 1, round = 1, wins[2] = {0, 0}, roundWinner = -1;
    float displayHp[2] = {MAX_HP, MAX_HP};
    bool showBoxes = false;
    int clock = 0;

    std::string name(int p) const { return p == 0 ? "P1" : (vsCpu ? "CPU" : "P2"); }

    void startMatch(bool cpu) {
        vsCpu = cpu;
        wins[0] = wins[1] = 0;
        round = 1;
        lastPick[0] = lastPick[1] = Action::Wait;
        startRound();
    }

    void startRound() {
        world = newWorld();
        fx.clear();
        turn = 1;
        koTimer = -1;
        displayHp[0] = displayHp[1] = MAX_HP;
        startTurn();
    }

    void startTurn() {
        for (int p = 0; p < 2; ++p) {
            needs[p] = !availableActions(world, p).empty();
            chosen[p] = Action::Continue;
        }
        if (vsCpu && needs[1]) chosen[1] = chooseAiAction(world, 1);
        int first = -1;
        for (int p = 0; p < 2 && first < 0; ++p)
            if (needs[p] && (p == 0 || !vsCpu)) first = p;
        if (first < 0) beginPlay();
        else enterPlanning(first);
    }

    void enterPlanning(int p) {
        planner = p;
        options = availableActions(world, p);
        oppOptions = availableActions(world, 1 - p);
        if (oppOptions.empty()) oppOptions = {Action::Continue};
        sel = 0;
        for (size_t i = 0; i < options.size(); ++i)
            if (options[i] == lastPick[p]) sel = (int)i;
        predict = 0;
        previewDirty = true;
        screen = Screen::Planning;
    }

    void lockIn() {
        chosen[planner] = options[sel];
        if (world.f[planner].state == FState::Free) lastPick[planner] = options[sel];
        if (planner == 0 && needs[1] && !vsCpu) enterPlanning(1);
        else beginPlay();
    }

    void beginPlay() {
        for (int p = 0; p < 2; ++p) {
            const Fighter &f = world.f[p];
            if (chosen[p] != Action::Continue) reveal[p] = moveData(chosen[p]).name;
            else if (f.state == FState::Acting) reveal[p] = std::string("(") + moveData(f.move).name + ")";
            else if (f.state == FState::Free) reveal[p] = "Wait";
            else reveal[p] = needs[p] ? "Continue" : "(stunned)";
        }
        beginTurn(world, chosen[0], chosen[1]);
        screen = Screen::Playing;
    }

    void rebuildPreview() {
        previewDirty = false;
        preview.clear();
        previewEvents.clear();
        Action mine = options[sel], opp = oppOptions[predict];
        Action a0 = planner == 0 ? mine : opp;
        Action a1 = planner == 0 ? opp : mine;
        World end = simulateTurn(world, a0, a1, &preview, &previewEvents);
        previewStop = preview.size() - 1;
        // Keep going (opponent idles) until the planner's own move is finished,
        // so long moves are fully visible.
        World ext = end;
        for (int n = 0; n < 90 && ext.f[planner].state != FState::Free && !anyKo(ext); ++n) {
            if (ext.hitstop == 0) beginTurn(ext, Action::Continue, Action::Continue);
            step(ext);
            preview.push_back(ext);
        }
    }

    // ---------------------------------------------------------------- update

    void update() {
        clock++;
        if (IsKeyPressed(KEY_H)) showBoxes = !showBoxes;
        for (int p = 0; p < 2; ++p) {
            float hp = (float)world.f[p].hp;
            if (displayHp[p] > hp) displayHp[p] = std::max(hp, displayHp[p] - std::max(2.0f, (displayHp[p] - hp) * 0.04f));
            else displayHp[p] = hp;
        }

        switch (screen) {
        case Screen::Title: updateTitle(); break;
        case Screen::Planning: updatePlanning(); break;
        case Screen::Playing: updatePlaying(); break;
        case Screen::RoundOver:
            if (++slowTick % 3 == 0) { // let the KO animation settle in slow motion
                std::vector<Event> events;
                step(world, &events);
                for (const Event &e : events) fx.onEvent(e, world);
            }
            if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (roundWinner >= 0 && wins[roundWinner] >= 2) screen = Screen::MatchOver;
                else {
                    round++;
                    startRound();
                }
            }
            break;
        case Screen::MatchOver:
            if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) screen = Screen::Title;
            break;
        }
        fx.update();

        updateCamera();
    }

    // Follows the fighters and zooms in when they are close. The floor always
    // sits just above the command panel.
    void updateCamera() {
        const Fighter &a = world.f[0], &b = world.f[1];
        float dist = std::fabs(a.pos.x - b.pos.x);
        float zoom = screen == Screen::Title ? 1.0f : std::clamp(1.55f - dist / 900.0f, 1.0f, 1.45f);
        float highest = std::min(a.pos.y, b.pos.y) - 230.0f; // keep heads clear of the HUD
        float room = cam.offset.y - 100.0f;
        zoom = std::min(zoom, room / std::max(1.0f, FLOOR_Y - highest));
        zoom = std::max(zoom, 1.0f);
        cam.zoom += (zoom - cam.zoom) * 0.06f;
        float half = WIDTH / 2.0f / cam.zoom;
        float tx = std::clamp((a.pos.x + b.pos.x) / 2, half, WIDTH - half);
        cam.target.x += (tx - cam.target.x) * 0.12f;
        cam.target.x = std::clamp(cam.target.x, half, WIDTH - half);
        cam.target.y = FLOOR_Y;
    }

    Rectangle titleButton(int i) const { return {WIDTH / 2.0f - 200, 430.0f + i * 70, 400, 54}; }

    void updateTitle() {
        if (IsKeyPressed(KEY_ONE) || clicked(titleButton(0))) startMatch(true);
        else if (IsKeyPressed(KEY_TWO) || clicked(titleButton(1))) startMatch(false);
    }

    Rectangle optionRect(int i) const {
        int c = i % GRID_COLS, r = i / GRID_COLS;
        return {24 + c * (BTN_W + BTN_GAP), PANEL_Y + 42 + r * (BTN_H + BTN_GAP), BTN_W, BTN_H};
    }
    Rectangle lockRect() const { return {WIDTH - 214.0f, PANEL_Y + 12.0f, 190, 46}; }
    Rectangle predictPrevRect() const { return {INFO_X + 170, PANEL_Y + 118.0f, 26, 24}; }
    Rectangle predictNextRect() const { return {INFO_X + 380, PANEL_Y + 118.0f, 26, 24}; }

    void updatePlanning() {
        int n = (int)options.size();
        int old = sel, oldPredict = predict;
        for (int i = 0; i < n && i < 20; ++i)
            if (IsKeyPressed(HOTKEYS[i])) sel = i;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) sel = (sel + 1) % n;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) sel = (sel + n - 1) % n;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) sel = std::min(n - 1, sel + GRID_COLS);
        if (IsKeyPressed(KEY_UP)) sel = std::max(0, sel - GRID_COLS);

        int m = (int)oppOptions.size();
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (IsKeyPressed(KEY_TAB)) predict = (predict + (shift ? m - 1 : 1)) % m;
        if (clicked(predictPrevRect())) predict = (predict + m - 1) % m;
        if (clicked(predictNextRect())) predict = (predict + 1) % m;

        bool lock = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || clicked(lockRect());
        for (int i = 0; i < n; ++i)
            if (clicked(optionRect(i))) {
                if (sel == i) lock = true; // click again to lock in
                sel = i;
            }

        if (sel != old || predict != oldPredict) previewDirty = true;
        if (lock) {
            lockIn();
            return;
        }
        if (previewDirty) rebuildPreview();
    }

    void updatePlaying() {
        int steps = IsKeyDown(KEY_SPACE) ? 4 : 1;
        if (koTimer >= 0) {
            // slow motion after a KO
            steps = (++slowTick % 3 == 0) ? 1 : 0;
            koTimer--;
            if (koTimer == 0 && screen == Screen::Playing) finishRound();
        }
        for (int s = 0; s < steps; ++s) {
            std::vector<Event> events;
            step(world, &events);
            for (const Event &e : events) fx.onEvent(e, world);
            if (koTimer < 0 && anyKo(world)) {
                koTimer = 150;
                break;
            }
            if (koTimer < 0 && turnOver(world) && screen == Screen::Playing) {
                turn++;
                startTurn();
                break;
            }
        }
    }

    void finishRound() {
        bool k0 = world.f[0].ko, k1 = world.f[1].ko;
        roundWinner = (k0 && k1) ? -1 : (k0 ? 1 : 0);
        if (roundWinner >= 0) wins[roundWinner]++;
        screen = Screen::RoundOver;
    }

    // ------------------------------------------------------------------ draw

    void draw() {
        BeginDrawing();
        ClearBackground({18, 18, 28, 255});

        Camera2D c = cam;
        if (fx.shake > 0) {
            c.offset.x += GetRandomValue(-100, 100) / 100.0f * fx.shake;
            c.offset.y += GetRandomValue(-100, 100) / 100.0f * fx.shake;
        }
        BeginMode2D(c);
        drawStage(WIDTH, HEIGHT);
        if (screen == Screen::Planning) drawPreview();
        for (int p = 0; p < 2; ++p) drawFighter(world.f[p], p, 1.0f, {0, 0, 0, 0}, clock);
        drawProjectiles(world);
        fx.drawWorld();
        if (showBoxes) drawBoxes(world);
        if (screen != Screen::Title) drawComboCounters();
        EndMode2D();

        if (screen == Screen::Title) drawTitle();
        else {
            drawHud();
            drawPanel();
            if (screen == Screen::RoundOver) drawRoundOver();
            if (screen == Screen::MatchOver) drawMatchOver();
            if (koTimer > 60 && screen == Screen::Playing) {
                float k = (koTimer - 60) / 90.0f;
                textCentered("K.O.", WIDTH / 2 + 4, 254, 120, alpha(BLACK, 0.5f * k + 0.2f));
                textCentered("K.O.", WIDTH / 2, 250, 120, alpha({255, 230, 120, 255}, std::min(1.0f, k * 2)));
            }
        }
        EndDrawing();
    }

    void drawPreview() {
        if (preview.empty()) return;
        int opp = 1 - planner;
        Color pc = PLAYER_COLORS[planner];

        // planner trajectory
        for (size_t i = 1; i < preview.size(); ++i) {
            Vector2 a = preview[i - 1].f[planner].pos, b = preview[i].f[planner].pos;
            a.y -= 66;
            b.y -= 66;
            DrawLineEx(a, b, 2, alpha(pc, i <= previewStop ? 0.5f : 0.2f));
        }
        // hitboxes along the way
        for (size_t i = 0; i < preview.size(); ++i) {
            Rectangle hb;
            if (activeHitbox(preview[i].f[planner], hb)) DrawRectangleLinesEx(hb, 1.5f, {255, 80, 80, 70});
        }
        // ghost key frames
        for (size_t i = 6; i < preview.size(); i += 6) {
            bool afterStop = i > previewStop;
            drawFighter(preview[i].f[planner], planner, afterStop ? 0.08f : 0.16f, {0, 0, 0, 0}, clock);
        }
        const World &stop = preview[previewStop];
        drawFighter(stop.f[opp], opp, 0.30f, {0, 0, 0, 0}, clock);
        drawProjectiles(stop, 0.4f);
        drawFighter(stop.f[planner], planner, 0.55f, {0, 0, 0, 0}, clock);
        if (preview.size() - 1 > previewStop)
            drawFighter(preview.back().f[planner], planner, 0.22f, {0, 0, 0, 0}, clock);

        // "turn ends here" marker
        Vector2 m = stop.f[planner].pos;
        textCentered("turn ends", (int)m.x, (int)m.y - 175, 14, alpha(TEXT, 0.7f));
    }

    void drawComboCounters() {
        for (int p = 0; p < 2; ++p) {
            const Fighter &f = world.f[p];
            if (f.combo >= 2 && f.state != FState::Free) {
                std::string s = std::to_string(f.combo) + " HITS";
                textCentered(s, (int)f.pos.x, (int)f.pos.y - 210, 28, PLAYER_COLORS[1 - p]);
            }
        }
    }

    void drawHud() {
        const float barW = 560, barH = 26, top = 22;
        for (int p = 0; p < 2; ++p) {
            const Fighter &f = world.f[p];
            float x = p == 0 ? 40 : WIDTH - 40 - barW;
            Rectangle back = {x, top, barW, barH};
            DrawRectangleRounded({x - 3, top - 3, barW + 6, barH + 6}, 0.3f, 6, {0, 0, 0, 160});
            DrawRectangleRec(back, {40, 36, 52, 255});
            float k = f.hp / (float)MAX_HP, kd = displayHp[p] / MAX_HP;
            // bars drain toward the center of the screen
            auto part = [&](float frac) {
                float w = barW * frac;
                return p == 0 ? Rectangle{x + barW - w, top, w, barH} : Rectangle{x, top, w, barH};
            };
            DrawRectangleRec(part(kd), {255, 240, 200, 255});
            Color hc = k > 0.3f ? PLAYER_COLORS[p] : Color{255, 70, 70, 255};
            DrawRectangleRec(part(k), hc);
            std::string hp = std::to_string(f.hp);
            int tx = p == 0 ? (int)(x + barW - MeasureText(hp.c_str(), 20) - 8) : (int)x + 8;
            textShadow(hp, tx, (int)top + 4, 20, WHITE);

            // name + meter
            int ny = (int)(top + barH + 8);
            std::string nm = name(p);
            int nameX = p == 0 ? (int)x : (int)(x + barW - MeasureText(nm.c_str(), 24));
            textShadow(nm, nameX, ny, 24, PLAYER_COLORS[p]);

            float mw = 260, mx = p == 0 ? x + 60 : x + barW - 60 - mw, my = ny + 6;
            DrawRectangleRec({mx, my, mw, 12}, {40, 36, 52, 255});
            float mk = f.meter / (float)MAX_METER;
            Rectangle mr = p == 0 ? Rectangle{mx, my, mw * mk, 12} : Rectangle{mx + mw * (1 - mk), my, mw * mk, 12};
            DrawRectangleRec(mr, f.meter >= BURST_COST ? Color{200, 140, 255, 255} : Color{140, 110, 210, 255});
            for (int tick : {BOLT_COST, BURST_COST}) {
                float t = tick / (float)MAX_METER;
                float tx2 = p == 0 ? mx + mw * t : mx + mw * (1 - t);
                DrawLineEx({tx2, my - 2}, {tx2, my + 14}, 2, {255, 255, 255, 120});
            }
            std::string ms = "METER " + std::to_string(f.meter);
            int msx = p == 0 ? (int)(mx + mw + 10) : (int)(mx - 10 - MeasureText(ms.c_str(), 14));
            DrawText(ms.c_str(), msx, (int)my - 1, 14, DIM);

            // round pips
            for (int i = 0; i < 2; ++i) {
                float px = p == 0 ? x + barW - 14 - i * 22 : x + 14 + i * 22;
                Vector2 c = {px, top + barH + 20};
                DrawCircleV(c, 8, {0, 0, 0, 150});
                DrawCircleV(c, 6, i < wins[p] ? Color{255, 220, 110, 255} : Color{60, 56, 76, 255});
            }
        }
        std::string r = "ROUND " + std::to_string(round);
        std::string t = "TURN " + std::to_string(turn);
        textCentered(r, WIDTH / 2, 20, 26, TEXT);
        textCentered(t, WIDTH / 2, 50, 18, DIM);
        if (showBoxes) DrawText("hitboxes on (H)", WIDTH / 2 - 60, 76, 14, DIM);
    }

    void drawPanel() {
        DrawRectangle(0, PANEL_Y, WIDTH, PANEL_H, PANEL_BG);
        if (screen == Screen::Planning) drawPlanningPanel();
        else drawPlaybackPanel();
    }

    void drawPlaybackPanel() {
        DrawRectangle(0, PANEL_Y, WIDTH, 3, {90, 84, 120, 255});
        int cy = PANEL_Y + 50;
        std::string l = name(0) + ":  " + reveal[0];
        std::string r = name(1) + ":  " + reveal[1];
        int lw = MeasureText(l.c_str(), 34);
        DrawText(l.c_str(), WIDTH / 2 - 60 - lw, cy, 34, PLAYER_COLORS[0]);
        textCentered("vs", WIDTH / 2, cy + 6, 26, DIM);
        DrawText(r.c_str(), WIDTH / 2 + 60, cy, 34, PLAYER_COLORS[1]);
        if (screen == Screen::Playing)
            textCentered("Playing out the turn...   hold SPACE to fast-forward   ·   H toggles hitboxes", WIDTH / 2,
                         PANEL_Y + 130, 18, DIM);
    }

    void drawPlanningPanel() {
        Color pc = PLAYER_COLORS[planner];
        DrawRectangle(0, PANEL_Y, WIDTH, 3, pc);
        const Fighter &me = world.f[planner];
        std::string header = name(planner) + " - choose your move";
        if (me.state == FState::Hitstun || me.state == FState::Blockstun) header = name(planner) + " - you are being hit! Burst?";
        else if (!me.grounded) header = name(planner) + " - airborne";
        DrawText(header.c_str(), 24, PANEL_Y + 12, 22, pc);
        if (!vsCpu && planner == 0 && needs[1])
            DrawText("(P2: look away!)", 24 + MeasureText(header.c_str(), 22) + 16, PANEL_Y + 16, 16, DIM);

        for (int i = 0; i < (int)options.size(); ++i)
            drawButton(optionRect(i), moveData(options[i]).name, i < 20 ? HOTKEY_LABELS[i] : nullptr, i == sel,
                       categoryColor(options[i]));

        // info column
        DrawLineEx({INFO_X - 12, PANEL_Y + 14.0f}, {INFO_X - 12, PANEL_Y + PANEL_H - 14.0f}, 1, {60, 56, 80, 255});
        Action a = options[sel];
        const MoveData &m = moveData(a);
        int x = (int)INFO_X, y = PANEL_Y + 12;
        DrawText(m.name, x, y, 28, WHITE);
        if (m.cost > 0) {
            std::string cost = std::to_string(m.cost) + " meter";
            DrawText(cost.c_str(), x + MeasureText(m.name, 28) + 12, y + 8, 16, {200, 140, 255, 255});
        }
        drawWrapped(m.desc, x, y + 34, 16, (int)(lockRect().x - x - 16), DIM, 2);

        // frame data + strip
        int fy = PANEL_Y + 88;
        if (a != Action::Continue) {
            std::string fd;
            int total = m.total;
            if (m.activeStart >= 0) {
                fd = "startup " + std::to_string(m.activeStart) + "f";
                if (m.hit.damage) fd += "   dmg " + std::to_string(m.hit.damage);
            } else {
                fd = total > 0 ? std::to_string(total) + " frames" : "until landing";
            }
            DrawText(fd.c_str(), x, fy, 16, TEXT);
            int sx = x + 190;
            int shown = total > 0 ? total : 24;
            float cw = std::min(7.0f, 440.0f / shown);
            for (int f = 0; f < shown; ++f) {
                Color c = {110, 110, 140, 255};
                if (m.activeStart >= 0 && f >= m.activeStart && f < m.activeEnd) c = {255, 90, 80, 255};
                else if (m.activeStart >= 0 && f >= m.activeEnd) c = {80, 130, 220, 255};
                else if (a == Action::Bolt && f == 10) c = {255, 90, 80, 255};
                DrawRectangleRec({sx + f * cw, (float)fy + 2, cw - 1, 12}, c);
                if (m.invulStart >= 0 && f >= m.invulStart && f < m.invulEnd)
                    DrawRectangleRec({sx + f * cw, (float)fy + 15, cw - 1, 3}, WHITE);
            }
        }

        // prediction selector
        int py = PANEL_Y + 120;
        DrawText("if opponent picks", x, py + 3, 16, DIM);
        drawButton(predictPrevRect(), "<", nullptr, false, {120, 120, 160, 255});
        drawButton(predictNextRect(), ">", nullptr, false, {120, 120, 160, 255});
        std::string pn = moveData(oppOptions[predict]).name;
        textCentered(pn, (int)(predictPrevRect().x + predictNextRect().x + 26) / 2, py + 2, 20, PLAYER_COLORS[1 - planner]);
        DrawText("TAB", (int)predictNextRect().x + 36, py + 5, 14, DIM);

        drawOutcome(x, PANEL_Y + 152);

        Rectangle lr = lockRect();
        bool hov = hovered(lr);
        DrawRectangleRounded(lr, 0.3f, 6, hov ? pc : Color{(unsigned char)(pc.r * 0.7f), (unsigned char)(pc.g * 0.7f), (unsigned char)(pc.b * 0.7f), 255});
        textCentered("LOCK IN", (int)(lr.x + lr.width / 2), (int)lr.y + 8, 24, WHITE);
        textCentered("ENTER", (int)(lr.x + lr.width / 2), (int)lr.y + 32, 11, {255, 255, 255, 190});
    }

    void drawOutcome(int x, int y) {
        if (preview.empty()) return;
        const World &end = preview[previewStop];
        int me = planner, op = 1 - planner;
        int dealt = world.f[op].hp - end.f[op].hp;
        int taken = world.f[me].hp - end.f[me].hp;
        int adv = framesUntilFree(end.f[op]) - framesUntilFree(end.f[me]);

        std::string what = "Preview: ";
        bool grabbed = false, blocked = false, teched = false, clashed = false;
        for (const Event &e : previewEvents) {
            if (e.type == EventType::Grab) grabbed = true;
            if (e.type == EventType::Block) blocked = true;
            if (e.type == EventType::Tech) teched = true;
            if (e.type == EventType::Clash) clashed = true;
        }
        Color c = DIM;
        if (end.f[op].ko) {
            what += "KNOCKOUT!";
            c = GOOD;
        } else if (dealt > 0 && taken > 0) {
            what += "trade  (+" + std::to_string(dealt) + " / -" + std::to_string(taken) + ")";
            c = TEXT;
        } else if (dealt > 0) {
            what += (grabbed ? "your grab lands  " : "you hit  ") + std::string("+") + std::to_string(dealt);
            c = GOOD;
        } else if (taken > 0) {
            what += "you get hit  -" + std::to_string(taken);
            c = BAD;
        } else if (grabbed) {
            what += "grab connects";
            c = GOOD;
        } else if (teched) {
            what += "grabs tech each other";
        } else if (blocked) {
            what += "attack is blocked";
        } else if (clashed) {
            what += "bolts clash";
        } else {
            what += "nothing connects";
        }
        DrawText(what.c_str(), x, y, 18, c);

        std::string fa;
        Color fc = DIM;
        if (end.f[me].ko || end.f[op].ko) fa = "";
        else if (adv > 0) {
            fa = "you act first  (+" + std::to_string(adv) + "f)";
            fc = GOOD;
        } else if (adv < 0) {
            fa = "they act first  (" + std::to_string(adv) + "f)";
            fc = BAD;
        } else fa = "both act together";
        std::string len = "turn: " + std::to_string((int)previewStop) + "f   ·   " + fa;
        DrawText(len.c_str(), x, y + 24, 16, fc);
    }

    void drawTitle() {
        DrawRectangle(0, 0, WIDTH, HEIGHT, {10, 10, 18, 150});
        textCentered("SECOND GUESS", WIDTH / 2 + 4, 154, 96, {0, 0, 0, 180});
        textCentered("SECOND GUESS", WIDTH / 2, 150, 96, WHITE);
        textCentered("a turn-based fighting game about reading your opponent", WIDTH / 2, 262, 22, DIM);
        textCentered("Both players pick a move in secret. The fight plays out frame by frame until someone can act again.",
                     WIDTH / 2, 330, 18, TEXT);
        textCentered("Preview your move as a ghost against the move you think they'll pick.", WIDTH / 2, 356, 18,
                     TEXT);
        const char *labels[] = {"[1]  Versus CPU", "[2]  Two players (hotseat)"};
        for (int i = 0; i < 2; ++i) {
            Rectangle r = titleButton(i);
            bool h = hovered(r);
            DrawRectangleRounded(r, 0.3f, 6, h ? PLAYER_COLORS[i] : Color{36, 34, 52, 255});
            DrawRectangleRoundedLinesEx(r, 0.3f, 6, 2, PLAYER_COLORS[i]);
            textCentered(labels[i], (int)(r.x + r.width / 2), (int)r.y + 15, 26, WHITE);
        }
        const char *help[] = {
            "Pick: click a move or press its key (1-0, Q-P), arrows to browse",
            "TAB: change what you expect the opponent to do (updates the ghost preview)",
            "ENTER / click again: lock in       SPACE (hold): fast-forward       H: hitboxes",
            "Strikes beat Grab  ·  Grab beats Block  ·  Block beats strikes  ·  Jump beats Sweep  ·  Rising beats jumps",
        };
        for (int i = 0; i < 4; ++i) textCentered(help[i], WIDTH / 2, 610 + i * 28, 17, i == 3 ? Color{255, 220, 140, 255} : DIM);
    }

    void drawRoundOver() {
        DrawRectangle(0, 180, WIDTH, 170, {0, 0, 0, 150});
        std::string s = roundWinner < 0 ? "DOUBLE K.O." : name(roundWinner) + " TAKES ROUND " + std::to_string(round);
        Color c = roundWinner < 0 ? TEXT : PLAYER_COLORS[roundWinner];
        textCentered(s, WIDTH / 2, 215, 60, c);
        textCentered("press ENTER", WIDTH / 2, 295, 20, DIM);
    }

    void drawMatchOver() {
        DrawRectangle(0, 0, WIDTH, HEIGHT, {0, 0, 0, 170});
        std::string s = name(roundWinner) + " WINS";
        textCentered(s, WIDTH / 2, 260, 100, PLAYER_COLORS[roundWinner]);
        std::string score = std::to_string(wins[0]) + " - " + std::to_string(wins[1]);
        textCentered(score, WIDTH / 2, 380, 40, TEXT);
        textCentered("press ENTER for the title screen", WIDTH / 2, 450, 20, DIM);
    }
};

} // namespace

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(WIDTH, HEIGHT, "Second Guess");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Game game;
    while (!WindowShouldClose()) {
        game.update();
        game.draw();
    }
    CloseWindow();
    return 0;
}
