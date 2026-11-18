#include "game/game.hpp"

#include "engine/assets/assets.hpp"
#include "engine/core/arena.hpp"
#include "engine/renderer/renderer.hpp"

#include <cstdint>

constexpr const char* TEST_SPRITE_NAME = "art/placeholder/test_sprite.png";
constexpr float SPRITE_SIZE = 16.0f;
constexpr float BIG_SCALE = 4.0f;

struct TestRect
{
    Rect rect;
    uint32_t color;
};

constexpr TestRect TEST_RECTS[] = {
    { { 40, 40, 200, 120 }, 0xE04848FF },
    { { 260, 40, 120, 120 }, 0x48C060FF },
    { { 400, 40, 160, 60 }, 0x4878E0FF },
    { { 400, 110, 160, 50 }, 0xF0F0F080 },
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
    renderer_draw_sprite(game->test_sprite, source, { 40, 200, SPRITE_SIZE, SPRITE_SIZE },
                         COLOR_WHITE);
    Rect big = { 80, 200, SPRITE_SIZE * BIG_SCALE, SPRITE_SIZE * BIG_SCALE };
    renderer_draw_sprite(game->test_sprite, source, big, COLOR_WHITE);
}
