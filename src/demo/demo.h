#pragma once

#include "../entity/entity.h"
#include <vector>

namespace demo {

/// Synthetic players for previewing ESP without attaching to a game process.
std::vector<entity::PlayerEntity> GenerateDemoPlayers(int screenWidth, int screenHeight, float timeSeconds);

} // namespace demo
