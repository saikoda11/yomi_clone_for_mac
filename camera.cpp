#include "camera.h"
#include "constants.h"

GameCam::GameCam(Vector2 position) {
    Camera2D camera = {0};
    this->camera = camera;
    this->update(position);
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
}

void GameCam::update(Vector2 position) {
    // update zoom
    camera.zoom += ((float)GetMouseWheelMove()*0.05f);

    if (camera.zoom > 3.0f) camera.zoom = 3.0f;
    else if (camera.zoom < 1.0f) camera.zoom = 1.0f;

    // update position and offset
    this->camera.target = position;
    this->camera.offset = (Vector2){ WIDTH/2.0f, HEIGHT/2.0f };
    float minX = 0, minY = 0, maxX = WIDTH, maxY = HEIGHT;

    Vector2 max = GetWorldToScreen2D((Vector2){ maxX, maxY }, this->camera);
    Vector2 min = GetWorldToScreen2D((Vector2){ minX, minY }, this->camera);

    if (max.x < WIDTH) this->camera.offset.x = WIDTH - (max.x - (float)WIDTH/2);
    if (max.y < HEIGHT) this->camera.offset.y = HEIGHT - (max.y - (float)HEIGHT/2);
    if (min.x > 0) this->camera.offset.x = (float)WIDTH/2 - min.x;
    if (min.y > 0) this->camera.offset.y = (float)HEIGHT/2 - min.y;
}

const Camera2D& GameCam::getCamera() const {
    return this->camera;
}
