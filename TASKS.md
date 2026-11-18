# Tasks

The engine core from `docs/game-engine-design-outline.md`, built to `docs/cpp.md`, on Windows
first. Work top to bottom. Each task ends with its **Done when** check passing.

## How to work through this list

- Read `CLAUDE.md`, `docs/cpp.md` and `docs/game-engine-design-outline.md` first.
- Do the tasks in order. Earlier tasks are the foundation for later ones.
- After each task: `build.bat` and `build.bat release` both pass with zero warnings,
  `bin\koora.exe --test` passes, then tick the box here and add a one-line note under it if
  anything was decided or left out.
- Verify by running, not by reading: use `--frames`, `--screenshot` and `--test` (tasks 3, 5, 2),
  and read the log file. Look at every screenshot you take.
- A question the docs don't answer: pick the simplest option that fits the design doc, write
  it under **Decisions** at the bottom, and keep going. Stop only for the items under
  **Needs the human**.
- Skip a task that turns out to be blocked; note why under it and continue with the next.
- Commit after each finished task (ticked box included in the commit), message
  `Task N: <what it adds>`. No AI attribution. Do not push.
- Do not add a library beyond the submodules already in `third-party/` without asking.

## Paths at runtime

`bin\koora.exe` finds the repo root as the parent of the exe's folder. Dev builds load game
content from `<root>\src\game\` and shaders from `<root>\src\engine\renderer\shaders\`.
Logs go to `<root>\bin\logs\`. Release packing (task 15) replaces this for shipped builds.

## Engine core

- [x] **1. Logging** (`src/engine/core/log.hpp/.cpp`)
  `log_init`, `log_shutdown`, `log_write`, `LOG_DEBUG/INFO/WARN/ERROR` macros capturing
  file and line, per the design doc. One file per session, keep the last 5
  (`koora.log` → `koora.1.log` … `koora.5.log`). Timestamps are seconds since startup.
  Warnings and errors flush. Also writes to stderr and `OutputDebugStringW`. `LOG_DEBUG`
  compiles out of release. Only the file name, not the full path, in each line.
  **Done when:** running the exe 7 times leaves exactly 6 log files, newest in `koora.log`.
  Note: lines are buffered and appended with open/write/close on each flush, since
  `docs/cpp.md` forbids keeping a `FILE*` open in a global. Info lines flush when the 16 KB
  buffer fills, on any warning/error, and at shutdown.

- [x] **2. Test runner and arenas** (`core/test.hpp/.cpp`, `core/arena.hpp/.cpp`)
  `--test` runs every registered test function, prints each failure with file:line, and exits
  non-zero on any failure. Tests use an explicit check macro, not `assert`, so they also run
  in release. Arena per the Memory section of `docs/cpp.md`: `arena_push` with alignment,
  `arena_reset`, `ARENA_PUSH_STRUCT/ARRAY`, out of memory logs an error and returns `nullptr`.
  Tests: alignment, reset, exhaustion.
  **Done when:** `bin\koora.exe --test` passes in debug and release, and a deliberately broken
  test makes it exit non-zero (then remove the broken test).
  Note: tests live in `<module>_tests.cpp`; each module exposes one suite function, declared
  in `test.hpp` and listed in the table in `test.cpp`. `arena_push` zeroes what it returns.

- [x] **3. Win32 platform and main loop** (`platform/platform.hpp`, `platform_win32.cpp`)
  `platform_alloc` (`VirtualAlloc`), `platform_get_time` (`QueryPerformanceCounter`), window
  create/destroy, `platform_poll_events`. One block allocated at startup and split into the
  Permanent, Level and Frame arenas (sizes as `constexpr` in `main.cpp`). Main loop: poll,
  fixed 60 Hz update with an accumulator (cap catch-up steps), render, reset the frame arena.
  Command line: `--frames N` quits after N rendered frames; `--test` runs tests without a
  window. Window title "koora", 1280x720, resizable.
  **Done when:** `bin\koora.exe --frames 120` opens a window, closes itself, exits 0, and the
  log shows startup, arena sizes and a clean shutdown.
  Note: console subsystem for now (stderr log is visible); the window is per-monitor DPI aware.

- [x] **4. D3D11 renderer: clear screen** (`renderer/renderer.hpp`, `render_d3d11.cpp`)
  Device, swap chain (flip model), render target, resize handling, vsync on.
  `renderer_init/shutdown/begin_frame/end_frame` per the design doc. Shaders are loaded from
  `.hlsl` files and compiled at runtime with `D3DCompile`; a compile error logs the
  compiler's message. Debug builds create the device with the debug layer and log its
  messages if available.
  **Done when:** the window clears to a color for `--frames 120`, with no D3D errors in the log.
  Note: `shader_load` is in this task and loads `sprite.hlsl` at startup, so a shader error
  fails the run early. Every shader shares the sprite vertex layout (pos, uv, color).

- [x] **5. Screenshots** (`--screenshot <path>`)
  On the last frame of a `--frames` run, copy the back buffer to a staging texture and write
  a PNG with `stb_image_write` (from the stb submodule). This is how the agent checks what
  renders, so it comes before any drawing work.
  **Done when:** `bin\koora.exe --frames 10 --screenshot bin\shot.png` writes a PNG of the
  clear color, and you have looked at it.
  Note: PNGs are encoded to memory with `stbi_write_png_to_mem`, then written, so no `FILE*`
  sits in a callback context. stb allocations go to a 64 MB scratch arena reset after each use.

- [x] **6. Sprite batcher, rects and textures** (`renderer/`, `assets/`)
  One dynamic vertex buffer of quads, flushed when the texture or shader changes or the
  buffer fills. `sprite.hlsl`. Orthographic camera in pixels, origin top-left, with a
  camera position for scrolling. `renderer_draw_rect`, `renderer_draw_sprite` (texture +
  source rect + dest rect + tint). PNG loading with `stb_image` in `assets.cpp`, point
  filtering for pixel art. The asset table lives in the Permanent arena.
  **Done when:** a screenshot shows colored rects and a test PNG drawn at 1x and scaled 4x
  with sharp pixels.
  Note: content is loaded by logical name (`art/x.png`, `shaders/sprite.hlsl`) through
  `assets.cpp`. `src/game/game.cpp` holds a test scene until the sandbox (task 14).
  Placeholder art comes from `tools/make_placeholder_art.py`.

- [x] **6b. Low-resolution game screen** (`renderer/`)
  The engine is for NES/SNES-era pixel-art games, so the game draws into a fixed low-res
  render target (default 320x180, set by the game) that is scaled up to the window by the
  largest whole number, centered, with black bars. Point sampling. The camera and all draw
  calls work in game pixels. Screenshots capture the window as shown.
  **Done when:** a screenshot at 1280x720 shows the test scene at exactly 4x with sharp
  pixels, and a resized window keeps whole-number scaling with bars.
  Note: checked with `--window 1000x700` (3x, 960x540 centered). `--window WxH` sets the
  starting window size. `renderer_get_size` and the camera are in game pixels.

- [ ] **7. Config (TOML)** (`config/config.hpp`, `config_toml.cpp`)
  Wrap tomlc17 per the design doc's `config.h`, plus string-array access. Missing keys or
  wrong types return the fallback and `LOG_WARN` with file, table and key. Route tomlc17's
  allocations to our arenas if its API allows; otherwise note it here. Update `build.bat` to
  compile `third-party/tomlc17/src/tomlc17.c` as C with warnings off.
  **Done when:** tests cover present keys, missing keys, wrong types and arrays.

- [ ] **8. Input: keyboard, mouse, XInput, actions** (`input/`, `platform_win32.cpp`)
  Raw state per the design doc (`Input`, `GamepadState`, engine key enum). XInput with
  hot-plug polling and a deadzone of 0.2. Action layer: `input_down/pressed/released/axis`,
  bindings loaded from `src/game/data/bindings.toml` with full gamepad coverage plus
  keyboard. Last-used device tracking. XInput rumble. Text entry from `WM_CHAR`.
  **Done when:** tests cover pressed/released edges and axis math from fake input states,
  and a test binding file loads. Real controller testing is for the human (see below).

- [ ] **9. Text** (`text/font.hpp/.cpp`)
  `stb_truetype`, ASCII 32–126 baked into an atlas per pixel size, drawn through the sprite
  batcher. `font_load`, `font_draw_text`, `font_measure_text`. No font ships yet: if
  `src/game/art/` has no `.ttf`, load `C:\Windows\Fonts\consola.ttf` and log a warning that
  this is a dev-only fallback.
  **Done when:** a screenshot shows readable text at two sizes, and `font_measure_text`
  matches the drawn width.

- [ ] **10. Audio** (`audio/audio.hpp`, `audio_miniaudio.cpp`)
  miniaudio behind `audio.hpp` per the design doc: init, shutdown, load a sound, play with
  volume, music with loop. Only `audio_miniaudio.cpp` includes miniaudio. For a test sound,
  generate a short `.wav` tone (a script is fine) into `src/game/audio/`.
  **Done when:** init, load and play succeed with no errors in the log during a `--frames` run.

- [ ] **11. Entities** (`entity/`)
  Fat struct, flags, `EntityKind`, pool with free list and generational handles, per the
  design doc, using `enum class` where the standard requires it. Lives in the Level arena.
  **Done when:** tests cover spawn, destroy, stale handle returns `nullptr`, slot reuse and
  a full pool.

- [ ] **12. Physics** (`physics/`)
  AABB and circle, static/dynamic/kinematic, the four-stage `physics_step` from the design
  doc, layer/mask filtering, fixed 60 Hz. Brute-force broadphase only. Plus tile-grid
  collision queries (task 13 provides the grid) and a move-and-slide helper for the player.
  **Done when:** tests cover each narrowphase pair, a box resting on the ground staying put
  for 600 steps, and restitution bouncing.

- [ ] **13. Tilemaps** (`assets/` or a `tilemap` module under `engine/`)
  Tileset and map TOML formats from the design doc. `Tilemap` struct in the Level arena,
  loader with clear warnings for bad legend characters or ragged rows, draw only on-screen
  tiles, `tilemap_is_solid(x, y)`. Placeholder art: generate a 16px tileset PNG with a
  script into `src/game/art/placeholder/`.
  **Done when:** a screenshot shows a test map, and tests cover loading and solid queries.

- [ ] **14. Sandbox game** (`src/game/`)
  A tiny test level, not a real game: a player box that runs and jumps on the tilemap with
  move-and-slide, a few coins to collect (entities + overlap), coin count drawn as text,
  a sound on pickup, camera following the player, Escape or Start to quit. Values (speed,
  jump, gravity) come from `src/game/data/*.toml`. Add `--autoplay` that feeds a scripted
  input sequence so a `--frames` run is repeatable.
  **Done when:** `--autoplay --frames 600 --screenshot` shows the player has moved and
  collected at least one coin, confirmed in the log.

- [ ] **15. Release asset pack**
  A tool or `--pack` mode that writes every file under `src/game/` and the shaders into one
  blob with a table of contents (name, offset, size). Release builds read from the blob
  next to the exe; debug builds keep reading loose files. Add `data.pak` to `.gitignore`.
  **Done when:** a release build copied alone with its pack to a temp folder runs the
  sandbox with `--autoplay --frames 300`.

- [ ] **16. Extra shaders** (maybe, only if everything above is done)
  `flash.hlsl` (hit flash), `palette.hlsl` (palette swap), `post.hlsl` (full-screen pass
  with a fade), each switchable with `renderer_set_shader` and the uniform setters from the
  design doc.
  **Done when:** a screenshot shows each effect.

## Needs the human (do not start these)

- **Pixeluna loaders** (`.pxtex` / `.pxanim`): blocked until the format spec exists.
- **Real controller testing**, and the Windows HID backend if any controller lacks an XInput
  mode. Needs physical controllers.
- **Linux** (X11, OpenGL, evdev, `build.sh`): can't be run or tested on this machine yet.
- **Picking a shipping font** to replace the Consolas dev fallback.
- **Choosing the first real game**, which decides the first genre layer.

## Decisions

Record decisions made while working through the list here, one line each, newest last.

- Project includes are written from `src/`: `#include "engine/core/log.hpp"`.
- `LogLevel` members are `LEVEL_DEBUG` etc. because `<windows.h>` defines `ERROR`.
- File and folder helpers (UTF-8 paths, rename, delete, create folder) and debugger output
  live in the platform layer, so `log.cpp` has no OS calls.
- `log_output` prints program results (test summary) to stdout and copies them to the log file.
- `assets_read_file` is the one place files are read whole; the pack (task 15) hooks in there.
- Permanent arena raised to 192 MB to hold the 64 MB asset scratch arena.
- koora is an NES/SNES-era engine: 2D pixel-art games of that scope (not an emulator, not
  building for that hardware). Added task 6b for a fixed low-res game screen; default 320x180
  (16:9, 4x at 720p, 6x at 1080p), changeable per game.
- Dev scripts (placeholder art, test sounds) go in `tools/`, Python standard library only.
