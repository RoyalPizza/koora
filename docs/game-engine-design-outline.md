# Game Engine Design Outline

> Exported from the Claude Doc [Game Engine Design Outline](https://claude.ai/artifact/XZZ925AujJjrUWgxDKfmQF) on 2026-11-18 (rev 30).

Nov 9, 2026 · @Royal

**Maybe: vision (not confirmed).** A small, 2D-only alternative to Godot that promises to stay 2D and stay simple. Godot remains the main tool; this is a backup in case Godot grows too heavy, and a learning project.

- **Generic core below the line:** platform, renderer and shaders, input, audio, memory, logging, config, text, asset loading.
- **Genre layers above the line**, added one game type at a time: physics style, entity kinds, map format and game rules. Examples: a tilemap painter for tile games, platformer physics for platformers.
- **Text files stay the source of truth.** A basic editor sits on top as a convenience for viewing and hand tweaks.
- **Compiled C++ over a scripting language**, since AI writes the code and a fast one-command build is enough.
- **Don't design for genres we haven't built.** Each genre layer comes from making a real game in it.

## Overview and principles

A small 2D game engine in C-style C++, talking to each OS through its native APIs, with third-party code kept to a few single-file libraries behind our own interfaces.

- **C-style C++:** free functions and plain structs. No classes with behavior, no virtual functions, no templates beyond trivial use.
- **Native only:** no GLFW or SDL. Each OS gets its own platform and renderer files.
- **Backends chosen at link time:** the build script picks which files to compile. No `#ifdef` forests, no runtime dispatch.
- **Third-party code is isolated:** it lives in `third_party/` and only wrapper files include it.
- **2D only.**

**AI first, simplest workflow.** Every decision is judged by one question: can AI easily work with this to help build the game? When in doubt, pick the simpler option.

- **Everything authored is text:** code, TOML data, maps and shaders. Binary only for images, audio and release packing.
- **One command builds** (`build/build.bat` or `build/build.sh`). There's no IDE-only step.
- **Plain, predictable APIs:** free functions with obvious names, one header per system.
- **Readable logs** with file and line numbers, so AI can diagnose from a pasted log.
- **Errors never crash silently:** missing data falls back to a default and logs a warning.
- **This doc is the source of truth** for design decisions, so AI can read it before helping.

**Controller first.** Games built on this engine are designed for gamepads first, with keyboard and mouse as the secondary input. See Input for what this means in the engine.

## Folder layout

Each engine system sits in its own folder behind a header. Everything external lives in `third_party/`.

```
docs/                         design docs
build/                        build.bat, build.sh
obj/                          intermediate files (.obj, .o, .pdb)  — not in git
bin/                          final binaries (game.exe / game)     — not in git
src/
  engine/
    core/
      main.cpp
      arena.h, arena.cpp
      log.h, log.cpp
    platform/
      platform.h
      platform_win32.cpp
      platform_win32_hid.cpp  ⚠ only if needed
      platform_x11.cpp
    renderer/
      renderer.h
      render_d3d11.cpp
      render_gl.cpp
      shaders/
        sprite.hlsl    sprite.glsl
        flash.hlsl     flash.glsl
        palette.hlsl   palette.glsl
        post.hlsl      post.glsl
    input/
      input.h, input.cpp      action mapping
    physics/
      physics.h, physics.cpp
    entity/
      entity.h, entity.cpp
    audio/
      audio.h, audio_miniaudio.cpp
    text/
      font.h, font.cpp
    assets/
      assets.h, assets.cpp    loading + blob packing
    config/
      config.h, config_toml.cpp
  game/
    game.cpp
    data/                     .toml (entities, bindings)
    art/                      .png, .ttf
    audio/                    .wav
    maps/                     .map.toml, .tileset.toml
  third_party/
    stb/                      stb_image.h, stb_truetype.h
    miniaudio/                miniaudio.h
    tomlc99/                  toml.c, toml.h
    gamecontrollerdb.txt      ⚠ only if HID backend
```

Rules for `third_party/`:

- Only wrapper files include third-party headers (`assets.cpp`, `audio_miniaudio.cpp`, `config_toml.cpp`).
- Each library keeps its license file beside its code.
- Each `*_IMPLEMENTATION` define appears in exactly one `.cpp`, usually the wrapper.
- Third-party files may compile with lowered warnings.

## Build

Windows builds with a `.bat` file calling MSVC, and Linux with a `.sh` file calling g++. Each script lists exactly the files for its platform.

**build.bat** (run from an x64 Native Tools Command Prompt)

```bat
@echo off
pushd %~dp0..
if not exist obj mkdir obj
if not exist bin mkdir bin

cl /nologo /Zi /Od /W4 /EHsc /std:c++17 /Fo:obj\ /Fd:obj\ ^
   src\engine\core\main.cpp ^
   src\engine\platform\platform_win32.cpp ^
   src\engine\renderer\render_d3d11.cpp ^
   src\game\game.cpp ^
   /Fe:bin\game.exe ^
   /link user32.lib d3d11.lib dxgi.lib d3dcompiler.lib xinput.lib

popd
```

**build.sh**

```sh
#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p obj bin

SRCS="src/engine/core/main.cpp
      src/engine/platform/platform_x11.cpp
      src/engine/renderer/render_gl.cpp
      src/game/game.cpp"

OBJS=""
for f in $SRCS; do
    o="obj/$(basename "${f%.cpp}").o"
    g++ -c -g -O0 -Wall -std=c++17 "$f" -o "$o"
    OBJS="$OBJS $o"
done

g++ $OBJS -o bin/game -lX11 -lGL
```

- Release builds swap `/Od` for `/O2` and `-O0` for `-O2`.
- Without exceptions, drop `/EHsc` and add `-fno-exceptions`.
- **Maybe: unity build.** One `unity_win32.cpp` that `#include`s every `.cpp` for that platform, compiled as one file. Faster full builds and a one-line script, but all files share one scope.

## Platform layer and main loop

Each OS has one file implementing `platform.h`: `platform_win32.cpp` (Win32) and `platform_x11.cpp` (X11, which also runs on Wayland through XWayland). `main.cpp` and the game never touch OS calls.

```cpp
// platform.h
struct Window;  // opaque: HWND on Win32, Display* + ::Window on X11

struct Input {
    bool keys[256];
    int  mouse_x, mouse_y;
    bool mouse_down[3];
};

Window* platform_create_window(const char* title, int w, int h);
void    platform_destroy_window(Window* win);
bool    platform_poll_events(Window* win, Input* input); // false = quit
double  platform_get_time();
void*   platform_alloc(size_t bytes);  // VirtualAlloc / mmap
```

The renderer backend includes the platform's private header to get at the native window handles.

```cpp
// main.cpp (shared by every OS)
int main() {
    Window* win = platform_create_window("Game", 1280, 720);
    renderer_init(win);
    Input input = {};
    double last = platform_get_time();

    while (platform_poll_events(win, &input)) {
        double now = platform_get_time();
        float dt = (float)(now - last);
        last = now;

        game_update(&input, dt);
        renderer_begin_frame(0.1f, 0.1f, 0.1f);
        game_render();
        renderer_end_frame();
    }
    renderer_shutdown();
    platform_destroy_window(win);
}
```

Physics runs at a fixed 60 Hz inside this loop, using an accumulator, separate from the render frame rate.

**Maybe:** macOS (`platform_macos.mm` with Cocoa).

## Input

Input has two layers. The platform layer reports raw device state, and an action-mapping layer turns that into game actions. Game code only asks about actions, never keys or buttons.

```cpp
// raw state, filled by the platform layer
struct GamepadState {
    bool  connected;
    bool  buttons[GAMEPAD_BUTTON_COUNT];
    float left_x, left_y, right_x, right_y; // -1..1
    float left_trigger, right_trigger;      // 0..1
};

struct Input {
    bool  keys[KEY_COUNT];   // engine key enum, not OS codes
    int   mouse_x, mouse_y;
    bool  mouse_down[3];
    float mouse_wheel;
    GamepadState pads[4];
};

// action layer, in engine core
enum Action { ACTION_MOVE_LEFT, ACTION_MOVE_RIGHT, ACTION_JUMP, ACTION_PAUSE, ACTION_COUNT };

bool  input_down(Action a);
bool  input_pressed(Action a);
bool  input_released(Action a);
float input_axis(Action neg, Action pos); // -1..1
```

```toml
[bindings]
jump = ["key_space", "pad_a"]
move_left = ["key_a", "key_left", "pad_left_stick_left", "pad_dpad_left"]
```

- **Gamepads:** XInput on Windows, evdev (`/dev/input/event*`) on Linux.
- **Pressed and released** are computed in the action layer from the previous frame's state.
- **Stick deadzone:** about 0.2.
- **Text entry** uses OS character events (`WM_CHAR`, `XLookupString`), not the key array.
- **Hot-plugging** is supported: XInput polling on Windows, re-scan or `inotify` on Linux.

**⚠ Concern: controller coverage without SDL.** XInput only sees Xbox-style controllers. Our third-party Switch controllers may not show up on Windows.

1. **First:** check each controller for an XInput mode (a switch or a button combo held at power-on, common on 8BitDo). If they all have one, no extra code is needed.
2. **Fallback:** `platform_win32_hid.cpp` using Raw Input / HID (`RegisterRawInputDevices`, `HidP_*`), with `gamecontrollerdb.txt` (data only, no SDL) to map raw buttons to a standard layout. It fills the same `GamepadState` as XInput.
3. **Switch layout:** Nintendo swaps A/B and X/Y. Bindings follow button **position**, not label.

The HID backend is a few hundred lines of fiddly, platform-specific code that's easy to get subtly wrong. Build it only if step 1 fails, and test it against every controller we own.

### Controller-first requirements

- **Gamepad support is core, not optional.** This raises the priority of the concern above: a controller that isn't recognized is a broken game. Test with real controllers from the first playable build.
- **Menus and UI are navigable by focus:** d-pad or stick to move between items, confirm and back buttons, and no screen that requires a mouse.
- **Last-used device tracking:** the engine records whether keyboard/mouse or a gamepad was used last, so the game can switch its on-screen button prompts.
- **Button prompt glyphs** for Xbox and Switch layouts, chosen by the detected controller type.
- **Rumble:** XInput supports it directly. HID rumble is a maybe.
- **Default bindings** ship with full gamepad coverage. Keyboard bindings are added alongside them.

## Renderer

The renderer is native per OS: D3D11 on Windows and OpenGL on Linux, both behind `renderer.h`.

```cpp
// renderer.h
bool renderer_init(Window* win);
void renderer_shutdown();
void renderer_begin_frame(float r, float g, float b);
void renderer_draw_rect(float x, float y, float w, float h, uint32_t color);
void renderer_end_frame();  // present / swap
```

- **D3D11 over D3D12:** far less setup for a 2D engine.
- **OpenGL before Vulkan:** a triangle in a fraction of the code. Vulkan can be added later as `render_vulkan.cpp` without touching anything else.
- **First milestone:** a Win32 window with D3D11 clearing the screen to a color.

**Maybe:** Vulkan on Linux, Metal on macOS.

### Shaders

Shaders are required from day one, since neither D3D11 nor OpenGL draws without them. Each shader is written twice by hand, in HLSL and GLSL. There's no cross-compiler.

```
shaders/
  sprite.hlsl    sprite.glsl     default
  flash.hlsl     flash.glsl      hit flash
  palette.hlsl   palette.glsl    palette swap
  post.hlsl      post.glsl       full-screen pass (CRT, fades)
```

```cpp
int  shader_load(const char* name);   // picks .hlsl or .glsl per backend
void shader_set_float(int shader, const char* name, float v);
void shader_set_vec4(int shader, const char* name, float x, float y, float z, float w);
void renderer_set_shader(int shader); // until changed
```

- **D3D11:** compiled at runtime with `D3DCompile` during development. Release builds can precompile with `fxc`.
- **OpenGL:** GLSL source is always compiled by the driver at runtime.
- **Maybe:** shader hot reload.

## Memory

All memory comes from one block requested at startup through `platform_alloc()` (`VirtualAlloc` or `mmap`) and is carved into arenas grouped by lifetime.

| Arena | Holds | Reset |
| --- | --- | --- |
| Permanent | engine systems, configs, asset table | never |
| Level | entities, map data, level assets | on level change |
| Frame | temp strings, sort buffers, collision pairs | every frame |

```cpp
// arena.h
struct Arena {
    uint8_t* base;
    size_t   size;
    size_t   used;
};

void* arena_push(Arena* a, size_t bytes, size_t align = 8);
void  arena_reset(Arena* a);   // used = 0

#define ARENA_PUSH_STRUCT(a, T)   (T*)arena_push(a, sizeof(T), alignof(T))
#define ARENA_PUSH_ARRAY(a, T, n) (T*)arena_push(a, sizeof(T)*(n), alignof(T))
```

- Objects with unpredictable individual lifetimes (entities, physics bodies, playing sounds) go in **fixed-size pools** with a free list, allocated inside an arena.
- Third-party libraries can later be routed to our allocators: `STBI_MALLOC`, miniaudio's allocation callbacks, and `toml_set_memutil`.

## Entities

Entities are one "fat struct" with feature flags, stored in a pool in the level arena and referenced by generational handles. There is no ECS.

```cpp
// entity.h
enum EntityFlags : uint32_t {
    ENT_ACTIVE      = 1 << 0,
    ENT_HAS_SPRITE  = 1 << 1,
    ENT_HAS_PHYSICS = 1 << 2,
    ENT_PLAYER_CTRL = 1 << 3,
    ENT_HURTS       = 1 << 4,
};

enum EntityKind { KIND_NONE, KIND_PLAYER, KIND_SLIME, KIND_COIN };

struct EntityHandle { uint32_t index, generation; };

struct Entity {
    uint32_t   flags;
    EntityKind kind;
    uint32_t   generation;
    float x, y;
    int   sprite;   // asset handle
    int   body;     // physics body index
    int   health;
    float timer;
    // add fields as the game needs them
};

struct EntityPool {
    Entity entities[MAX_ENTITIES];
    int    free_list[MAX_ENTITIES];
    int    free_count;
};

EntityHandle entity_spawn(EntityPool* p, EntityKind kind);
void         entity_destroy(EntityPool* p, EntityHandle h);
Entity*      entity_get(EntityPool* p, EntityHandle h); // NULL if dead
```

- Per-type behavior is a `switch (e->kind)` in the update loop.
- A reused slot gets a new generation, so stale handles return `NULL` instead of the wrong entity.
- Entities refer to physics bodies and assets by index, never by pointer.
- Starting values (health, speed, sprite) come from TOML.

## Physics (2D)

Physics is a hand-rolled, Box2D-lite system: AABBs and circles only, impulse resolution, stepped at a fixed 60 Hz.

- **Shapes:** AABB and circle.
- **Bodies:** static, dynamic and kinematic.
- **Out of scope for now:** rotation, polygons, joints, continuous collision.

```cpp
// physics.h
enum BodyType  { BODY_STATIC, BODY_DYNAMIC, BODY_KINEMATIC };
enum ShapeType { SHAPE_AABB, SHAPE_CIRCLE };

struct Body {
    BodyType  type;
    ShapeType shape;
    float x, y;           // center
    float vx, vy;
    float half_w, half_h; // AABB
    float radius;         // circle
    float inv_mass;       // 0 = immovable
    float restitution;
    uint32_t layer, mask;
};

struct PhysicsWorld {
    Body  bodies[MAX_BODIES];
    int   body_count;
    float gravity_y;
};

int  physics_add_body(PhysicsWorld* w, Body b);
void physics_step(PhysicsWorld* w, float dt);
```

`physics_step` runs in four stages:

1. **Integrate:** apply gravity, then move by velocity.
2. **Broadphase:** check every pair at first, then a uniform grid once there are a few hundred bodies.
3. **Narrowphase:** AABB–AABB, circle–circle and AABB–circle tests return a normal and a penetration depth.
4. **Resolve:** apply an impulse along the normal, then push the bodies apart.

The player may instead use "move and slide" collision against static boxes for tighter platformer control.

## Audio

Audio uses miniaudio for now, behind `audio.h`. Native backends (WASAPI, ALSA) are a learn-someday goal.

```cpp
// audio.h
bool audio_init();
void audio_shutdown();
int  audio_load_sound(const char* path);
void audio_play(int sound, float volume);
void audio_play_music(const char* path, bool loop);
```

- `audio_miniaudio.cpp` is the only file that includes miniaudio.
- Learning path: first write our own mixer on miniaudio's low-level callback, then replace miniaudio with `audio_wasapi.cpp` and `audio_alsa.cpp`, swapped in by the build script.

## Assets and data

The engine is code-first and data-driven. Game logic lives in C++, and content lives in files authored with existing tools. There is no custom editor yet.

- **Authoring tools:** hand-written TOML for maps, Pixeluna (in-house) for sprites and animations.
- **Images:** PNG, loaded with `stb_image`.
- **Hand-authored data:** TOML, parsed with `tomlc99` behind `config.h`. Arrays were the deciding factor over INI, and text keeps the data easy to edit by hand or with AI.
- **Release packing:** all assets go into one blob with a table of contents (name, offset, size).
- **Map format:** our own TOML format, with layers as character grids plus a legend (see Sprites and tilemaps).

```cpp
// config.h
Config*     config_load(const char* path);
void        config_free(Config* cfg);
int         config_get_int(Config* cfg, const char* table, const char* key, int fallback);
float       config_get_float(Config* cfg, const char* table, const char* key, float fallback);
const char* config_get_string(Config* cfg, const char* table, const char* key, const char* fallback);
int         config_get_array_len(Config* cfg, const char* table, const char* key);
```

```toml
[slime]
health = 20
speed = 1.5
drops = ["goo", "goo", "coin"]
```

Missing keys return the fallback and log a warning instead of crashing.

## Sprites and tilemaps

Sprites and animations come from Pixeluna, our in-house tool. Tilemaps are our own text format, written by hand. There's no map editor in v1, and no Tiled. Everything draws through one sprite batcher.

**Sprite batching.** The renderer collects quads into one large vertex buffer and flushes it when the texture or shader changes. Sprites, tiles and text all use this path.

**Sprites and animation (Pixeluna).**

- `.pxtex`: texture details for a sheet.
- `.pxanim`: animation definitions.
- Loaded by `pxtex` / `pxanim` loaders in `src/engine/assets/`.
- ⚠ **Format spec still to come.** The loaders are blocked until we have it.

**Tilemaps (Tiled).**

- A **tileset** is a grid PNG plus a small TOML file: tile size, and which tile indices are solid.
- A **map** is a TOML file. Each layer is an array of strings, one character per tile, with a legend mapping characters to tile indices. It's readable, diffable, and easy to edit by hand or with AI.
- Engine side: a `Tilemap` struct (width, height, tile size, layers of `uint16_t` tile indices), a loader, and rendering of on-screen tiles only.
- ⚠ Proposed format, not final.

```toml
# forest.tileset.toml
image = "forest.png"
tile_size = 16
solid = [5, 6, 7]
```

```toml
# level1.map.toml
[map]
tileset = "forest"
width = 20
height = 4

[legend]
"." = 0   # grass
"#" = 5   # wall (solid)
"~" = 9   # water

[layers]
ground = [
  "####################",
  "#........~~~.......#",
  "#..................#",
  "####################",
]
```

**Tile collision.** Solid tiles are not physics bodies. Physics queries the tile grid directly ("is the tile at x,y solid?"), using the tileset's `solid` list.

## Text

Text uses TrueType fonts loaded with `stb_truetype` and baked into a texture atlas at load time. Text is drawn as textured quads through the normal sprite path.

```cpp
// font.h
int   font_load(const char* path, float pixel_size); // bakes atlas
void  font_draw_text(int font, const char* text, float x, float y, uint32_t color);
float font_measure_text(int font, const char* text);
```

- The bake covers ASCII 32–126 at first, recording each glyph's atlas position, size and advance.
- Each pixel size needs its own bake.
- For a pixel-art look, use a pixel-style TTF baked at its native size with no texture filtering.
- `stb_truetype.h` lives in `third_party/stb/`.

## Logging

Logging writes one file per session and keeps the last 5. It's hand-rolled in about 80 lines.

```cpp
// log.h
enum LogLevel { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR };

void log_init(const char* path);   // rotates game.log -> game.1.log ... game.5.log
void log_shutdown();
void log_write(LogLevel level, const char* file, int line, const char* fmt, ...);

#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
```

An example line:

```
[12.345] [WARN] config_toml.cpp:42 missing key 'speed' in [slime], using 1.0
```

- Timestamps are seconds since startup.
- Every warning and error flushes the file, so the log survives a crash.
- Output also goes to the console, and to `OutputDebugStringA` on Windows.
- `LOG_DEBUG` compiles out of release builds.

## Decision log

**Decided**

- C-style C++, native platform and renderer layers, backends chosen at link time
- D3D11 on Windows, OpenGL on Linux, X11 for Linux windowing
- Shaders hand-written in both HLSL and GLSL, compiled at runtime
- `build.bat` (MSVC) and `build.sh` (g++)
- Arenas (permanent, level, frame) and pools, on top of `VirtualAlloc` / `mmap`
- Fat-struct entities with flags and generational handles
- Hand-rolled 2D physics: AABB and circle, fixed 60 Hz step
- Controller first: XInput / evdev plus an action-mapping layer with TOML bindings
- miniaudio behind `audio.h`
- Code-first and data-driven, using hand-written TOML tilemaps (no Tiled) and Pixeluna (.pxtex / .pxanim)
- TOML via `tomlc99`, PNG via `stb_image`, all in `third_party/`
- TrueType text via `stb_truetype`, baked to an atlas at load
- Assets packed into one blob with a table of contents for release
- One log file per session, keeping the last 5

**Maybe**

- Unity build
- Vulkan on Linux
- macOS (Cocoa + Metal)
- Hot reload of assets and shaders
- In-game debug editor (Dear ImGui)
- Baking TOML to binary at pack time
- QOI at pack time with a hand-rolled decoder
- SDF fonts for scalable text
- Non-ASCII glyphs
- HID rumble

**Learn someday**

- Native audio (WASAPI, ALSA) and our own mixer
- A PNG and DEFLATE decoder

**Open**

- Pixeluna `.pxtex` / `.pxanim` format spec (coming later)
- ⚠ Concern: Windows HID gamepad backend for third-party Switch controllers, needed only if they lack an XInput mode
