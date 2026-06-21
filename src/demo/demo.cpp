#include "demo.h"
#include <cmath>

namespace demo {

std::vector<entity::PlayerEntity> GenerateDemoPlayers(
    int screenWidth, int screenHeight, float timeSeconds
) {
    std::vector<entity::PlayerEntity> players;

    struct Spawn {
        float x;
        float y;
        const char* name;
        float health;
        float shield;
        float distance;
    };

    const Spawn spawns[] = {
        { 0.22f, 0.38f, "Demo_Alpha",  82.0f, 50.0f, 47.0f },
        { 0.50f, 0.42f, "Demo_Bravo",  55.0f,  0.0f, 92.0f },
        { 0.78f, 0.33f, "Demo_Charlie", 100.0f, 75.0f, 31.0f },
    };

    for (size_t i = 0; i < sizeof(spawns) / sizeof(spawns[0]); ++i) {
        const Spawn& s = spawns[i];
        const float phase = timeSeconds * 1.2f + static_cast<float>(i);

        entity::PlayerEntity player;
        player.name = s.name;
        player.isOnScreen = true;
        player.isDead = false;
        player.teamIndex = 1;
        player.health = s.health;
        player.maxHealth = 100.0f;
        player.shield = s.shield;
        player.maxShield = 100.0f;
        player.distance = s.distance + std::sin(phase) * 4.0f;

        const float cx = s.x * static_cast<float>(screenWidth) + std::sin(phase) * 35.0f;
        const float cy = s.y * static_cast<float>(screenHeight) + std::cos(phase * 0.7f) * 20.0f;
        player.screenPos = { cx, cy };

        const float halfW = 28.0f;
        const float top = cy - 90.0f;
        const float bottom = cy + 10.0f;
        player.box.top = top;
        player.box.bottom = bottom;
        player.box.left = cx - halfW;
        player.box.right = cx + halfW;
        player.box.width = halfW * 2.0f;
        player.box.height = bottom - top;

        players.push_back(std::move(player));
    }

    return players;
}

} // namespace demo
