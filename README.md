# SnakeSDL

A single-file Snake game written in C++ with SDL2, SDL2_ttf and SDL2_mixer.

## Features

- Three difficulties (Easy / Medium / Hard) with different board sizes and speeds
- Golden fruit worth 5 points that vanishes if you wait too long
- A new obstacle every 5 fruits, placed so it can never cause an unavoidable death
- The game speeds up gradually as you eat
- Buffered turns, so fast U-turns work
- Light and dark themes, fullscreen, auto-pause on focus loss
- Per-difficulty high scores saved between sessions
- Synthesized sound effects (no audio files needed) and optional background music

## Controls

| Key | Action |
|---|---|
| WASD / Arrow keys | Move (or pick difficulty in the menu) |
| 1 / 2 / 3 | Select difficulty in the menu |
| Enter | Start / restart |
| P or Space | Pause |
| N | Restart after game over |
| T | Toggle theme |
| M | Toggle sound |
| F | Toggle fullscreen |
| Esc | Back to menu (quit from the menu) |

## Build

### Windows (MSYS2 UCRT64)

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 \
          mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_mixer
g++ -std=c++17 SnakeSDL.cpp -o snake.exe -mwindows -static-libgcc -static-libstdc++ \
    -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lSDL2_mixer
```

### Linux (Debian/Ubuntu)

```sh
sudo apt install g++ libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev
g++ -std=c++17 SnakeSDL.cpp -o snake $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_mixer
```

### macOS (Homebrew)

```sh
brew install sdl2 sdl2_ttf sdl2_mixer
g++ -std=c++17 SnakeSDL.cpp -o snake $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_mixer
```

## Optional assets

Place these next to the executable:

- `Arial.ttf`: any TrueType font works under this name. If it is missing, the game tries common system fonts (Arial, DejaVu Sans, Liberation Sans, Noto Sans).

Fonts are not included in this repository.

## Tuning

Gameplay constants (growth per fruit, obstacle frequency, speed-up, golden fruit lifetime) are at the top of `SnakeSDL.cpp`.

## License

Add a license of your choice (for example MIT) as `LICENSE`.
