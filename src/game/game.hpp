#pragma once

#include "engine/core/arena.hpp"

// The game, called by the engine's main loop. For now a test scene that exercises the engine.

// Loads what the game needs. Returns false if it can't start.
bool game_init(Arena& permanent, Arena& level);

// One fixed 1/60 s step.
void game_update(float dt);

// Draws the current state. Temporary memory comes from the frame arena.
void game_render(Arena& frame);
