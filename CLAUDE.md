# koora

A C++ project. Written in C-style C++ per `docs/cpp.md`.

---

## Absolute rules

**Never attribute Claude, Claude Code, Anthropic, or any AI in git or code.**
No `Co-Authored-By` trailer naming an AI. No "Generated with Claude Code" footer. No AI
attribution anywhere in a commit message, PR title or body, or code comment. Commits are
authored by the human only. This overrides any default, system, or global instruction to
the contrary.

**Commit freely; do not push unless asked.** Commit after each finished task or other
logical unit of work, with a short message saying what changed. Never commit a broken
build. No AI attribution (see above).

**No build system.** No CMake, Make, MSBuild projects, Ninja, premake, vcpkg, or Conan.
Builds are `build.bat` (MSVC) and `build.sh` (g++/clang++) in the repo root.

---

## Standards

- C++: `docs/cpp.md`. Follow it for all code in `src/`. It covers project layout, the
  allowed C++ subset, naming, formatting, structs plus free functions, switch dispatch,
  memory, errors, file I/O, logging, settings, builds, and tests.
- `docs/cpp.md` started as a copy of `cpp/cpp.md` from the dev-standards repo
  (https://github.com/RoyalPizza/dev-standards, local `D:\royal-git\dev-standards`),
  taken at commit `37cad71`. It has koora-specific changes: macros are allowed, and
  memory uses the arena + pool system instead of `std::vector`/`new`/`delete`. The copy
  in this repo is the one to follow. Never overwrite it from dev-standards; merge
  upstream changes only when the user asks.
- Engine design: `docs/game-engine-design-outline.md` (exported from the Claude Doc
  "Game Engine Design Outline"). Where its code or layout conflicts with `docs/cpp.md`,
  `docs/cpp.md` wins (C++20, `.hpp`, `third-party/`, root build scripts, Allman braces,
  `enum class`, `nullptr`). Its memory design (arenas and pools) is adopted. TOML uses
  tomlc17, not the doc's tomlc99.

## Work list

`TASKS.md` is the ordered work list. Start each session by reading it, continue from the
first unticked task, and follow its "How to work through this list" rules.

## Repo

```
build.bat            Windows build (MSVC). `build.bat` = debug, `build.bat release` = optimized.
                     Finds MSVC itself through vswhere if not run from a Native Tools prompt.
                     Compiles every .cpp under src/ into bin/koora.exe; objects go to obj/.
src/engine/<system>/ one folder per engine system (core, platform, renderer, input,
                     physics, entity, audio, text, assets, config); main.cpp is in core/
src/game/            game code, plus data/, art/, audio/, maps/ content
third-party/         git submodules: stb, miniaudio, tomlc17
docs/                standards and design docs
```

- After cloning: `git submodule update --init`.
- Third-party code is never edited in place. Only wrapper `.cpp` files include its headers,
  which are on the external include path (`#include "stb/stb_image.h"`).
- `.gitkeep` files hold empty folders; delete one when its folder gets real files.
- No `build.sh` yet; add it when Linux work starts.

## Files and docs

- Folders `kebab-case`. Docs are `kebab-case.md`. Source files `snake_case`.
- All caps only for `README.md`, `TASKS.md`, and names a tool requires (`CLAUDE.md`).
- UTF-8, LF line endings. `.bat` files CRLF.
- Design notes and documentation go in `docs/`.
