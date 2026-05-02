#pragma once

#include "raylib.h"

class GameCam {
private:
    Camera2D camera;
public:
    GameCam(Vector2 position);
    void update(Vector2 position);
    const Camera2D& getCamera() const;
};
