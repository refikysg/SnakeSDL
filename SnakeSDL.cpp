// SnakeSDL.cpp - Cross-platform SDL2 Snake Game (v7)
//
// Optional assets (place next to the executable, the game runs without them):
//   Arial.ttf        - any TTF works; common system fonts are tried as a fallback
//   snake_music.wav  - background music (sound effects are synthesized, no files needed)
//
// --- Windows (MSYS2 UCRT64) ---
// 1. Install packages:
//    pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-SDL2_mixer
// 2. Compile:
//    g++ -std=c++17 SnakeSDL.cpp -o snake.exe -mwindows -static-libgcc -static-libstdc++ -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lSDL2_mixer
//
// --- Linux (Debian/Ubuntu) ---
//    sudo apt install g++ libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev
//    g++ -std=c++17 SnakeSDL.cpp -o snake $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_mixer
//
// --- macOS (Homebrew) ---
//    brew install sdl2 sdl2_ttf sdl2_mixer
//    g++ -std=c++17 SnakeSDL.cpp -o snake $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_mixer
//
// Controls:
//   Menu    : 1/2/3 or arrows = difficulty, Enter = start, Esc = quit
//   Playing : WASD / arrows = move, P or Space = pause, Esc = back to menu
//   Game over: N or Enter = restart, Esc = menu
//   Any time: T = theme, M = sound, F = fullscreen

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <deque>
#include <vector>
#include <string>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>

using std::string;
using std::vector;
using std::deque;

// ---------------------------------------------------------------- types
enum Direction { UP, DOWN, LEFT, RIGHT };
enum GameState { MENU, PLAYING, PAUSED, GAMEOVER };

struct Vec2 { int x, y; };
inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }

// ---------------------------------------------------------------- tuning
const int    HUD_H              = 40;    // info bar height (pixels)
const int    GAME_CELL          = 24;    // preferred cell size when the window is auto-sized
const int    MENU_W             = 640;
const int    MENU_H             = 480;
const int    GROW_PER_FRUIT     = 2;     // segments gained per fruit (original game behaviour)
const int    OBSTACLE_EVERY     = 5;     // a new obstacle every N fruits eaten
const int    GOLDEN_CHANCE      = 10;    // 1 in N fruits is golden
const int    GOLDEN_LIFETIME_MS = 6000;  // golden fruit vanishes if not eaten in time
const int    SPEEDUP_EVERY      = 5;     // game gets a little faster every N fruits...
const int    SPEEDUP_MS         = 4;     // ...by this many ms per step...
const int    MIN_SPEED_PERCENT  = 55;    // ...but never below this % of the base interval
const double PI_                = 3.14159265358979323846;

const char* diffNames[3] = { "Easy", "Medium", "Hard" };
const int baseSpeeds[3]  = { 180, 120, 80 };   // ms per step
const int colsLevels[3]  = { 20, 30, 40 };
const int rowsLevels[3]  = { 15, 20, 25 };

// ---------------------------------------------------------------- globals
SDL_Window*   window     = nullptr;
SDL_Renderer* renderer   = nullptr;
TTF_Font*     fontLarge  = nullptr;
TTF_Font*     fontSmall  = nullptr;
TTF_Font*     fontHud    = nullptr;
Mix_Music*    music      = nullptr;

bool audioOk = false;
bool soundOn = true;
bool themeDark = false;

int cols = 20, rows = 15;
GameState gameState = MENU;
int difficulty = 1;

deque<Vec2>   snake;
vector<Vec2>  obstacles;
Vec2          fruit = {0, 0};
bool          goldenFruit = false;
int           goldenLeftMs = 0;
Direction     dir = RIGHT;
deque<Direction> inputQueue;       // up to 2 buffered turns
int  score = 0, fruitsEaten = 0, pendingGrowth = 0;
int  highScores[3] = { 0, 0, 0 };
bool newHighScore = false;
bool won = false;
int  stepAccum = 0;

struct Layout { int cell, offX, offY, winW, winH; } L = { 20, 0, 0, 640, 480 };

// ---------------------------------------------------------------- files / assets
static bool FileExists(const string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static string BasePath() {
    static string base;
    static bool init = false;
    if (!init) {
        char* p = SDL_GetBasePath();
        if (p) { base = p; SDL_free(p); }
        init = true;
    }
    return base;
}

// Looks in the current directory first, then next to the executable.
static string FindAsset(const string& name) {
    if (FileExists(name)) return name;
    string b = BasePath() + name;
    if (FileExists(b)) return b;
    return "";
}

static string FindFontFile() {
    const char* local[] = { "Arial.ttf", "arial.ttf" };
    for (const char* n : local) { string s = FindAsset(n); if (!s.empty()) return s; }
    const char* system[] = {
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"
    };
    for (const char* n : system) if (FileExists(n)) return n;
    return "";
}

// ---------------------------------------------------------------- high scores
static string SavePath() {
    static string p;
    if (p.empty()) {
        char* pref = SDL_GetPrefPath("SnakeSDL", "Snake");
        if (pref) { p = string(pref) + "highscores.txt"; SDL_free(pref); }
        else p = "snake_highscores.txt";
    }
    return p;
}

void LoadHighScores() {
    FILE* f = fopen(SavePath().c_str(), "r");
    if (!f) return;
    int a = 0, b = 0, c = 0;
    if (fscanf(f, "%d %d %d", &a, &b, &c) == 3) {
        highScores[0] = std::max(0, a);
        highScores[1] = std::max(0, b);
        highScores[2] = std::max(0, c);
    }
    fclose(f);
}

void SaveHighScores() {
    FILE* f = fopen(SavePath().c_str(), "w");
    if (!f) return;
    fprintf(f, "%d %d %d\n", highScores[0], highScores[1], highScores[2]);
    fclose(f);
}

// ---------------------------------------------------------------- sound
struct Sfx {
    Mix_Chunk* chunk = nullptr;
    vector<Sint16> buf;   // must outlive the chunk (Mix_QuickLoad_RAW does not copy)
};
Sfx sfxEat, sfxGold, sfxDie;

// Generates a short decaying sine sweep, so the game needs no sound files.
void MakeTone(Sfx& s, double f0, double f1, int ms, double vol) {
    int freq = 0, ch = 0;
    Uint16 fmt = 0;
    if (!Mix_QuerySpec(&freq, &fmt, &ch) || fmt != AUDIO_S16SYS || ch < 1) return;
    int frames = freq * ms / 1000;
    s.buf.assign((size_t)frames * ch, 0);
    double phase = 0;
    for (int i = 0; i < frames; ++i) {
        double t = (double)i / frames;
        phase += 2.0 * PI_ * (f0 + (f1 - f0) * t) / freq;
        double env = (1.0 - t) * std::min(1.0, i / (freq * 0.005));   // fade out, 5ms attack
        Sint16 v = (Sint16)(std::sin(phase) * env * vol * 32767.0);
        for (int c = 0; c < ch; ++c) s.buf[(size_t)i * ch + c] = v;
    }
    s.chunk = Mix_QuickLoad_RAW((Uint8*)s.buf.data(), (Uint32)(s.buf.size() * sizeof(Sint16)));
}

void PlaySfx(Sfx& s) {
    if (audioOk && soundOn && s.chunk) Mix_PlayChannel(-1, s.chunk, 0);
}

void ToggleSound() {
    soundOn = !soundOn;
    if (!audioOk || !music) return;
    if (soundOn) {
        if (Mix_PlayingMusic()) Mix_ResumeMusic(); else Mix_PlayMusic(music, -1);
    } else {
        Mix_PauseMusic();
    }
}

// ---------------------------------------------------------------- text (cached textures)
struct CachedText { SDL_Texture* tex; int w, h; };
std::map<string, CachedText> textCache;

void ClearTextCache() {
    for (auto& kv : textCache) SDL_DestroyTexture(kv.second.tex);
    textCache.clear();
}

CachedText* GetText(const string& text, TTF_Font* f, SDL_Color c) {
    if (!f || text.empty()) return nullptr;
    char key[64];
    snprintf(key, sizeof key, "%p|%02x%02x%02x|", (void*)f, c.r, c.g, c.b);
    string k = string(key) + text;
    auto it = textCache.find(k);
    if (it != textCache.end()) return &it->second;

    if (textCache.size() > 256) ClearTextCache();   // scores change, keep the cache bounded

    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text.c_str(), c);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;
    return &textCache.emplace(k, CachedText{ tex, w, h }).first->second;
}

void DrawText(const string& text, int x, int y, TTF_Font* f, SDL_Color c) {
    CachedText* t = GetText(text, f, c);
    if (!t) return;
    SDL_Rect dst = { x, y, t->w, t->h };
    SDL_RenderCopy(renderer, t->tex, nullptr, &dst);
}

void DrawTextCentered(const string& text, int cx, int y, TTF_Font* f, SDL_Color c) {
    CachedText* t = GetText(text, f, c);
    if (!t) return;
    SDL_Rect dst = { cx - t->w / 2, y, t->w, t->h };
    SDL_RenderCopy(renderer, t->tex, nullptr, &dst);
}

void DrawTextRight(const string& text, int rightX, int y, TTF_Font* f, SDL_Color c) {
    CachedText* t = GetText(text, f, c);
    if (!t) return;
    SDL_Rect dst = { rightX - t->w, y, t->w, t->h };
    SDL_RenderCopy(renderer, t->tex, nullptr, &dst);
}

// ---------------------------------------------------------------- drawing helpers
void FillRect(const SDL_Rect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &r);
}

void OutlineRect(const SDL_Rect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(renderer, &r);
}

// Integer cell size => no gaps or uneven cells between tiles.
void UpdateLayout() {
    SDL_GetRendererOutputSize(renderer, &L.winW, &L.winH);
    int availH = L.winH - HUD_H;
    L.cell = std::max(1, std::min(L.winW / cols, availH / rows));
    L.offX = std::max(0, (L.winW - L.cell * cols) / 2);
    L.offY = std::max(0, (availH - L.cell * rows) / 2);
}

SDL_Rect CellRect(int cx, int cy) {
    return SDL_Rect{ L.offX + cx * L.cell, L.offY + cy * L.cell, L.cell, L.cell };
}

// ---------------------------------------------------------------- window sizing
void FitWindow(int w, int h) {
    Uint32 fl = SDL_GetWindowFlags(window);
    if (fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MAXIMIZED)) return;
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void FitWindowToBoard() {
    int cell = GAME_CELL;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(window), &usable) == 0) {
        cell = std::min(cell, std::max(8, (usable.w - 40) / cols));
        cell = std::min(cell, std::max(8, (usable.h - HUD_H - 80) / rows));
    }
    FitWindow(cols * cell, rows * cell + HUD_H);
}

// ---------------------------------------------------------------- game logic
Direction Opposite(Direction d) {
    switch (d) { case UP: return DOWN; case DOWN: return UP; case LEFT: return RIGHT; default: return LEFT; }
}

bool CellBlocked(int x, int y) {
    for (auto& s : snake)     if (s.x == x && s.y == y) return true;
    for (auto& o : obstacles) if (o.x == x && o.y == y) return true;
    return false;
}

// Picks from free cells only, so it can never loop forever. Returns false if the board is full.
bool SpawnFruit() {
    vector<Vec2> freeCells;
    for (int y = 1; y < rows - 1; ++y)
        for (int x = 1; x < cols - 1; ++x)
            if (!CellBlocked(x, y)) freeCells.push_back({ x, y });
    if (freeCells.empty()) return false;
    fruit = freeCells[rand() % freeCells.size()];
    goldenFruit = (rand() % GOLDEN_CHANCE == 0);
    goldenLeftMs = GOLDEN_LIFETIME_MS;
    return true;
}

// Never on the snake, the fruit, another obstacle, or close to the head (no unavoidable deaths).
void SpawnObstacle() {
    Vec2 head = snake.front();
    for (int attempt = 0; attempt < 200; ++attempt) {
        Vec2 ob = { 1 + rand() % (cols - 2), 1 + rand() % (rows - 2) };
        if (CellBlocked(ob.x, ob.y)) continue;
        if (ob == fruit) continue;
        if (std::abs(ob.x - head.x) + std::abs(ob.y - head.y) <= 4) continue;
        obstacles.push_back(ob);
        return;
    }
}

int StepIntervalMs() {
    int base = baseSpeeds[difficulty];
    int reduced = base - (fruitsEaten / SPEEDUP_EVERY) * SPEEDUP_MS;
    return std::max(base * MIN_SPEED_PERCENT / 100, reduced);
}

void ResetGame() {
    cols = colsLevels[difficulty];
    rows = rowsLevels[difficulty];
    score = fruitsEaten = pendingGrowth = 0;
    newHighScore = won = false;
    dir = RIGHT;
    inputQueue.clear();
    snake.clear();
    obstacles.clear();
    int startX = cols / 2, startY = rows / 2;
    for (int i = 0; i < 4; ++i) snake.push_back({ startX - i, startY });
    SpawnFruit();
    stepAccum = 0;
    FitWindowToBoard();
    gameState = PLAYING;
}

void GoToMenu() {
    SaveHighScores();
    gameState = MENU;
    FitWindow(MENU_W, MENU_H);
}

void EndGame(bool win) {
    won = win;
    gameState = GAMEOVER;
    SaveHighScores();
    if (!win) PlaySfx(sfxDie);
}

void QueueDirection(Direction d) {
    if (gameState != PLAYING) return;
    Direction last = inputQueue.empty() ? dir : inputQueue.back();
    if (d == last || d == Opposite(last)) return;
    if (inputQueue.size() < 2) inputQueue.push_back(d);   // two buffered turns = fast U-turns work
}

void StepSnake() {
    if (!inputQueue.empty()) { dir = inputQueue.front(); inputQueue.pop_front(); }

    Vec2 head = snake.front();
    switch (dir) {
        case UP: head.y--; break;
        case DOWN: head.y++; break;
        case LEFT: head.x--; break;
        case RIGHT: head.x++; break;
    }

    if (head.x <= 0 || head.x >= cols - 1 || head.y <= 0 || head.y >= rows - 1) { EndGame(false); return; }

    bool eating  = (head == fruit);
    bool growing = eating || pendingGrowth > 0;

    // If the snake isn't growing, its tail cell is about to be vacated, so moving into it is legal.
    size_t limit = growing ? snake.size() : snake.size() - 1;
    for (size_t i = 0; i < limit; ++i)
        if (snake[i] == head) { EndGame(false); return; }
    for (auto& o : obstacles)
        if (o == head) { EndGame(false); return; }

    snake.push_front(head);
    if (eating)                 pendingGrowth += GROW_PER_FRUIT - 1;   // this move already grew by 1
    else if (pendingGrowth > 0) pendingGrowth--;
    else                        snake.pop_back();

    if (eating) {
        bool wasGolden = goldenFruit;
        score += wasGolden ? 5 : 1;
        fruitsEaten++;
        if (score > highScores[difficulty]) { highScores[difficulty] = score; newHighScore = true; }
        PlaySfx(wasGolden ? sfxGold : sfxEat);
        if (fruitsEaten % OBSTACLE_EVERY == 0) SpawnObstacle();
        if (!SpawnFruit()) EndGame(true);   // board completely full: you win
    }
}

void UpdateGolden(int dtMs) {
    if (!goldenFruit) return;
    goldenLeftMs -= dtMs;
    if (goldenLeftMs <= 0) SpawnFruit();    // missed it: fruit relocates (and may roll golden again)
}

// ---------------------------------------------------------------- rendering
SDL_Color BgColor() { return themeDark ? SDL_Color{ 30, 30, 30, 255 } : SDL_Color{ 230, 230, 230, 255 }; }
SDL_Color TextColor() { return themeDark ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 0, 0, 255 }; }

void DrawEyes(const SDL_Rect& r, Direction d) {
    int e = std::max(2, r.w / 5);
    int a = r.w / 5, b = r.w * 3 / 5;
    int x1, y1, x2, y2;
    switch (d) {
        case RIGHT: x1 = x2 = b; y1 = a; y2 = b; break;
        case LEFT:  x1 = x2 = a; y1 = a; y2 = b; break;
        case UP:    y1 = y2 = a; x1 = a; x2 = b; break;
        default:    y1 = y2 = b; x1 = a; x2 = b; break;
    }
    SDL_Rect e1 = { r.x + x1, r.y + y1, e, e };
    SDL_Rect e2 = { r.x + x2, r.y + y2, e, e };
    FillRect(e1, { 255, 255, 255, 255 });
    FillRect(e2, { 255, 255, 255, 255 });
}

void DrawOverlay(const string& title, SDL_Color titleCol, const string& l1, SDL_Color l1Col, const string& l2) {
    SDL_Rect board = { L.offX, L.offY, L.cell * cols, L.cell * rows };
    FillRect(board, { 0, 0, 0, 150 });
    int cx = board.x + board.w / 2;
    int y  = board.y + board.h / 4;
    DrawTextCentered(title, cx, y, fontLarge, titleCol);
    DrawTextCentered(l1, cx, y + 70, fontSmall, l1Col);
    DrawTextCentered(l2, cx, y + 105, fontSmall, { 255, 255, 255, 255 });
}

void RenderMenu() {
    int cx = L.winW / 2;
    SDL_Color red  = { 220, 40, 40, 255 };
    SDL_Color blue = { 0, 90, 255, 255 };

    DrawTextCentered("SNAKE GAME", cx, 45, fontLarge, red);
    DrawTextCentered("Select difficulty", cx, 120, fontSmall, red);
    for (int i = 0; i < 3; ++i) {
        string line = std::to_string(i + 1) + "  " + diffNames[i] + "   (" +
                      std::to_string(colsLevels[i] - 2) + "x" + std::to_string(rowsLevels[i] - 2) +
                      ")   Best: " + std::to_string(highScores[i]);
        bool sel = (i == difficulty);
        DrawTextCentered(sel ? "> " + line + " <" : line, cx, 165 + i * 38, fontSmall, sel ? blue : red);
    }
    DrawTextCentered("Press Enter to Start", cx, 305, fontSmall, red);

    SDL_Color tc = TextColor();
    DrawTextCentered("Move: WASD / Arrow keys", cx, 375, fontHud, tc);
    DrawTextCentered("P pause    T theme    M sound", cx, 400, fontHud, tc);
    DrawTextCentered("F fullscreen    Esc back / quit", cx, 425, fontHud, tc);
}

void RenderBoard() {
    bool dark = themeDark;
    SDL_Color boardBg  = dark ? SDL_Color{ 38, 38, 38, 255 }    : SDL_Color{ 245, 245, 245, 255 };
    SDL_Color gridCol  = dark ? SDL_Color{ 60, 60, 60, 255 }    : SDL_Color{ 205, 205, 205, 255 };
    SDL_Color wallFill = dark ? SDL_Color{ 190, 190, 190, 255 } : SDL_Color{ 45, 45, 45, 255 };
    SDL_Color wallEdge = dark ? SDL_Color{ 110, 110, 110, 255 } : SDL_Color{ 0, 0, 0, 255 };

    // Board background + grid (interior only)
    SDL_Rect inner = { L.offX + L.cell, L.offY + L.cell, L.cell * (cols - 2), L.cell * (rows - 2) };
    FillRect(inner, boardBg);
    for (int y = 1; y < rows - 1; ++y)
        for (int x = 1; x < cols - 1; ++x)
            OutlineRect(CellRect(x, y), gridCol);

    // Solid walls
    for (int x = 0; x < cols; ++x)
        for (int y = 0; y < rows; ++y)
            if (x == 0 || y == 0 || x == cols - 1 || y == rows - 1) {
                SDL_Rect r = CellRect(x, y);
                FillRect(r, wallFill);
                OutlineRect(r, wallEdge);
            }

    // Obstacles
    for (auto& ob : obstacles) {
        SDL_Rect r = CellRect(ob.x, ob.y);
        FillRect(r, { 110, 110, 110, 255 });
        OutlineRect(r, { 40, 40, 40, 255 });
    }

    // Fruit (golden blinks, and blinks faster when about to vanish)
    {
        SDL_Rect r = CellRect(fruit.x, fruit.y);
        if (goldenFruit) {
            Uint32 period = goldenLeftMs < 2000 ? 100 : 300;
            Uint8 a = ((SDL_GetTicks() / period) % 2) ? 255 : 100;
            FillRect(r, { 255, 215, 0, a });
        } else {
            FillRect(r, { 255, 0, 0, 255 });
        }
    }

    // Snake
    bool dead = (gameState == GAMEOVER && !won);
    SDL_Color headCol = dead ? SDL_Color{ 200, 0, 0, 255 } : SDL_Color{ 0, 0, 255, 255 };
    SDL_Color bodyCol = { 0, 150, 0, 255 };
    for (size_t i = 0; i < snake.size(); ++i) {
        SDL_Rect r = CellRect(snake[i].x, snake[i].y);
        FillRect(r, i == 0 ? headCol : bodyCol);
        OutlineRect(r, { 0, 0, 0, 255 });
        if (i == 0 && L.cell >= 8) DrawEyes(r, dir);
    }

    // Info bar
    int hy = L.winH - HUD_H + 10;
    SDL_Color tc = TextColor();
    SDL_SetRenderDrawColor(renderer, gridCol.r, gridCol.g, gridCol.b, 255);
    SDL_RenderDrawLine(renderer, 0, L.winH - HUD_H, L.winW, L.winH - HUD_H);
    DrawText("Score: " + std::to_string(score) + "   Best: " + std::to_string(highScores[difficulty]),
             8, hy, fontHud, tc);
    string snd = audioOk ? (soundOn ? "On" : "Off") : "N/A";
    DrawTextRight(string(diffNames[difficulty]) + "   " + (themeDark ? "Dark" : "Light") + "   Sound: " + snd,
                  L.winW - 8, hy, fontHud, tc);

    // Overlays
    SDL_Color titleRed = { 255, 70, 70, 255 };
    SDL_Color gold     = { 255, 215, 0, 255 };
    SDL_Color white    = { 255, 255, 255, 255 };
    if (gameState == PAUSED) {
        DrawOverlay("PAUSED", titleRed, "Press P to resume", white, "Esc: menu");
    } else if (gameState == GAMEOVER) {
        string l1 = newHighScore ? "NEW HIGH SCORE: " + std::to_string(score) + "!"
                                 : "Score: " + std::to_string(score) + "   Best: " + std::to_string(highScores[difficulty]);
        DrawOverlay(won ? "YOU WIN!" : "GAME OVER!", won ? gold : titleRed,
                    l1, newHighScore ? gold : white, "N: restart    Esc: menu");
    }
}

void Render() {
    SDL_Color bg = BgColor();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(renderer);

    if (gameState == MENU) RenderMenu();
    else                   RenderBoard();

    SDL_RenderPresent(renderer);
}

// ---------------------------------------------------------------- input
// Returns true when the program should quit.
bool HandleKey(SDL_Keycode key) {
    switch (key) {
        // Difficulty select
        case SDLK_1: if (gameState == MENU) difficulty = 0; break;
        case SDLK_2: if (gameState == MENU) difficulty = 1; break;
        case SDLK_3: if (gameState == MENU) difficulty = 2; break;

        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (gameState == MENU || gameState == GAMEOVER) ResetGame();
            break;

        // Movement (or menu selection)
        case SDLK_w: case SDLK_UP:
            if (gameState == MENU) difficulty = std::max(0, difficulty - 1); else QueueDirection(UP);
            break;
        case SDLK_s: case SDLK_DOWN:
            if (gameState == MENU) difficulty = std::min(2, difficulty + 1); else QueueDirection(DOWN);
            break;
        case SDLK_a: case SDLK_LEFT:
            if (gameState == MENU) difficulty = std::max(0, difficulty - 1); else QueueDirection(LEFT);
            break;
        case SDLK_d: case SDLK_RIGHT:
            if (gameState == MENU) difficulty = std::min(2, difficulty + 1); else QueueDirection(RIGHT);
            break;

        case SDLK_n: if (gameState == GAMEOVER) ResetGame(); break;

        case SDLK_p: case SDLK_SPACE:
            if (gameState == PLAYING) gameState = PAUSED;
            else if (gameState == PAUSED) gameState = PLAYING;
            break;

        case SDLK_t: themeDark = !themeDark; break;
        case SDLK_m: ToggleSound(); break;

        case SDLK_f: {
            bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }

        case SDLK_ESCAPE:
            if (gameState == MENU) return true;
            GoToMenu();
            break;
    }
    return false;
}

// ---------------------------------------------------------------- main
static void Fatal(const char* msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Snake Game", msg, window);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        Fatal(SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        Fatal(TTF_GetError());
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("Snake Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              MENU_W, MENU_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { Fatal(SDL_GetError()); TTF_Quit(); SDL_Quit(); return 1; }
    SDL_SetWindowMinimumSize(window, 400, 300);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { Fatal(SDL_GetError()); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);   // once, for overlays + blinking fruit

    // Fonts (required: the game is text-driven)
    string fontFile = FindFontFile();
    if (!fontFile.empty()) {
        fontLarge = TTF_OpenFont(fontFile.c_str(), 48);
        fontSmall = TTF_OpenFont(fontFile.c_str(), 24);
        fontHud   = TTF_OpenFont(fontFile.c_str(), 18);
    }
    if (!fontLarge || !fontSmall || !fontHud) {
        Fatal("Could not load a font.\n\nPlace any TrueType font named Arial.ttf next to the game and try again.");
        if (fontLarge) TTF_CloseFont(fontLarge);
        if (fontSmall) TTF_CloseFont(fontSmall);
        if (fontHud)   TTF_CloseFont(fontHud);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Audio (optional: the game runs silently if anything here fails)
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 && Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0) {
        audioOk = true;
        string musicFile = FindAsset("snake_music.wav");
        if (!musicFile.empty()) music = Mix_LoadMUS(musicFile.c_str());
        if (music) {
            Mix_VolumeMusic(48);
            Mix_PlayMusic(music, -1);
        }
        MakeTone(sfxEat,  600, 900,  90, 0.35);
        MakeTone(sfxGold, 800, 1600, 220, 0.35);
        MakeTone(sfxDie,  330, 70,   450, 0.45);
    }

    LoadHighScores();

    bool quit = false;
    Uint32 prev = SDL_GetTicks();

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: quit = true; break;
                case SDL_KEYDOWN:
                    if (!e.key.repeat && HandleKey(e.key.keysym.sym)) quit = true;
                    break;
                case SDL_WINDOWEVENT:
                    // Auto-pause when the window loses focus
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && gameState == PLAYING)
                        gameState = PAUSED;
                    break;
            }
        }

        Uint32 now = SDL_GetTicks();
        int dt = (int)std::min<Uint32>(now - prev, 100);   // clamp: no burst of moves after a stall/drag
        prev = now;

        if (gameState == PLAYING) {
            stepAccum += dt;
            int interval = StepIntervalMs();
            while (gameState == PLAYING && stepAccum >= interval) {
                stepAccum -= interval;
                StepSnake();
            }
            if (gameState == PLAYING) UpdateGolden(dt);
        }

        UpdateLayout();
        Render();
        SDL_Delay(2);
    }

    SaveHighScores();

    ClearTextCache();
    if (music) Mix_FreeMusic(music);
    if (sfxEat.chunk)  Mix_FreeChunk(sfxEat.chunk);
    if (sfxGold.chunk) Mix_FreeChunk(sfxGold.chunk);
    if (sfxDie.chunk)  Mix_FreeChunk(sfxDie.chunk);
    if (audioOk) Mix_CloseAudio();
    TTF_CloseFont(fontLarge);
    TTF_CloseFont(fontSmall);
    TTF_CloseFont(fontHud);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}