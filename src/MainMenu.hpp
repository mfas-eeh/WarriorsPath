#pragma once

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace wp {

constexpr int VW = 1376;
constexpr int VH = 768;

struct GameSettings {
    float master = 0.80f;
    float music = 0.70f;
    float sfx = 0.80f;
    float brightness = 1.00f;
    bool fullscreen = false;
    bool vsync = true;
    bool showFps = false;
    int particleQuality = 2;
};

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float Rand01() { return (float)GetRandomValue(0, 10000) / 10000.0f; }
inline float RandR(float a, float b) { return a + (b - a) * Rand01(); }
inline float Smooth(float t) { t = Clampf(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline float Hash(int n) { float s = sinf((float)n * 12.9898f) * 43758.5453f; return s - floorf(s); }

inline float Approach(float cur, float target, float speed, float dt) {
    if (cur < target) return std::min(cur + speed * dt, target);
    return std::max(cur - speed * dt, target);
}

inline unsigned char U8(float v) { return (unsigned char)Clampf(v, 0.0f, 255.0f); }
inline Color Col(float r, float g, float b, float a = 255.0f) { return Color{ U8(r), U8(g), U8(b), U8(a) }; }
inline Color A(Color c, float k) { c.a = U8((float)c.a * k); return c; }
inline Color Mix(Color a, Color b, float t) {
    return Col(Lerpf(a.r, b.r, t), Lerpf(a.g, b.g, t), Lerpf(a.b, b.b, t), Lerpf(a.a, b.a, t));
}

inline Color FlameColor(float t) {
    if (t < 0.25f) return Mix(Col(255, 252, 215), Col(255, 205, 70), t / 0.25f);
    if (t < 0.65f) return Mix(Col(255, 205, 70), Col(255, 110, 20), (t - 0.25f) / 0.40f);
    return Mix(Col(255, 110, 20), Col(190, 40, 12), (t - 0.65f) / 0.35f);
}

inline void SaveSettings(const GameSettings& s, const char* path = "settings.cfg") {
    std::ofstream f(path);
    if (!f) return;
    f << "master=" << s.master << "\n";
    f << "music=" << s.music << "\n";
    f << "sfx=" << s.sfx << "\n";
    f << "brightness=" << s.brightness << "\n";
    f << "fullscreen=" << (s.fullscreen ? 1 : 0) << "\n";
    f << "vsync=" << (s.vsync ? 1 : 0) << "\n";
    f << "showFps=" << (s.showFps ? 1 : 0) << "\n";
    f << "particleQuality=" << s.particleQuality << "\n";
}

inline GameSettings LoadSettings(const char* path = "settings.cfg") {
    GameSettings s;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        float v = (float)atof(line.substr(eq + 1).c_str());
        if (k == "master") s.master = Clampf(v, 0.0f, 1.0f);
        else if (k == "music") s.music = Clampf(v, 0.0f, 1.0f);
        else if (k == "sfx") s.sfx = Clampf(v, 0.0f, 1.0f);
        else if (k == "brightness") s.brightness = Clampf(v, 0.5f, 1.5f);
        else if (k == "fullscreen") s.fullscreen = v > 0.5f;
        else if (k == "vsync") s.vsync = v > 0.5f;
        else if (k == "showFps") s.showFps = v > 0.5f;
        else if (k == "particleQuality") s.particleQuality = (int)Clampf(v, 0.0f, 2.0f);
    }
    return s;
}

inline void DrawBrightnessOverlay(const GameSettings& s) {
    float b = s.brightness;
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    if (b < 0.999f) {
        float a = Clampf((1.0f - b) / 0.5f, 0.0f, 1.0f) * 0.60f;
        DrawRectangle(0, 0, w, h, Col(0, 0, 0, a * 255.0f));
    } else if (b > 1.001f) {
        float a = Clampf((b - 1.0f) / 0.5f, 0.0f, 1.0f) * 0.25f;
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(0, 0, w, h, Col(255, 255, 255, a * 255.0f));
        EndBlendMode();
    }
}

inline void OutlineRounded(Rectangle r, float rad, float th, Color c) {
    rad = std::min(rad, std::min(r.width, r.height) * 0.5f);
    DrawRectangleRec({ r.x + rad, r.y, r.width - 2 * rad, th }, c);
    DrawRectangleRec({ r.x + rad, r.y + r.height - th, r.width - 2 * rad, th }, c);
    DrawRectangleRec({ r.x, r.y + rad, th, r.height - 2 * rad }, c);
    DrawRectangleRec({ r.x + r.width - th, r.y + rad, th, r.height - 2 * rad }, c);
    float in = std::max(rad - th, 0.0f);
    DrawRing({ r.x + rad, r.y + rad }, in, rad, 180, 270, 12, c);
    DrawRing({ r.x + r.width - rad, r.y + rad }, in, rad, 270, 360, 12, c);
    DrawRing({ r.x + r.width - rad, r.y + r.height - rad }, in, rad, 0, 90, 12, c);
    DrawRing({ r.x + rad, r.y + r.height - rad }, in, rad, 90, 180, 12, c);
}

inline void FillRounded(Rectangle r, float rad, Color c) {
    float m = std::min(r.width, r.height);
    DrawRectangleRounded(r, Clampf(rad * 2.0f / m, 0.0f, 1.0f), 10, c);
}

inline Texture2D MakeGlowTexture(int n) {
    Image img = GenImageColor(n, n, BLANK);
    float c = (n - 1) * 0.5f;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float d = sqrtf((x - c) * (x - c) + (y - c) * (y - c)) / c;
            float a = powf(Clampf(1.0f - d, 0.0f, 1.0f), 2.0f);
            ImageDrawPixel(&img, x, y, Col(255, 255, 255, a * 255.0f));
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

inline Texture2D MakeFlameTexture(int w, int h) {
    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y++) {
        float v = ((float)y + 0.5f) / (float)h;
        float hw = 1.0f * powf(v, 0.36f);
        for (int x = 0; x < w; x++) {
            float u = (((float)x + 0.5f) / (float)w - 0.5f) * 2.0f;
            float f = hw > 0.001f ? 1.0f - powf(fabsf(u) / hw, 1.7f) : 0.0f;
            f = Clampf(f, 0.0f, 1.0f);
            float a = powf(f, 1.1f) * (0.5f + 0.5f * v) * (1.0f - powf(v, 6.0f));
            ImageDrawPixel(&img, x, y, Col(255, 255, 255, a * 255.0f));
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

inline Texture2D MakeLeafTexture() {
    const int w = 32, h = 16;
    Image img = GenImageColor(w, h, BLANK);
    for (int x = 0; x < w; x++) {
        float t = (float)x / (float)(w - 1);
        float half = sinf(PI * t) * 7.0f;
        for (int y = 0; y < h; y++) {
            float dy = fabsf((float)y - 7.5f);
            float a = Clampf((half - dy) * 0.8f + 0.5f, 0.0f, 1.0f);
            float shade = dy < 0.7f ? 190.0f : 255.0f;
            ImageDrawPixel(&img, x, y, Col(shade, shade, shade, a * 255.0f));
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

enum class MenuAction { None, StartGame, Quit };

class MainMenu {
public:
    MenuAction action = MenuAction::None;

    void Init(GameSettings* settings, const std::string& dir = "assets/menu") {
        S = settings;

        const char* bgCandidates[] = {
            "main.png",
            "assets/main.png",
            "main.jpeg",
            "assets/main.jpeg",
            "assets/menu/main.png",
            "assets/menu/menu_bg.png",
            "menu_bg.png"
        };
        for (const char* path : bgCandidates) {
            if (FileExists(path)) {
                bg = LoadTexture(path);
                SetTextureFilter(bg, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
        if (!bg.id) {
            std::string bgPath = dir + "/menu_bg.png";
            if (FileExists(bgPath.c_str())) {
                bg = LoadTexture(bgPath.c_str());
                SetTextureFilter(bg, TEXTURE_FILTER_BILINEAR);
            }
        }

        glow = MakeGlowTexture(128);
        flame = MakeFlameTexture(64, 128);
        leaf = MakeLeafTexture();

        canvas = LoadRenderTexture(VW, VH);
        SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);

        std::string fp = dir + "/menu_font.ttf";
        if (FileExists(fp.c_str())) {
            font = LoadFontEx(fp.c_str(), 96, nullptr, 0);
            SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
            ownFont = true;
        } else {
            font = GetFontDefault();
        }

        LoadAudio(dir);

        panelRT = LoadRenderTexture((int)panel.width, (int)panel.height);
        BakePanel();

        pool.resize(3200);
        ApplyAll(true);
        fade = 1.0f;
        fadeTarget = 0.0f;
        glintTimer = 2.5f;
    }

    void Unload() {
        if (bg.id) UnloadTexture(bg);
        UnloadTexture(glow);
        UnloadTexture(flame);
        UnloadTexture(leaf);
        UnloadRenderTexture(canvas);
        UnloadRenderTexture(panelRT);
        if (ownFont) UnloadFont(font);
        if (soundOk) { UnloadSound(sHover); UnloadSound(sClick); }
        if (themeOk) UnloadMusicStream(theme);
        if (fireOk) UnloadMusicStream(fireLoop);
    }

    void Reset() {
        action = MenuAction::None;
        pending = MenuAction::None;
        screen = Screen::Main;
        sel = 0;
        fade = 1.0f;
        fadeTarget = 0.0f;
        settingsT = 0.0f;
        if (themeOk) PlayMusicStream(theme);
    }

    void StopMusic() {
        if (themeOk) StopMusicStream(theme);
        if (fireOk) StopMusicStream(fireLoop);
    }

    void OpenSettings() {
        screen = Screen::Settings;
        setSel = 0;
        mouseWasMoved = false;
    }

    void SelectButton(int i) { sel = i; }

    void Update(float dt) {
        dt = Clampf(dt, 0.0f, 0.05f);
        time += dt;
        UpdateLayout();

        if (themeOk) UpdateMusicStream(theme);
        if (fireOk) UpdateMusicStream(fireLoop);

        settingsT = Approach(settingsT, screen == Screen::Settings ? 1.0f : 0.0f, 5.0f, dt);
        fade = Approach(fade, fadeTarget, fadeTarget > fade ? 1.8f : 1.2f, dt);
        if (pending != MenuAction::None && fade >= 0.999f) action = pending;

        Vector2 md = GetMouseDelta();
        mouseWasMoved = fabsf(md.x) + fabsf(md.y) > 0.5f;

        bool locked = pending != MenuAction::None;
        hovering = false;
        if (!locked) {
            if (screen == Screen::Main) UpdateMain();
            else UpdateSettings(dt);
        }
        SetMouseCursor(hovering ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);

        float maxK = 0.0f;
        for (int i = 0; i < 3; i++) {
            float target = (screen == Screen::Main && sel == i) ? 1.0f : 0.0f;
            btn[i].k = Approach(btn[i].k, target, target > btn[i].k ? 3.2f : 2.0f, dt);
            if (btn[i].k > 0.01f) EmitFlames(btn[i].r, btn[i].k, dt, btn[i].acc, 0, 1.0f);
            maxK = std::max(maxK, btn[i].k);
        }

        for (int i = 0; i < R_COUNT; i++) {
            float target = (screen == Screen::Settings && setSel == i) ? 1.0f : 0.0f;
            rowK[i] = Approach(rowK[i], target, target > rowK[i] ? 5.0f : 3.0f, dt);
        }
        if (settingsT > 0.5f && rowK[setSel] > 0.01f) {
            if (setSel == R_RESET || setSel == R_BACK) {
                EmitFlames(ButtonRect(setSel), rowK[setSel], dt, setAcc, 1, 0.6f);
            } else {
                setAcc += dt * 9.0f * rowK[setSel] * Quality();
                Rectangle r = RowRect(setSel);
                while (setAcc >= 1.0f) {
                    setAcc -= 1.0f;
                    Particle& p = Spawn(PKind::Spark, 1);
                    p.pos = { r.x + RandR(10.0f, r.width - 10.0f), r.y + r.height - 4.0f };
                    p.vel = { RandR(-12.0f, 12.0f), -RandR(30.0f, 70.0f) };
                    p.maxLife = p.life = RandR(0.6f, 1.2f);
                    p.size = RandR(2.0f, 4.0f);
                    p.phase = RandR(0.0f, 6.28f);
                    p.color = Col(255, 190, 80);
                }
            }
        }

        if (fireOk) SetMusicVolume(fireLoop, maxK * 0.55f * S->sfx);

        Ambient(dt);
        UpdateGlint(dt);
        UpdateParticles(dt);
    }

    void RenderCanvas() {
        BeginTextureMode(canvas);
        ClearBackground(BLACK);

        if (bg.id) {
            DrawTexturePro(bg, { 0, 0, (float)bg.width, (float)bg.height },
                           { 0, 0, (float)VW, (float)VH }, { 0, 0 }, 0.0f, WHITE);
        } else {
            DrawRectangleGradientV(0, 0, VW, VH, Col(60, 90, 60), Col(20, 40, 25));
        }

        DrawParticles(0);

        for (int i = 0; i < 3; i++) DrawButtonGlow(btn[i].r, btn[i].k, 1.0f);
        DrawSwordGlint();

        DrawRectangleGradientV(0, 0, VW, 150, Col(0, 0, 0, 60), Col(0, 0, 0, 0));
        DrawRectangleGradientV(0, VH - 190, VW, 190, Col(0, 0, 0, 0), Col(0, 0, 0, 95));
        DrawRectangleGradientH(0, 0, 150, VH, Col(0, 0, 0, 55), Col(0, 0, 0, 0));
        DrawRectangleGradientH(VW - 150, 0, 150, VH, Col(0, 0, 0, 0), Col(0, 0, 0, 55));
        DrawRectangle(0, 0, VW, VH, Col(0, 0, 0, 10.0f + 10.0f * sinf(time * 0.9f)));

        if (settingsT > 0.001f) {
            float e = Smooth(settingsT);
            DrawRectangle(0, 0, VW, VH, Col(0, 0, 0, 190.0f * e));
            DrawSettings(e);
            DrawParticles(1);
        }

        if (fade > 0.001f) DrawRectangle(0, 0, VW, VH, Col(0, 0, 0, 255.0f * fade));
        EndTextureMode();
    }

    void DrawFrameToScreen() {
        UpdateLayout();
        ClearBackground(BLACK);
        rlDrawRenderBatchActive();
        rlDisableColorBlend();
        DrawTexturePro(canvas.texture, { 0, 0, (float)VW, -(float)VH },
                       { offX, offY, VW * scale, VH * scale }, { 0, 0 }, 0.0f, WHITE);
        rlDrawRenderBatchActive();
        rlEnableColorBlend();
        DrawBrightnessOverlay(*S);
        if (S->showFps) DrawFPS(12, 12);
    }

    void Draw() {
        RenderCanvas();
        BeginDrawing();
        DrawFrameToScreen();
        EndDrawing();
    }

    const RenderTexture2D& Canvas() const { return canvas; }

private:
    enum class Screen { Main, Settings };
    enum class PKind : unsigned char { Flame, Spark, Ember, Firefly, Leaf, Smoke, Twinkle };
    enum Row { R_MASTER, R_MUSIC, R_SFX, R_BRIGHT, R_FULL, R_VSYNC, R_FPS, R_PARTICLES, R_RESET, R_BACK, R_COUNT };

    struct Particle {
        Vector2 pos{}, vel{};
        float life = 0, maxLife = 1, size = 10, rot = 0, rotVel = 0, phase = 0, power = 1;
        Color color{ 255, 255, 255, 255 };
        PKind kind = PKind::Spark;
        unsigned char layer = 0;
        bool alive = false;
    };

    struct MenuButton {
        Rectangle r;
        const char* label;
        float k = 0.0f;
        float acc = 0.0f;
    };

    GameSettings* S = nullptr;
    Texture2D bg{}, glow{}, flame{}, leaf{};
    RenderTexture2D canvas{}, panelRT{};
    Font font{};
    bool ownFont = false;

    Sound sHover{}, sClick{};
    Music theme{}, fireLoop{};
    bool soundOk = false, themeOk = false, fireOk = false;

    Screen screen = Screen::Main;
    MenuAction pending = MenuAction::None;
    int sel = 0;
    int setSel = 0;
    int dragging = -1;
    float time = 0.0f;
    float settingsT = 0.0f;
    float fade = 1.0f;
    float fadeTarget = 0.0f;
    float rowK[R_COUNT] = {};
    float setAcc = 0.0f;
    bool hovering = false;
    bool mouseWasMoved = false;

    float scale = 1.0f, offX = 0.0f, offY = 0.0f;
    Vector2 mouse{};
    int windowedW = 1280, windowedH = 720;

    float emberAcc = 0.0f, twinkleAcc = 0.0f, leafAcc = 0.0f, smokeAcc[3] = {};
    float glintTimer = 3.0f, glintT = 1.0f;
    bool glintActive = false;
    int fireflyCount = 0;

    Rectangle panel{ 308, 70, 760, 628 };

    MenuButton btn[3] = {
        { { 962, 210, 346, 115 }, "START" },
        { { 972, 345, 326, 98 }, "SETTINGS" },
        { { 970, 473, 332, 102 }, "QUIT" },
    };

    Vector2 chimneys[3] = { { 778, 196 }, { 527, 266 }, { 869, 330 } };

    std::vector<Particle> pool;
    size_t cursor = 0;

    float Quality() const {
        static const float q[3] = { 0.35f, 0.70f, 1.00f };
        return q[std::max(0, std::min(2, S->particleQuality))];
    }

    void UpdateLayout() {
        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();
        scale = std::min(sw / VW, sh / VH);
        offX = (sw - VW * scale) * 0.5f;
        offY = (sh - VH * scale) * 0.5f;
        Vector2 m = GetMousePosition();
        mouse = { (m.x - offX) / scale, (m.y - offY) / scale };
    }

    void LoadAudio(const std::string& dir) {
        if (!IsAudioDeviceReady()) return;
        std::string h = dir + "/hover.wav", c = dir + "/click.wav";
        std::string t = dir + "/theme.ogg", f = dir + "/fire_loop.ogg";
        if (FileExists(h.c_str()) && FileExists(c.c_str())) {
            sHover = LoadSound(h.c_str());
            sClick = LoadSound(c.c_str());
            soundOk = true;
        } else if (FileExists("GAMEEE/sword_swing.mp3")) {
            sHover = LoadSound("GAMEEE/sword_swing.mp3");
            sClick = LoadSound("GAMEEE/sword_swing.mp3");
            soundOk = true;
        }
        if (FileExists(t.c_str())) {
            theme = LoadMusicStream(t.c_str());
            theme.looping = true;
            PlayMusicStream(theme);
            themeOk = true;
        } else if (FileExists("GAMEEE/dark_forest.mp3")) {
            theme = LoadMusicStream("GAMEEE/dark_forest.mp3");
            theme.looping = true;
            PlayMusicStream(theme);
            themeOk = true;
        } else if (FileExists("GAMEEE/origin_bg.mp3")) {
            theme = LoadMusicStream("GAMEEE/origin_bg.mp3");
            theme.looping = true;
            PlayMusicStream(theme);
            themeOk = true;
        }
        if (FileExists(f.c_str())) {
            fireLoop = LoadMusicStream(f.c_str());
            fireLoop.looping = true;
            SetMusicVolume(fireLoop, 0.0f);
            PlayMusicStream(fireLoop);
            fireOk = true;
        }
    }

    void PlayHover() { if (soundOk) { SetSoundVolume(sHover, S->sfx); PlaySound(sHover); } }
    void PlayClick() { if (soundOk) { SetSoundVolume(sClick, S->sfx); PlaySound(sClick); } }

    void ApplyAll(bool windowToo) {
        SetMasterVolume(S->master);
        if (themeOk) SetMusicVolume(theme, S->music);
        if (windowToo) ApplyWindow();
    }

    void ApplyWindow() {
        if (S->vsync) SetWindowState(FLAG_VSYNC_HINT);
        else ClearWindowState(FLAG_VSYNC_HINT);
        int hz = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(S->vsync ? (hz > 0 ? hz : 60) : 144);

        if (S->fullscreen != IsWindowFullscreen()) {
            if (S->fullscreen) {
                windowedW = GetScreenWidth();
                windowedH = GetScreenHeight();
                int m = GetCurrentMonitor();
                SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
                ToggleFullscreen();
            } else {
                ToggleFullscreen();
                SetWindowSize(windowedW, windowedH);
            }
        }
    }

    Particle& Spawn(PKind k, unsigned char layer = 0) {
        size_t n = pool.size();
        for (size_t i = 0; i < n; i++) {
            size_t idx = (cursor + i) % n;
            if (!pool[idx].alive) {
                cursor = idx + 1;
                Particle& p = pool[idx];
                p = Particle{};
                p.alive = true;
                p.kind = k;
                p.layer = layer;
                return p;
            }
        }
        cursor = (cursor + 1) % n;
        Particle& p = pool[cursor];
        p = Particle{};
        p.alive = true;
        p.kind = k;
        p.layer = layer;
        return p;
    }

    void Burst(Vector2 c, int n, unsigned char layer) {
        for (int i = 0; i < n; i++) {
            Particle& p = Spawn(PKind::Spark, layer);
            float ang = RandR(0.0f, 6.2831f);
            float sp = RandR(60.0f, 320.0f);
            p.pos = { c.x + RandR(-60.0f, 60.0f), c.y + RandR(-20.0f, 20.0f) };
            p.vel = { cosf(ang) * sp, sinf(ang) * sp * 0.7f - 60.0f };
            p.maxLife = p.life = RandR(0.5f, 1.3f);
            p.size = RandR(2.5f, 5.5f);
            p.phase = RandR(0.0f, 6.28f);
            p.color = Rand01() < 0.5f ? Col(255, 210, 100) : Col(255, 140, 40);
        }
    }

    void EmitFlames(const Rectangle& r, float k, float dt, float& acc, unsigned char layer, float sc) {
        float q = Quality();
        acc += dt * 78.0f * k * q;
        while (acc >= 1.0f) {
            acc -= 1.0f;
            Particle& p = Spawn(PKind::Flame, layer);
            bool side = Rand01() < 0.12f;
            if (side) {
                bool left = Rand01() < 0.5f;
                p.pos = { left ? r.x + 16.0f : r.x + r.width - 16.0f, r.y + RandR(10.0f, r.height * 0.35f) };
                p.vel = { (left ? -1.0f : 1.0f) * RandR(6.0f, 22.0f), -RandR(40.0f, 70.0f) * sc };
                p.size = RandR(22.0f, 36.0f) * sc;
            } else {
                float x;
                if (Rand01() < 0.65f) {
                    int c = GetRandomValue(0, 2);
                    float cx = r.x + r.width * (0.5f + 0.34f * sinf(time * (1.1f + 0.55f * (float)c) + (float)c * 2.1f));
                    x = cx + (Rand01() + Rand01() - 1.0f) * 38.0f * sc;
                } else {
                    x = r.x + r.width * RandR(0.08f, 0.92f);
                }
                x = Clampf(x, r.x + r.width * 0.07f, r.x + r.width * 0.93f);
                float bias = 1.0f - fabsf((x - r.x) / r.width - 0.5f) * 0.9f;
                p.pos = { x, r.y + 24.0f * sc };
                p.vel = { RandR(-14.0f, 14.0f), -RandR(50.0f, 95.0f) * bias * sc };
                p.size = RandR(28.0f, 50.0f) * sc;
            }
            p.maxLife = p.life = RandR(0.50f, 0.90f);
            p.phase = RandR(0.0f, 6.28f);
            p.power = RandR(0.75f, 1.0f);

            if (Rand01() < 0.22f) {
                Particle& s = Spawn(PKind::Spark, layer);
                s.pos = { p.pos.x, p.pos.y - 6.0f };
                s.vel = { RandR(-40.0f, 40.0f), -RandR(60.0f, 170.0f) };
                s.maxLife = s.life = RandR(0.8f, 1.6f);
                s.size = RandR(2.0f, 4.2f);
                s.phase = RandR(0.0f, 6.28f);
                s.color = Rand01() < 0.6f ? Col(255, 200, 90) : Col(255, 240, 180);
            }
        }
    }

    void Ambient(float dt) {
        float q = Quality();

        emberAcc += dt * 16.0f * q;
        while (emberAcc >= 1.0f) {
            emberAcc -= 1.0f;
            Particle& p = Spawn(PKind::Ember);
            bool left = Rand01() < 0.6f;
            p.pos = { left ? RandR(0.0f, 560.0f) : RandR(0.0f, (float)VW), RandR(VH * 0.35f, VH + 10.0f) };
            p.vel = { RandR(4.0f, 26.0f), -RandR(14.0f, 46.0f) };
            p.maxLife = p.life = RandR(4.0f, 8.0f);
            p.size = RandR(3.5f, 8.0f);
            p.phase = RandR(0.0f, 6.28f);
            p.color = Rand01() < 0.7f ? Col(255, 150, 40) : Col(255, 200, 90);
        }

        int target = (int)(16.0f * q);
        if (fireflyCount < target) {
            Particle& p = Spawn(PKind::Firefly);
            if (Rand01() < 0.7f) p.pos = { RandR(20.0f, 620.0f), RandR(260.0f, (float)VH - 20.0f) };
            else p.pos = { RandR(880.0f, (float)VW - 20.0f), RandR(600.0f, (float)VH - 20.0f) };
            p.vel = { RandR(-8.0f, 8.0f), RandR(-8.0f, 8.0f) };
            p.maxLife = p.life = RandR(6.0f, 11.0f);
            p.size = RandR(14.0f, 24.0f);
            p.phase = RandR(0.0f, 6.28f);
            p.color = Rand01() < 0.6f ? Col(255, 232, 120) : Col(190, 255, 150);
        }

        twinkleAcc += dt * 2.6f * q;
        while (twinkleAcc >= 1.0f) {
            twinkleAcc -= 1.0f;
            Particle& p = Spawn(PKind::Twinkle);
            if (Rand01() < 0.25f) p.pos = { RandR(470.0f, 502.0f), RandR(440.0f, 750.0f) };
            else p.pos = { RandR(190.0f, 520.0f), RandR(250.0f, 740.0f) };
            p.maxLife = p.life = RandR(0.9f, 1.7f);
            p.size = RandR(6.0f, 13.0f);
            float pick = Rand01();
            p.color = pick < 0.4f ? Col(255, 255, 255) : (pick < 0.75f ? Col(255, 235, 170) : Col(200, 240, 255));
        }

        leafAcc += dt * 0.8f * q;
        while (leafAcc >= 1.0f) {
            leafAcc -= 1.0f;
            Particle& p = Spawn(PKind::Leaf);
            p.pos = { RandR(-40.0f, VW * 0.9f), -20.0f };
            p.vel = { RandR(25.0f, 60.0f), RandR(35.0f, 70.0f) };
            p.maxLife = p.life = 30.0f;
            p.size = RandR(9.0f, 15.0f);
            p.rot = RandR(0.0f, 360.0f);
            p.rotVel = RandR(-150.0f, 150.0f);
            p.phase = RandR(0.0f, 6.28f);
            float pick = Rand01();
            p.color = pick < 0.35f ? Col(95, 150, 60) : (pick < 0.7f ? Col(125, 172, 72)
                    : (pick < 0.88f ? Col(168, 150, 52) : Col(205, 120, 42)));
        }

        for (int i = 0; i < 3; i++) {
            smokeAcc[i] += dt * 3.0f * (0.5f + 0.5f * q);
            while (smokeAcc[i] >= 1.0f) {
                smokeAcc[i] -= 1.0f;
                Particle& p = Spawn(PKind::Smoke);
                p.pos = { chimneys[i].x + RandR(-3.0f, 3.0f), chimneys[i].y };
                p.vel = { RandR(6.0f, 16.0f), -RandR(18.0f, 30.0f) };
                p.maxLife = p.life = RandR(4.2f, 5.5f);
                p.size = RandR(8.0f, 14.0f);
                p.phase = RandR(0.0f, 6.28f);
            }
        }
    }

    void UpdateGlint(float dt) {
        glintTimer -= dt;
        if (!glintActive && glintTimer <= 0.0f) {
            glintActive = true;
            glintT = 0.0f;
            glintTimer = RandR(5.5f, 8.5f);
        }
        if (glintActive) {
            glintT += dt / 1.1f;
            if (glintT >= 1.0f) { glintActive = false; return; }
            if (Rand01() < 0.5f) {
                Vector2 g = GlintPos();
                Particle& p = Spawn(PKind::Twinkle);
                p.pos = { g.x + RandR(-6.0f, 6.0f), g.y + RandR(-10.0f, 0.0f) };
                p.maxLife = p.life = RandR(0.4f, 0.8f);
                p.size = RandR(4.0f, 8.0f);
                p.color = Col(255, 250, 220);
            }
        }
    }

    Vector2 GlintPos() const {
        float e = Smooth(glintT);
        return { Lerpf(487.0f, 483.0f, e), Lerpf(432.0f, 760.0f, e) };
    }

    void UpdateParticles(float dt) {
        fireflyCount = 0;
        for (auto& p : pool) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) { p.alive = false; continue; }

            switch (p.kind) {
            case PKind::Flame:
                p.vel.x += sinf(time * 9.0f + p.phase) * 90.0f * dt;
                p.vel.y -= 40.0f * dt;
                p.vel.x *= (1.0f - 1.5f * dt);
                break;
            case PKind::Spark:
                p.vel.y -= 20.0f * dt;
                p.vel.x += sinf(time * 6.0f + p.phase) * 30.0f * dt;
                p.vel.x *= (1.0f - 0.6f * dt);
                p.vel.y *= (1.0f - 0.6f * dt);
                break;
            case PKind::Ember:
                p.vel.x += sinf(time * 1.3f + p.phase) * 14.0f * dt;
                break;
            case PKind::Firefly:
                fireflyCount++;
                p.vel.x += cosf(time * 0.9f + p.phase) * 18.0f * dt;
                p.vel.y += sinf(time * 1.1f + p.phase * 1.7f) * 18.0f * dt;
                p.vel.x *= (1.0f - 0.8f * dt);
                p.vel.y *= (1.0f - 0.8f * dt);
                break;
            case PKind::Leaf:
                p.vel.x += sinf(time * 1.7f + p.phase) * 35.0f * dt;
                p.rot += p.rotVel * dt;
                if (p.pos.y > VH + 30.0f || p.pos.x > VW + 40.0f) p.alive = false;
                break;
            case PKind::Smoke:
                p.vel.x += 3.0f * dt;
                p.vel.y *= (1.0f - 0.15f * dt);
                break;
            case PKind::Twinkle:
                break;
            }
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
        }
    }

    void Glow(float cx, float cy, float w, float h, Color c, float rot = 0.0f) {
        DrawTexturePro(glow, { 0, 0, (float)glow.width, (float)glow.height },
                       { cx, cy, w, h }, { w * 0.5f, h * 0.5f }, rot, c);
    }

    void Star(float x, float y, float size, Color c) {
        Glow(x, y, size * 2.4f, size * 2.4f, A(c, 0.55f));
        Glow(x, y, size * 3.6f, size * 0.34f, c);
        Glow(x, y, size * 0.34f, size * 3.6f, c);
        Glow(x, y, size * 2.0f, size * 0.22f, A(c, 0.7f), 45.0f);
        Glow(x, y, size * 2.0f, size * 0.22f, A(c, 0.7f), -45.0f);
    }

    static float Env(float t) {
        return Clampf(t / 0.15f, 0.0f, 1.0f) * Clampf((1.0f - t) / 0.25f, 0.0f, 1.0f);
    }

    void DrawParticles(unsigned char layer) {
        for (auto& p : pool) {
            if (!p.alive || p.layer != layer) continue;
            float t = 1.0f - p.life / p.maxLife;
            if (p.kind == PKind::Smoke) {
                float a = Clampf(t / 0.15f, 0.0f, 1.0f) * (1.0f - t) * 0.30f;
                float sz = Lerpf(14.0f, 70.0f, t);
                Glow(p.pos.x, p.pos.y, sz, sz, Col(205, 205, 210, a * 255.0f));
            }
        }
        for (auto& p : pool) {
            if (!p.alive || p.layer != layer || p.kind != PKind::Leaf) continue;
            float flip = fabsf(cosf(p.phase + time * 3.2f));
            float w = p.size * 2.0f;
            float h = p.size * (0.35f + 0.65f * flip);
            DrawTexturePro(leaf, { 0, 0, 32, 16 }, { p.pos.x, p.pos.y, w, h }, { w * 0.5f, h * 0.5f }, p.rot, p.color);
        }

        BeginBlendMode(BLEND_ADDITIVE);

        for (auto& p : pool) {
            if (!p.alive || p.layer != layer) continue;
            float t = 1.0f - p.life / p.maxLife;
            if (p.kind == PKind::Ember) {
                float fl = 0.7f + 0.3f * sinf(time * 11.0f + p.phase * 3.0f);
                float a = Env(t) * fl;
                Glow(p.pos.x, p.pos.y, p.size * 3.2f, p.size * 3.2f, A(p.color, a * 0.55f));
                Glow(p.pos.x, p.pos.y, p.size * 1.2f, p.size * 1.2f, Col(255, 235, 180, a * 255.0f));
            } else if (p.kind == PKind::Firefly) {
                float pulse = 0.45f + 0.55f * sinf(time * 3.0f + p.phase);
                float a = Env(t) * pulse;
                Glow(p.pos.x, p.pos.y, p.size, p.size, A(p.color, a * 0.60f));
                Glow(p.pos.x, p.pos.y, p.size * 0.35f, p.size * 0.35f, Col(255, 255, 230, a * 255.0f));
            }
        }

        for (auto& p : pool) {
            if (!p.alive || p.layer != layer || p.kind != PKind::Twinkle) continue;
            float t = 1.0f - p.life / p.maxLife;
            float a = sinf(PI * t);
            Star(p.pos.x, p.pos.y, p.size * (0.5f + 0.5f * a), A(p.color, a));
        }

        for (auto& p : pool) {
            if (!p.alive || p.layer != layer || p.kind != PKind::Spark) continue;
            float t = 1.0f - p.life / p.maxLife;
            float fl = 0.65f + 0.35f * sinf(time * 25.0f + p.phase * 5.0f);
            float a = (1.0f - t) * fl;
            Glow(p.pos.x, p.pos.y, p.size * 3.0f, p.size * 3.0f, A(p.color, a * 0.6f));
            Glow(p.pos.x, p.pos.y, p.size * 1.0f, p.size * 1.0f, Col(255, 245, 210, a * 255.0f));
        }

        for (auto& p : pool) {
            if (!p.alive || p.layer != layer || p.kind != PKind::Flame) continue;
            float t = 1.0f - p.life / p.maxLife;
            float w = p.size * (1.0f - 0.55f * t);
            float a = Clampf(t * 8.0f, 0.0f, 1.0f) * (1.0f - t * t) * p.power;
            Glow(p.pos.x, p.pos.y - w * 0.4f, w * 2.4f, w * 2.4f, Col(255, 120, 25, a * 105.0f));
        }

        for (auto& p : pool) {
            if (!p.alive || p.layer != layer || p.kind != PKind::Flame) continue;
            float t = 1.0f - p.life / p.maxLife;
            float flick = 0.85f + 0.15f * sinf(time * 40.0f + p.phase * 7.0f);
            float w = p.size * (1.0f - 0.55f * t) * flick;
            float h = w * 1.9f;
            float a = Clampf(t * 8.0f, 0.0f, 1.0f) * (1.0f - t * t) * p.power * 1.0f;
            Color c = FlameColor(t * 0.9f);
            c.a = U8(a * 255.0f);
            float rot = atan2f(p.vel.x, -p.vel.y) * RAD2DEG * 0.8f;
            DrawTexturePro(flame, { 0, 0, (float)flame.width, (float)flame.height },
                           { p.pos.x, p.pos.y, w, h }, { w * 0.5f, h * 0.9f }, rot, c);
        }

        EndBlendMode();
    }

    void DrawButtonGlow(const Rectangle& r, float k, float sc) {
        if (k <= 0.01f) return;
        float pulse = 0.84f + 0.16f * sinf(time * 7.3f) + 0.05f * sinf(time * 17.1f);
        float cx = r.x + r.width * 0.5f;
        float cy = r.y + r.height * 0.5f;

        BeginBlendMode(BLEND_ADDITIVE);
        Glow(cx, cy, r.width * 1.55f, r.height * 2.4f * sc, Col(255, 105, 20, 70.0f * k * pulse));
        Glow(cx, cy, r.width * 1.20f, r.height * 1.55f, Col(255, 150, 40, 120.0f * k * pulse));
        Glow(cx, r.y + r.height * 0.88f, r.width * 0.80f, r.height * 0.80f, Col(255, 130, 30, 120.0f * k * pulse));
        FillRounded(r, r.height * 0.40f, Col(255, 150, 50, 14.0f * k * pulse));
        EndBlendMode();
    }

    void DrawSwordGlint() {
        if (!glintActive) return;
        Vector2 g = GlintPos();
        float s = sinf(PI * glintT);
        BeginBlendMode(BLEND_ADDITIVE);
        Star(g.x, g.y, 20.0f * s + 4.0f, Col(255, 252, 230, 255.0f * s));
        EndBlendMode();
    }

    Rectangle RowRect(int i) const {
        float py = panel.y + (1.0f - Smooth(settingsT)) * 28.0f;
        if (i >= R_RESET) return ButtonRect(i);
        return { panel.x + 36, py + 118 + (float)i * 50.0f, panel.width - 72, 44 };
    }

    Rectangle ButtonRect(int row) const {
        float py = panel.y + (1.0f - Smooth(settingsT)) * 28.0f;
        float by = py + panel.height - 102.0f;
        if (row == R_RESET) return { panel.x + 70, by, 280, 58 };
        return { panel.x + panel.width - 70 - 280, by, 280, 58 };
    }

    Rectangle TrackRect(int i) const {
        Rectangle r = RowRect(i);
        return { r.x + 300, r.y + r.height * 0.5f - 6.0f, 260, 12 };
    }

    float GetNorm(int row) const {
        switch (row) {
        case R_MASTER: return S->master;
        case R_MUSIC: return S->music;
        case R_SFX: return S->sfx;
        default: return S->brightness - 0.5f;
        }
    }

    void SetNorm(int row, float v) {
        v = Clampf(v, 0.0f, 1.0f);
        switch (row) {
        case R_MASTER: S->master = v; break;
        case R_MUSIC: S->music = v; break;
        case R_SFX: S->sfx = v; break;
        default: S->brightness = 0.5f + v; break;
        }
        ApplyAll(false);
    }

    void UpdateMain() {
        for (int i = 0; i < 3; i++) {
            if (CheckCollisionPointRec(mouse, btn[i].r)) {
                hovering = true;
                if (mouseWasMoved && sel != i) { sel = i; PlayHover(); }
            }
        }

        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
            sel = (sel + 1) % 3;
            PlayHover();
        }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
            sel = (sel + 2) % 3;
            PlayHover();
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < 3; i++) {
                if (CheckCollisionPointRec(mouse, btn[i].r)) { sel = i; Activate(i); break; }
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
            Activate(sel);
        }
    }

    void Activate(int i) {
        PlayClick();
        Burst({ btn[i].r.x + btn[i].r.width * 0.5f, btn[i].r.y + btn[i].r.height * 0.5f }, 70, 0);
        if (i == 0) { pending = MenuAction::StartGame; fadeTarget = 1.0f; }
        else if (i == 1) OpenSettings();
        else { pending = MenuAction::Quit; fadeTarget = 1.0f; }
    }

    void CloseSettings() {
        SaveSettings(*S);
        screen = Screen::Main;
        dragging = -1;
    }

    void ResetDefaults() {
        GameSettings d;
        *S = d;
        ApplyAll(true);
        SaveSettings(*S);
    }

    void Adjust(int row, int dir) {
        if (row <= R_BRIGHT) {
            SetNorm(row, GetNorm(row) + 0.05f * (float)dir);
        } else if (row == R_FULL) {
            S->fullscreen = !S->fullscreen; ApplyWindow(); PlayClick();
        } else if (row == R_VSYNC) {
            S->vsync = !S->vsync; ApplyWindow(); PlayClick();
        } else if (row == R_FPS) {
            S->showFps = !S->showFps; PlayClick();
        } else if (row == R_PARTICLES) {
            S->particleQuality = (S->particleQuality + dir + 3) % 3; PlayClick();
        } else if (row == R_RESET || row == R_BACK) {
            setSel = (row == R_RESET) ? R_BACK : R_RESET; PlayHover();
        }
    }

    void ActivateRow(int row) {
        if (row == R_RESET) { PlayClick(); ResetDefaults(); }
        else if (row == R_BACK) { PlayClick(); CloseSettings(); }
        else if (row >= R_FULL) Adjust(row, 1);
    }

    void UpdateSettings(float) {
        Vector2 m = mouse;

        for (int i = 0; i < R_COUNT; i++) {
            Rectangle r = RowRect(i);
            if (CheckCollisionPointRec(m, r)) {
                hovering = true;
                if (mouseWasMoved && setSel != i && dragging < 0) { setSel = i; PlayHover(); }
            }
        }

        if (IsKeyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
            PlayClick();
            CloseSettings();
            return;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
            setSel = (setSel + 1) % R_COUNT; PlayHover();
        }
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
            setSel = (setSel + R_COUNT - 1) % R_COUNT; PlayHover();
        }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT) || IsKeyPressed(KEY_A) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) Adjust(setSel, -1);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT) || IsKeyPressed(KEY_D) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) Adjust(setSel, 1);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) ActivateRow(setSel);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < R_COUNT; i++) {
                Rectangle r = RowRect(i);
                if (!CheckCollisionPointRec(m, r)) continue;
                setSel = i;
                if (i <= R_BRIGHT) {
                    Rectangle t = TrackRect(i);
                    if (m.x >= t.x - 14 && m.x <= t.x + t.width + 14) dragging = i;
                } else if (i == R_PARTICLES) {
                    Rectangle t = TrackRect(i);
                    Adjust(i, m.x < t.x + t.width * 0.5f ? -1 : 1);
                } else {
                    ActivateRow(i);
                }
                break;
            }
        }

        if (dragging >= 0) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Rectangle t = TrackRect(dragging);
                SetNorm(dragging, (m.x - t.x) / t.width);
            } else {
                dragging = -1;
                SaveSettings(*S);
                PlayClick();
            }
        }
    }

    void Txt(const char* s, float x, float y, float size, Color c, int align = 0, float shadow = 2.0f) {
        float sp = size * 0.04f;
        Vector2 m = MeasureTextEx(font, s, size, sp);
        if (align == 1) x -= m.x * 0.5f;
        else if (align == 2) x -= m.x;
        if (shadow > 0.0f) DrawTextEx(font, s, { x + shadow, y + shadow }, size, sp, Col(10, 6, 4, c.a * 0.8f));
        DrawTextEx(font, s, { x, y }, size, sp, c);
    }

    void BakePanel() {
        float W = panel.width, H = panel.height;
        BeginTextureMode(panelRT);
        ClearBackground(Col(0, 0, 0, 0));
        DrawRectangleGradientV(0, 0, (int)W, (int)H, Col(76, 49, 33), Col(46, 29, 19));

        for (int i = 0; i * 56 < (int)H; i++) {
            float y = (float)(i * 56);
            for (int g = 0; g < 7; g++) {
                float hx = Hash(i * 7 + g) * W;
                float hy = y + 6.0f + Hash(i * 13 + g + 100) * 44.0f;
                float len = 60.0f + Hash(i * 5 + g + 50) * 220.0f;
                DrawLineEx({ hx, hy }, { hx + len, hy + Hash(i * 3 + g + 7) * 3.0f }, 1.5f, Col(20, 12, 8, 38));
            }
            DrawRectangle(0, (int)y, (int)W, 2, Col(22, 14, 9, 210));
            DrawRectangle(0, (int)y + 2, (int)W, 1, Col(130, 92, 58, 70));
            DrawRectangle((int)(Hash(i + 31) * (W - 40) + 20), (int)y, 2, 56, Col(22, 14, 9, 170));
        }

        DrawRectangleLinesEx({ 0, 0, W, H }, 10, Col(26, 17, 12));
        DrawRectangleLinesEx({ 5, 5, W - 10, H - 10 }, 2, Col(120, 84, 40));
        DrawRectangleLinesEx({ 10, 10, W - 20, H - 20 }, 3, Col(224, 172, 74));
        DrawRectangleLinesEx({ 14, 14, W - 28, H - 28 }, 1, Col(255, 226, 150, 130));

        float sx[4] = { 24, W - 24, 24, W - 24 };
        float sy[4] = { 24, 24, H - 24, H - 24 };
        for (int i = 0; i < 4; i++) {
            DrawCircle((int)sx[i], (int)sy[i], 9, Col(40, 26, 16));
            DrawCircle((int)sx[i], (int)sy[i], 7, Col(222, 168, 70));
            DrawCircle((int)sx[i] - 2, (int)sy[i] - 2, 3, Col(255, 236, 170));
        }

        const char* title = "SETTINGS";
        float tsz = 56.0f;
        float tx = W * 0.5f;
        float ty = 34.0f;
        Txt(title, tx + 3, ty + 4, tsz, Col(0, 0, 0, 170), 1, 0);
        Txt(title, tx, ty, tsz, Col(246, 206, 112), 1, 2);

        float ly = 108.0f;
        DrawRectangle(60, (int)ly, (int)W / 2 - 90, 2, Col(224, 172, 74, 200));
        DrawRectangle((int)W / 2 + 30, (int)ly, (int)W / 2 - 90, 2, Col(224, 172, 74, 200));
        DrawPoly({ W * 0.5f, ly + 1 }, 4, 9, 0, Col(246, 206, 112));
        EndTextureMode();
    }

    void DrawWoodButton(Rectangle r, const char* label, float alpha) {
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, Col(124, 80, 46, 255 * alpha), Col(78, 48, 28, 255 * alpha));
        OutlineRounded(r, 6, 3, Col(226, 176, 80, 255 * alpha));
        Rectangle in = { r.x + 6, r.y + 6, r.width - 12, r.height - 12 };
        OutlineRounded(in, 4, 1, Col(255, 226, 150, 110 * alpha));
        Txt(label, r.x + r.width * 0.5f, r.y + r.height * 0.5f - 15.0f, 30, Col(255, 226, 150, 255 * alpha), 1, 2);
    }

    void DrawSettings(float e) {
        float py = (1.0f - e) * 28.0f;
        DrawTexturePro(panelRT.texture, { 0, 0, panel.width, -panel.height },
                       { panel.x, panel.y + py, panel.width, panel.height }, { 0, 0 }, 0.0f, Col(255, 255, 255, 255 * e));

        static const char* labels[8] = { "Master Volume", "Music Volume", "Effects Volume", "Brightness",
                                         "Fullscreen", "VSync", "Show FPS", "Particles" };
        static const char* quality[3] = { "Low", "Medium", "High" };
        float pulse = 0.82f + 0.18f * sinf(time * 7.3f);

        for (int i = 0; i < 8; i++) {
            Rectangle r = RowRect(i);
            float k = rowK[i];
            if (k > 0.01f) {
                BeginBlendMode(BLEND_ADDITIVE);
                FillRounded(r, 8, Col(255, 130, 30, 34 * k * pulse * e));
                OutlineRounded(r, 8, 2, Col(255, 170, 60, 150 * k * pulse * e));
                OutlineRounded({ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, 10, 3, Col(255, 120, 30, 60 * k * pulse * e));
                EndBlendMode();
            }

            Color lc = Mix(Col(226, 200, 160), Col(255, 236, 180), k);
            Txt(labels[i], r.x + 24, r.y + r.height * 0.5f - 15.0f, 28, A(lc, e), 0, 2);

            Rectangle t = TrackRect(i);
            if (i <= R_BRIGHT) {
                float n = GetNorm(i);
                DrawRectangleRounded(t, 1.0f, 8, Col(24, 15, 10, 255 * e));
                DrawRectangleRounded({ t.x, t.y, std::max(t.width * n, 1.0f), t.height }, 1.0f, 8, Col(230, 160, 55, 255 * e));
                DrawRectangleRounded({ t.x, t.y, std::max(t.width * n, 1.0f), t.height * 0.45f }, 1.0f, 8, Col(255, 214, 120, 150 * e));
                OutlineRounded({ t.x - 1, t.y - 1, t.width + 2, t.height + 2 }, 6, 1, Col(120, 84, 40, 255 * e));

                float kx = t.x + t.width * n;
                float ky = t.y + t.height * 0.5f;
                if (k > 0.01f) {
                    BeginBlendMode(BLEND_ADDITIVE);
                    Glow(kx, ky, 56, 56, Col(255, 130, 30, 170 * k * pulse * e));
                    EndBlendMode();
                }
                DrawCircle((int)kx, (int)ky, 14, Col(30, 19, 12, 255 * e));
                DrawCircle((int)kx, (int)ky, 12, Col(228, 176, 78, 255 * e));
                DrawCircle((int)kx - 3, (int)ky - 3, 4, Col(255, 238, 180, 230 * e));

                char buf[16];
                int pct = (i == R_BRIGHT) ? (int)lroundf(S->brightness * 100.0f) : (int)lroundf(n * 100.0f);
                snprintf(buf, sizeof(buf), "%d%%", pct);
                Txt(buf, r.x + r.width - 24, r.y + r.height * 0.5f - 14.0f, 26, Col(255, 226, 150, 255 * e), 2, 2);
            } else if (i == R_PARTICLES) {
                float cx = t.x + t.width * 0.5f;
                Txt(quality[S->particleQuality], cx, r.y + r.height * 0.5f - 14.0f, 28, Col(255, 226, 150, 255 * e), 1, 2);
                Txt("<", t.x + 30, r.y + r.height * 0.5f - 14.0f, 28, Col(224, 172, 74, 255 * e), 0, 2);
                Txt(">", t.x + t.width - 44, r.y + r.height * 0.5f - 14.0f, 28, Col(224, 172, 74, 255 * e), 0, 2);
            } else {
                bool on = (i == R_FULL) ? S->fullscreen : (i == R_VSYNC ? S->vsync : S->showFps);
                Rectangle sw = { t.x + t.width - 150, r.y + r.height * 0.5f - 15.0f, 66, 30 };
                DrawRectangleRounded(sw, 1.0f, 10, on ? Col(214, 150, 50, 255 * e) : Col(36, 24, 17, 255 * e));
                OutlineRounded(sw, 15, 2, Col(226, 176, 80, 255 * e));
                float kx = on ? sw.x + sw.width - 15.0f : sw.x + 15.0f;
                DrawCircle((int)kx, (int)(sw.y + 15), 10, on ? Col(255, 238, 180, 255 * e) : Col(150, 120, 90, 255 * e));
                Txt(on ? "ON" : "OFF", sw.x + sw.width + 14, r.y + r.height * 0.5f - 14.0f, 26,
                    on ? Col(255, 226, 150, 255 * e) : Col(160, 130, 100, 255 * e), 0, 2);
            }
        }

        for (int row = R_RESET; row <= R_BACK; row++) {
            Rectangle r = ButtonRect(row);
            DrawWoodButton(r, row == R_RESET ? "RESET" : "BACK", e);
            DrawButtonGlow(r, rowK[row] * e, 0.8f);
        }
        Txt("Arrows / WASD to navigate   Enter to select   Esc to go back", panel.x + panel.width * 0.5f,
            panel.y + py + panel.height - 33.0f, 17, Col(200, 170, 120, 190 * e), 1, 1);
    }
};

}
