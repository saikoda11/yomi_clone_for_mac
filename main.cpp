#include "raylib.h"
#include <vector>
#include "constants.h"
#include "camera.h"

struct Player {
    Vector2 size;
    Vector2 pos;
    Vector2 velocity;
    Vector2 acceleration;
    bool in_air;
    Color color;
};

struct Boundary {
    Rectangle rect;
    Color color;
};

void drawPlayers(const std::vector<Player> &players);
void updatePlayerPos(Player &player, const std::array<Boundary, 4> &boundaries);
void drawBoundaries(const std::array<Boundary, 4> &boundaries);

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT);  
    InitWindow(WIDTH, HEIGHT, "simple physics");
    SetTargetFPS(30);
    
    std::array<Boundary, 4> boundaries {{
        {.rect={20, 20, WIDTH - 40, 10}, .color=BLACK},
        {.rect={20, HEIGHT - 20, WIDTH - 40, 10}, .color=BLACK},
        {.rect={20, 20, 10, HEIGHT - 40}, .color=BLACK},
        {.rect={WIDTH - 30, 20, 10, HEIGHT - 40}, .color=BLACK},
    }};

    Player player = {
        .size = {40, 40},
        .pos = {(float) WIDTH / 2 - 20, boundaries[1].rect.y - 20},
        .velocity = {0, 0},
        .acceleration = {0, -G},
        .in_air = true,
        .color = RED,
    };
    std::vector<Player> players = {player};
    
    GameCam camera(player.pos);

    while (!WindowShouldClose()) {
        double frameStart = GetTime();
        
        if (!IsKeyDown(KEY_W)) {
            players[0].velocity.y = std::max(players[0].velocity.y, (float) 0);
        } else if (!players[0].in_air) {
            players[0].velocity.y = -30;
            players[0].in_air = true;
        }

        for (Player &player : players) {
            updatePlayerPos(player, boundaries);
        }

        camera.update(players[0].pos);

        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginMode2D(camera.getCamera());
                drawBoundaries(boundaries);
                drawPlayers(players);
            EndMode2D();
        EndDrawing();

        double frameTime = GetTime() - frameStart;
        double targetTime = 1.0 / 60.0;
        if (frameTime < targetTime)
            WaitTime(targetTime - frameTime);
    }

    CloseWindow();

    return 0;
}

void drawPlayers(const std::vector<Player> &players) {
    for (int i = 0; i < players.size(); ++i) {
        DrawRectangleV(players[i].pos, players[i].size, players[i].color);
    }
}

void updatePlayerPos(Player &player, const std::array<Boundary, 4> &boundaries) {
    if (IsKeyDown(KEY_D)) {
        player.pos.x += 4;
    } 
    if (IsKeyDown(KEY_A)) {
        player.pos.x -= 4;
    }
    if (player.pos.x < boundaries[2].rect.x + boundaries[2].rect.width) {
        player.pos.x = boundaries[2].rect.x + boundaries[2].rect.width;
    }
    if (player.pos.x + player.size.x > boundaries[3].rect.x) {
        player.pos.x = boundaries[3].rect.x - player.size.x;
    }

    player.velocity.y += player.acceleration.y;
    player.pos.y += player.velocity.y;
    if (player.pos.y < boundaries[0].rect.y + boundaries[0].rect.height) {
        player.pos.y = boundaries[0].rect.y + boundaries[0].rect.height;
    }
    if (player.pos.y + player.size.y > boundaries[1].rect.y) {
        player.pos.y = boundaries[1].rect.y - player.size.y;
        player.in_air = false;
    }
}

void drawBoundaries(const std::array<Boundary, 4> &boundaries) {
    for (int i = 0; i < boundaries.size(); ++i) {
        DrawRectangleRec(boundaries[i].rect, boundaries[i].color);
    }
}

