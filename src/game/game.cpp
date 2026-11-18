#include "game/game.hpp"

#include "engine/assets/assets.hpp"
#include "engine/core/arena.hpp"
#include "engine/renderer/renderer.hpp"

#include <cstdint>

constexpr const char* TEST_SPRITE_NAME = "art/placeholder/test_sprite.png";
constexpr float SPRITE_SIZE = 16.0f;
constexpr float BIG_SCALE = 2.0f;

struct TestRect
{
    Rect rect;
    uint32_t color;
};

constexpr TestRect TEST_RECTS[] = {
    { { 10, 10, 50, 30 }, 0xE04848FF },
    { { 65, 10, 30, 30 }, 0x48C060FF },
    { { 100, 10, 40, 15 }, 0x4878E0FF },
    { { 100, 27, 40, 13 }, 0xF0F0F080 },
    // Marks the game screen's corners, to check nothing is cropped.
    { { 0, 0, 2, 2 }, 0xFFFFFFFF },
    { { 318, 178, 2, 2 }, 0xFFFFFFFF },
};

struct GameState
{
    int test_sprite;
};

static GameState* game = nullptr;

bool game_init(Arena& permanent, Arena& level)
{
    (void)level;
    game = ARENA_PUSH_STRUCT(permanent, GameState);
    if (!game)
    {
        return false;
    }

    game->test_sprite = assets_load_texture(TEST_SPRITE_NAME);
    return game->test_sprite >= 0;
}

void game_update(float dt)
{
    (void)dt;
}

void game_render(Arena& frame)
{
    (void)frame;
    for (const TestRect& test : TEST_RECTS)
    {
        renderer_draw_rect(test.rect, test.color);
    }

    Rect source = { 0, 0, SPRITE_SIZE, SPRITE_SIZE };
    renderer_draw_sprite(game->test_sprite, source, { 10, 50, SPRITE_SIZE, SPRITE_SIZE },
                         COLOR_WHITE);
    Rect big = { 30, 50, SPRITE_SIZE * BIG_SCALE, SPRITE_SIZE * BIG_SCALE };
    renderer_draw_sprite(game->test_sprite, source, big, COLOR_WHITE);
}
