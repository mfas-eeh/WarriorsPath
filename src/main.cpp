// Path Explorer - raylib 5.x, C++17
// * Player can ONLY walk on the orange path of the map image (auto-detected from pixel colours).
// * Paths that are hidden behind tall house roofs are detected too: the player can walk
//   behind the roof (the roof is drawn over the player, who stays visible as a faint silhouette).
// * Fog of war with adjustable vision radius, and an option to dim it or switch it off.
//
// Controls
//   WASD / Arrows  move          Shift   run            Mouse wheel  zoom
//   O / Space / Left-Click       swing attack
//   [  ]  (or - =) vision radius smaller / bigger
//   F1  show walkable mask (green = path, magenta = path behind roofs)
//   F2  minimap fog: FULL black -> DIM -> OFF
//   F3  minimap on/off           F4      HUD on/off     M  mute/unmute music
//   F5 / F6 (or 9 / 0)  BGM volume down / up (or click in HUD)

#include "raylib.h"
#include "MainMenu.hpp"
#include "shop.h"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class AppState { Menu, Logo, Intro, Game };

static inline float v2Dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Level 2 Market Store (The Alchemist's Study) locations
static const Vector2 LEVEL2_STORE_LEAVING_POINT = { 385.0f, 655.0f }; // Leaving point outside market in Level 2
static const Vector2 LEVEL2_STORE_ENTRANCE      = { 385.0f, 638.0f }; // Entrance doorway to market in Level 2

// Floor 4 (Pentagram Arena) portal, at the eastern edge of the Cyclops arena on Map 2
static const Vector2 FLOOR4_PORTAL = { 1440.0f, 585.0f };
static const Vector2 FLOOR4_SPAWN  = { 512.0f, 545.0f };

// Market Store Interior walkable bounds (store1.jpg & store2.jpg: 1024x1024)
static inline bool canStandInStore(Vector2 p) {
    if (p.x < 460.0f || p.x > 710.0f) return false;
    if (p.y < 490.0f || p.y > 940.0f) return false;
    // Avoid barrel/clutter near the left wall entrance
    if (p.x < 485.0f && p.y > 730.0f && p.y < 820.0f) return false;
    return true;
}

// ------------------------------------------------------------------
// Tuning
// ------------------------------------------------------------------
struct PathColorRule {
    // Orange/dirt path in HSV (hue in degrees 0..360, sat/val 0..1).
    float hueMin = 12.0f;
    float hueMax = 44.0f;
    float satMin = 0.26f;
    float valMin = 0.32f;
};

struct RoofColorRule {
    // Blue house roofs.
    float hueMin = 195.0f;
    float hueMax = 255.0f;
    float satMin = 0.20f;
    float valMax = 0.85f;
};

// ---- walkable mask ----
static const int   CLOSE_RADIUS  = 8;      // bridges gaps (gate beam, stones, shaded crossing cracks)
static const int   HOLE_MAX_AREA = 1500;   // non-path holes smaller than this inside the path get filled
static const int   MIN_COMPONENT = 2500;   // keeps village roads and right forest trails

// ---- roofs that hide the path ----
static const int   ROOF_GAP      = 8;      // tolerated outline pixels between path and roof edge
static const int   ROOF_MAX_RUN  = 260;    // longest stretch of roof we will "walk behind" (map px)
static const int   ROOF_MARGIN   = 55;     // overlay area around a hidden-path spot (player head height)

// ---- gameplay ----
static const float WALK_SPEED    = 130.0f; // map pixels / second
static const float RUN_SPEED     = 210.0f;
static const float SPAWN_FX      = 0.31f;  // spawn hint (fraction of map size): village gate
static const float SPAWN_FY      = 0.525f;
static float g_avatarScale = 1.0f;       // >1 inside the store so the player is easier to see
static const float AVATAR_HEIGHT = 50.0f;  // size of main character (map pixels, increased from 28)

// ---- fog of war ----
static const float RADIUS_START  = 190.0f; // vision radius at start (map px)
static const float RADIUS_MIN    = 40.0f;
static const float RADIUS_MAX    = 1500.0f;
static const float RADIUS_SPEED  = 260.0f; // px/second while holding [ or ]
static const int   FOG_DOWNSCALE = 4;      // fog texture = map / 4 (soft + cheap)
static const uint8_t FOG_DIM_ALPHA = 150;  // how dark the DIM mode is (0..255)

// Player feet collision box (half sizes, map pixels)
static const float FOOT_HX = 4.0f;
static const float FOOT_HY = 3.0f;

// ------------------------------------------------------------------
// Walkable mask
// ------------------------------------------------------------------
struct WalkMask {
    int w = 0, h = 0;
    std::vector<uint8_t> cell;    // 1 = player may stand here
    std::vector<uint8_t> behind;  // 1 = path hidden behind a roof (subset of cell)

    bool at(int x, int y) const {
        return x >= 0 && y >= 0 && x < w && y < h && cell[(size_t)y * w + x] != 0;
    }
    bool atF(float x, float y) const { return at((int)std::floor(x), (int)std::floor(y)); }
    bool isBehind(Vector2 p) const {
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y);
        return x >= 0 && y >= 0 && x < w && y < h && behind[(size_t)y * w + x] != 0;
    }

    // Player stands on the path only if all 4 foot corners + centre are on it.
    bool canStand(Vector2 p) const {
        return atF(p.x, p.y) &&
               atF(p.x - FOOT_HX, p.y - FOOT_HY) && atF(p.x + FOOT_HX, p.y - FOOT_HY) &&
               atF(p.x - FOOT_HX, p.y + FOOT_HY) && atF(p.x + FOOT_HX, p.y + FOOT_HY);
    }
};

static void boxPass(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                    int w, int h, int r, bool horizontal, bool dilate) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t acc = dilate ? 0 : 1;
            for (int k = -r; k <= r; ++k) {
                int xx = x + (horizontal ? k : 0);
                int yy = y + (horizontal ? 0 : k);
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                uint8_t v = in[(size_t)yy * w + xx];
                if (dilate) { if (v)  { acc = 1; break; } }
                else        { if (!v) { acc = 0; break; } }
            }
            out[(size_t)y * w + x] = acc;
        }
    }
}

static void dilateMask(std::vector<uint8_t>& m, int w, int h, int r) {
    std::vector<uint8_t> tmp(m.size());
    boxPass(m, tmp, w, h, r, true, true);
    boxPass(tmp, m, w, h, r, false, true);
}

// Morphological closing = dilate then erode. Fills small holes (stones) in the path.
static void closeMask(std::vector<uint8_t>& m, int w, int h, int r) {
    std::vector<uint8_t> tmp(m.size());
    boxPass(m, tmp, w, h, r, true, true);
    boxPass(tmp, m, w, h, r, false, true);
    boxPass(m, tmp, w, h, r, true, false);
    boxPass(tmp, m, w, h, r, false, false);
}

// Flood-fill helper: labels 4-connected components of cells equal to `value`.
// Returns label per cell (0 = other value) and fills areas[label].
static std::vector<int> labelComponents(const std::vector<uint8_t>& m, int w, int h,
                                        uint8_t value, std::vector<int>& areas) {
    std::vector<int> lab(m.size(), 0);
    areas.assign(1, 0);
    std::vector<int> q;
    for (int y0 = 0; y0 < h; ++y0) {
        for (int x0 = 0; x0 < w; ++x0) {
            size_t i0 = (size_t)y0 * w + x0;
            if (m[i0] != value || lab[i0]) continue;
            int id = (int)areas.size();
            areas.push_back(0);
            q.clear();
            q.push_back((int)i0);
            lab[i0] = id;
            for (size_t head = 0; head < q.size(); ++head) {
                int idx = q[head];
                int x = idx % w, y = idx / w;
                areas[id]++;
                const int nx[4] = { x + 1, x - 1, x, x };
                const int ny[4] = { y, y, y + 1, y - 1 };
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
                    size_t n = (size_t)ny[k] * w + nx[k];
                    if (m[n] == value && !lab[n]) { lab[n] = id; q.push_back((int)n); }
                }
            }
        }
    }
    return lab;
}

// Fill tiny non-path holes (cobblestones, pebbles) that sit inside the path.
static void fillSmallHoles(std::vector<uint8_t>& m, int w, int h, int maxArea) {
    std::vector<int> areas;
    std::vector<int> lab = labelComponents(m, w, h, 0, areas);
    for (size_t i = 0; i < m.size(); ++i)
        if (!m[i] && areas[lab[i]] < maxArea) m[i] = 1;
}

// Shrink the path by 1px so the player never touches the blurry orange/grass edge.
static void erode1(std::vector<uint8_t>& m, int w, int h) {
    std::vector<uint8_t> tmp(m.size());
    boxPass(m, tmp, w, h, 1, true, false);
    boxPass(tmp, m, w, h, 1, false, false);
}

// Keep large path pieces (+ whichever piece is nearest the spawn). Snap spawn onto the path.
static bool keepLargePieces(std::vector<uint8_t>& m, int w, int h, Vector2& spawn) {
    std::vector<int> areas;
    std::vector<int> lab = labelComponents(m, w, h, 1, areas);

    int sx = std::clamp((int)spawn.x, 0, w - 1);
    int sy = std::clamp((int)spawn.y, 0, h - 1);
    long long best = -1;
    int bx = -1, by = -1, bestLabel = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!lab[(size_t)y * w + x]) continue;
            long long d = (long long)(x - sx) * (x - sx) + (long long)(y - sy) * (y - sy);
            if (best < 0 || d < best) { best = d; bx = x; by = y; bestLabel = lab[(size_t)y * w + x]; }
        }
    }
    if (bx < 0) return false;

    for (size_t i = 0; i < m.size(); ++i)
        m[i] = (lab[i] && (areas[lab[i]] >= MIN_COMPONENT || lab[i] == bestLabel)) ? 1 : 0;
    spawn = { (float)bx, (float)by };
    return true;
}

// ------------------------------------------------------------------
// Paths hidden behind roofs
// ------------------------------------------------------------------
// Scans every row (or column). Wherever a stretch of roof sits between two walkable path pixels
// (allowing a few outline pixels in between), the path obviously continues behind the roof,
// so that whole stretch is marked in `out`.
static void bridgeLines(const std::vector<uint8_t>& occ, const std::vector<uint8_t>& walk,
                        std::vector<uint8_t>& out, int w, int h, bool horizontal) {
    const int lines = horizontal ? h : w;
    const int len   = horizontal ? w : h;
    auto idx = [&](int line, int i) -> size_t {
        return horizontal ? (size_t)line * w + i : (size_t)i * w + line;
    };
    for (int L = 0; L < lines; ++L) {
        int i = 0;
        while (i < len) {
            if (!(occ[idx(L, i)] && !walk[idx(L, i)])) { ++i; continue; }
            int s = i;
            while (i < len && occ[idx(L, i)] && !walk[idx(L, i)]) ++i;
            int e = i; // exclusive end of the roof stretch
            int lw = -1, rw = -1;
            for (int k = 1; k <= ROOF_GAP + 1 && s - k >= 0; ++k)
                if (walk[idx(L, s - k)]) { lw = s - k; break; }
            for (int k = 0; k <= ROOF_GAP && e + k < len; ++k)
                if (walk[idx(L, e + k)]) { rw = e + k; break; }
            if (lw >= 0 && rw >= 0 && rw - lw - 1 <= ROOF_MAX_RUN)
                for (int j = lw + 1; j < rw; ++j) out[idx(L, j)] = 1;
        }
    }
}

// Everything the renderer needs to put roofs back on top of the player.
struct Occlusion {
    Texture2D tex{};                 // map pixels of the roofs (rest transparent)
    std::vector<Rectangle> zones;    // areas where the player can be hidden by a roof
    std::vector<uint8_t> roofCells;  // (kept for debugging)
    bool ready = false;
};

// ------------------------------------------------------------------
// Grass areas outside town for wildlife critters
// ------------------------------------------------------------------
struct CritterGrassMask {
    int w = 0, h = 0;
    std::vector<uint8_t> grid;          // 1 = valid outside grass, 0 = town/roof/path/water/edge
    std::vector<Vector2> validCoords;   // pre-sampled valid points for fast random placement

    bool isValid(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        return grid[(size_t)y * w + x] != 0;
    }
    bool isValidF(float x, float y) const {
        return isValid((int)std::floor(x), (int)std::floor(y));
    }
    Vector2 getRandomPoint() const {
        if (validCoords.empty()) return { 0.0f, 0.0f };
        return validCoords[GetRandomValue(0, (int)validCoords.size() - 1)];
    }
    Vector2 getRandomPointNear(Vector2 p, float minDist, float maxDist) const {
        if (validCoords.empty()) return p;
        for (int attempt = 0; attempt < 16; ++attempt) {
            float angle = (float)GetRandomValue(0, 360) * (PI / 180.0f);
            float dist = (float)GetRandomValue((int)minDist, (int)maxDist);
            float nx = p.x + std::cos(angle) * dist;
            float ny = p.y + std::sin(angle) * dist;
            if (isValidF(nx, ny)) return { nx, ny };
        }
        return getRandomPoint();
    }
};

static WalkMask buildWalkMask(const Image& img, const PathColorRule& rule, const RoofColorRule& roofRule,
                              Vector2& spawn, std::vector<Color>& roofPixels,
                              std::vector<Rectangle>& zones, CritterGrassMask& critterMask) {
    WalkMask wm;
    wm.w = img.width;
    wm.h = img.height;
    wm.cell.assign((size_t)wm.w * wm.h, 0);
    wm.behind.assign((size_t)wm.w * wm.h, 0);

    std::vector<uint8_t> roof((size_t)wm.w * wm.h, 0);

    Color* px = LoadImageColors(img);
    for (size_t i = 0; i < wm.cell.size(); ++i) {
        Vector3 hsv = ColorToHSV(px[i]);
        uint8_t r = px[i].r, g = px[i].g, b = px[i].b;
        wm.cell[i] = (hsv.x >= rule.hueMin && hsv.x <= rule.hueMax &&
                      hsv.y >= rule.satMin && hsv.z >= rule.valMin &&
                      r >= b) ? 1 : 0;
        roof[i]    = (hsv.x >= roofRule.hueMin && hsv.x <= roofRule.hueMax &&
                      hsv.y >= roofRule.satMin && hsv.z <= roofRule.valMax) ? 1 : 0;
    }

    // ---- 1) orange path -> clean walkable mask ----
    closeMask(wm.cell, wm.w, wm.h, CLOSE_RADIUS);
    fillSmallHoles(wm.cell, wm.w, wm.h, HOLE_MAX_AREA);
    erode1(wm.cell, wm.w, wm.h);
    if (!keepLargePieces(wm.cell, wm.w, wm.h, spawn)) {
        TraceLog(LOG_WARNING, "No walkable pixel found near spawn - check PathColorRule!");
    }

    // ---- 2) roofs: tidy the roof mask (fill tile lines, include the dark outline) ----
    closeMask(roof, wm.w, wm.h, 2);
    dilateMask(roof, wm.w, wm.h, 3);

    // ---- 3) path continuing behind roofs (rows + columns) ----
    std::vector<uint8_t> br(roof.size(), 0);
    bridgeLines(roof, wm.cell, br, wm.w, wm.h, true);
    bridgeLines(roof, wm.cell, br, wm.w, wm.h, false);
    for (size_t i = 0; i < br.size(); ++i) {
        if (br[i] && !wm.cell[i]) { wm.behind[i] = 1; }
    }
    for (size_t i = 0; i < br.size(); ++i) if (wm.behind[i]) wm.cell[i] = 1;

    // ---- 4) zones (bounding boxes of each hidden-path spot) + roof overlay pixels ----
    std::vector<int> areas;
    std::vector<int> lab = labelComponents(wm.behind, wm.w, wm.h, 1, areas);
    std::vector<int> minx(areas.size(), wm.w), miny(areas.size(), wm.h), maxx(areas.size(), -1), maxy(areas.size(), -1);
    for (int y = 0; y < wm.h; ++y) {
        for (int x = 0; x < wm.w; ++x) {
            int l = lab[(size_t)y * wm.w + x];
            if (!l) continue;
            minx[l] = std::min(minx[l], x); maxx[l] = std::max(maxx[l], x);
            miny[l] = std::min(miny[l], y); maxy[l] = std::max(maxy[l], y);
        }
    }
    zones.clear();
    for (size_t l = 1; l < areas.size(); ++l) {
        if (areas[l] < 40) continue; // ignore specks
        int x0 = std::max(0, minx[l] - ROOF_MARGIN), y0 = std::max(0, miny[l] - ROOF_MARGIN);
        int x1 = std::min(wm.w - 1, maxx[l] + ROOF_MARGIN), y1 = std::min(wm.h - 1, maxy[l] + ROOF_MARGIN);
        zones.push_back(Rectangle{ (float)x0, (float)y0, (float)(x1 - x0 + 1), (float)(y1 - y0 + 1) });
    }

    roofPixels.assign((size_t)wm.w * wm.h, Color{ 0, 0, 0, 0 });
    for (const Rectangle& z : zones) {
        for (int y = (int)z.y; y < (int)(z.y + z.height); ++y)
            for (int x = (int)z.x; x < (int)(z.x + z.width); ++x) {
                size_t i = (size_t)y * wm.w + x;
                if (roof[i]) { roofPixels[i] = px[i]; roofPixels[i].a = 255; }
            }
    }

    // ---- 5) outside-town grass mask for wildlife critters ----
    critterMask.w = wm.w;
    critterMask.h = wm.h;
    critterMask.grid.assign((size_t)wm.w * wm.h, 0);

    std::vector<uint8_t> rawOutside((size_t)wm.w * wm.h, 0);
    for (int y = 0; y < wm.h; ++y) {
        for (int x = 0; x < wm.w; ++x) {
            size_t i = (size_t)y * wm.w + x;
            // Map borders margin
            if (x < 45 || x >= wm.w - 45 || y < 45 || y >= wm.h - 45) continue;
            // Not on walkable path or roofs
            if (wm.cell[i] || roof[i]) continue;
            // Town exclusion bounding box (the town cluster is located at x: 220..1340, y: 0..840)
            if (x >= 220 && x <= 1340 && y >= 0 && y <= 840) continue;
            // Isolated top-right cabin exclusion
            int dx_tr = x - 2350, dy_tr = y - 150;
            if (dx_tr * dx_tr + dy_tr * dy_tr <= 160 * 160) continue;

            Vector3 hsv = ColorToHSV(px[i]);
            // Green grass color condition: hue in [52..145] deg, sat >= 0.24, val in [0.15..0.85]
            if (hsv.x >= 52.0f && hsv.x <= 145.0f && hsv.y >= 0.24f && hsv.z >= 0.15f && hsv.z <= 0.85f) {
                rawOutside[i] = 1;
            }
        }
    }

    // Erode the outside grass mask by 6px so critters don't walk on edges of roads or town borders
    std::vector<uint8_t> erodedOutside(rawOutside.size(), 0);
    boxPass(rawOutside, erodedOutside, wm.w, wm.h, 6, true, false);
    boxPass(erodedOutside, critterMask.grid, wm.w, wm.h, 6, false, false);

    // Pre-sample valid grass coordinates for instant O(1) random placement
    critterMask.validCoords.clear();
    for (int y = 50; y < wm.h - 50; y += 8) {
        for (int x = 50; x < wm.w - 50; x += 8) {
            if (critterMask.isValid(x, y)) {
                critterMask.validCoords.push_back({ (float)x, (float)y });
            }
        }
    }

    UnloadImageColors(px);
    return wm;
}

// ------------------------------------------------------------------
// Fog of war (CPU part: soft, low-res alpha map stretched over the map)
// ------------------------------------------------------------------
struct FogMap {
    int w = 0, h = 0;
    std::vector<Color> px;   // black, alpha 255 = hidden, 0 = explored

    void init(int mapW, int mapH) {
        w = (mapW + FOG_DOWNSCALE - 1) / FOG_DOWNSCALE;
        h = (mapH + FOG_DOWNSCALE - 1) / FOG_DOWNSCALE;
        px.assign((size_t)w * h, Color{ 0, 0, 0, 255 });
    }

    // Reveals a soft circle. Returns the changed rectangle in fog pixels (inclusive);
    // false if the circle is completely outside the map.
    bool reveal(Vector2 p, float radius, int& x0, int& y0, int& x1, int& y1) {
        float cx = p.x / FOG_DOWNSCALE, cy = p.y / FOG_DOWNSCALE;
        float R = std::max(1.0f, radius / FOG_DOWNSCALE);
        x0 = std::max(0, (int)std::floor(cx - R));      x1 = std::min(w - 1, (int)std::ceil(cx + R));
        y0 = std::max(0, (int)std::floor(cy - R));      y1 = std::min(h - 1, (int)std::ceil(cy + R));
        if (x0 > x1 || y0 > y1) return false;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
                if (d > R) continue;
                float t = std::clamp((d - 0.55f * R) / (0.45f * R), 0.0f, 1.0f);
                t = t * t * (3.0f - 2.0f * t);
                uint8_t a = (uint8_t)(255.0f * t);
                Color& c = px[(size_t)y * w + x];
                if (a < c.a) c.a = a;
            }
        }
        return true;
    }
};

// ------------------------------------------------------------------
// ------------------------------------------------------------------
// Player (spritesheet avatar with procedural fallback)
// ------------------------------------------------------------------
static Color withAlpha(Color c, float k) { c.a = (uint8_t)std::clamp(c.a * k, 0.0f, 255.0f); return c; }

enum Direction {
    DIR_DOWN = 0,
    DIR_UP = 1,
    DIR_LEFT = 2,
    DIR_RIGHT = 3
};

enum PlayerLoadout {
    LOADOUT_BASE = 0,
    LOADOUT_SHADOW = 1,
    LOADOUT_FIRE = 2,
    LOADOUT_TIME = 3
};

struct AvatarRenderer {
    // Base textures
    Texture2D downTex{};
    Texture2D upTex{};
    Texture2D sideTex{};
    bool ready = false;

    // Time player sprites
    Texture2D timeUpDownTex{};
    Texture2D timeSideTex{};
    bool timeReady = false;

    // Sword swing textures
    Texture2D swingBaseTex{};
    Texture2D swingShadowTex{};
    Texture2D swingFireTex{};
    Texture2D swingTimeTex{};

    bool swingBaseReady = false;
    bool swingShadowReady = false;
    bool swingFireReady = false;
    bool swingTimeReady = false;

    // down: 7 frames (0 = idle, 1..6 = walk cycle)
    const Rectangle downFrames[7] = {
        {   26.0f,  50.0f, 168.0f, 287.0f },
        {  253.0f,  56.0f, 165.0f, 290.0f },
        {  474.0f,  61.0f, 162.0f, 282.0f },
        {  695.0f,  58.0f, 164.0f, 285.0f },
        {  911.0f,  54.0f, 167.0f, 286.0f },
        { 1131.0f,  57.0f, 162.0f, 294.0f },
        { 1365.0f,  53.0f, 162.0f, 291.0f }
    };

    // up: 7 frames (0 = idle, 1..6 = walk cycle)
    const Rectangle upFrames[7] = {
        {   38.0f,  94.0f, 162.0f, 300.0f },
        {  267.0f,  99.0f, 158.0f, 297.0f },
        {  483.0f,  98.0f, 156.0f, 302.0f },
        {  701.0f,  94.0f, 161.0f, 302.0f },
        {  920.0f,  89.0f, 157.0f, 306.0f },
        { 1138.0f,  89.0f, 163.0f, 310.0f },
        { 1378.0f,  96.0f, 159.0f, 300.0f }
    };

    // left_right: 6 frames (0 = idle, 1..5 = walk cycle; sprite naturally faces left)
    const Rectangle sideFrames[6] = {
        {   94.0f, 125.0f, 145.0f, 319.0f },
        {  355.0f, 115.0f, 163.0f, 315.0f },
        {  619.0f, 114.0f, 166.0f, 331.0f },
        {  881.0f, 113.0f, 157.0f, 328.0f },
        { 1146.0f, 114.0f, 161.0f, 333.0f },
        { 1408.0f, 117.0f, 154.0f, 324.0f }
    };

    // Time player: Row 0 Down (4 frames: 0 = idle, 1..3 = walk)
    const Rectangle timeDownFrames[4] = {
        {   65.0f,  48.0f, 521.0f, 678.0f },
        {  775.0f,  48.0f, 517.0f, 676.0f },
        { 1479.0f,  48.0f, 517.0f, 677.0f },
        { 2198.0f,  48.0f, 525.0f, 680.0f }
    };

    // Time player: Row 1 Up (4 frames: 0 = idle, 1..3 = walk)
    const Rectangle timeUpFrames[4] = {
        {  223.0f, 761.0f, 491.0f, 682.0f },
        {  943.0f, 762.0f, 475.0f, 673.0f },
        { 1656.0f, 762.0f, 477.0f, 681.0f },
        { 2371.0f, 763.0f, 461.0f, 679.0f }
    };

    // Time player: Side (4 frames: 0 = idle, 1..3 = walk; naturally faces left)
    const Rectangle timeSideFrames[4] = {
        {   88.0f, 247.0f, 682.0f, 853.0f },
        { 1009.0f, 248.0f, 624.0f, 856.0f },
        { 1809.0f, 248.0f, 638.0f, 856.0f },
        { 2571.0f, 248.0f, 627.0f, 852.0f }
    };

    static Texture2D loadTrilinear(const char* path) {
        if (!path || !FileExists(path)) return Texture2D{};
        Image img = LoadImage(path);
        if (img.width == 0 || img.height == 0) return Texture2D{};
        Texture2D tex = LoadTextureFromImage(img);
        GenTextureMipmaps(&tex);
        SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
        UnloadImage(img);
        return tex;
    }

    static Texture2D loadSwingTexture(const char* path) {
        if (!path || !FileExists(path)) return Texture2D{};
        Image img = LoadImage(path);
        if (img.width == 0 || img.height == 0) return Texture2D{};
        // If image lacks alpha or was saved as RGB with white background, key out background
        if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            Color* px = (Color*)img.data;
            int total = img.width * img.height;
            for (int i = 0; i < total; ++i) {
                if (px[i].r >= 235 && px[i].g >= 235 && px[i].b >= 235) {
                    px[i].a = 0;
                }
            }
        }
        Texture2D tex = LoadTextureFromImage(img);
        GenTextureMipmaps(&tex);
        SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
        UnloadImage(img);
        return tex;
    }

    bool load(const char* downPath, const char* upPath, const char* sidePath,
              const char* swingBasePath, const char* swingShadowPath,
              const char* swingFirePath, const char* swingTimePath,
              const char* timeUpDownPath, const char* timeSidePath) {
        if (!downPath || !upPath || !sidePath) return false;
        if (!FileExists(downPath) || !FileExists(upPath) || !FileExists(sidePath)) return false;

        downTex = loadTrilinear(downPath);
        upTex   = loadTrilinear(upPath);
        sideTex = loadTrilinear(sidePath);
        ready = (downTex.id != 0 && upTex.id != 0 && sideTex.id != 0);

        // Time player sprites
        if (timeUpDownPath && timeSidePath && FileExists(timeUpDownPath) && FileExists(timeSidePath)) {
            timeUpDownTex = loadTrilinear(timeUpDownPath);
            timeSideTex   = loadTrilinear(timeSidePath);
            timeReady     = (timeUpDownTex.id != 0 && timeSideTex.id != 0);
        }

        // Sword swing textures
        if (swingBasePath) {
            swingBaseTex = loadSwingTexture(swingBasePath);
            swingBaseReady = (swingBaseTex.id != 0);
        }
        if (swingShadowPath) {
            swingShadowTex = loadSwingTexture(swingShadowPath);
            swingShadowReady = (swingShadowTex.id != 0);
        }
        if (swingFirePath) {
            swingFireTex = loadSwingTexture(swingFirePath);
            swingFireReady = (swingFireTex.id != 0);
        }
        if (swingTimePath) {
            swingTimeTex = loadSwingTexture(swingTimePath);
            swingTimeReady = (swingTimeTex.id != 0);
        }

        return ready;
    }

    void unload() {
        if (ready) {
            UnloadTexture(downTex);
            UnloadTexture(upTex);
            UnloadTexture(sideTex);
            ready = false;
        }
        if (timeReady) {
            UnloadTexture(timeUpDownTex);
            UnloadTexture(timeSideTex);
            timeReady = false;
        }
        if (swingBaseReady)   { UnloadTexture(swingBaseTex);   swingBaseReady = false; }
        if (swingShadowReady) { UnloadTexture(swingShadowTex); swingShadowReady = false; }
        if (swingFireReady)   { UnloadTexture(swingFireTex);   swingFireReady = false; }
        if (swingTimeReady)   { UnloadTexture(swingTimeTex);   swingTimeReady = false; }
    }

    void draw(Vector2 p, bool moving, float animT, Direction dir,
              bool isSwinging, float swingProgress, PlayerLoadout loadout = LOADOUT_BASE,
              float alpha = 1.0f) const {
        float timeVal = (float)GetTime();
        float pulse = (std::sin(timeVal * 4.0f) + 1.0f) * 0.5f;

        // ----------------------------------------------------
        // GROUND AURA BY LOADOUT
        // ----------------------------------------------------
        if (loadout == LOADOUT_BASE) {
            // Standard adventurer shadow
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 11.0f, 4.5f, withAlpha(Color{ 0, 0, 0, 110 }, alpha));
        } else if (loadout == LOADOUT_SHADOW) {
            // Shadow phantom aura under feet
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 13.0f + 2.0f * pulse, 5.5f + pulse,
                        withAlpha(Color{ 35, 10, 50, 140 }, alpha));
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 8.5f, 3.5f,
                        withAlpha(Color{ 140, 40, 200, 90 }, alpha));
        } else if (loadout == LOADOUT_FIRE) {
            // Blazing embers glow under feet
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 13.0f + 2.5f * pulse, 5.5f + pulse,
                        withAlpha(Color{ 200, 60, 10, 130 }, alpha));
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 8.0f, 3.0f,
                        withAlpha(Color{ 255, 170, 30, 120 }, alpha));
        } else if (loadout == LOADOUT_TIME) {
            // Chrono dial / temporal ring under feet
            DrawEllipse((int)std::round(p.x), (int)std::round(p.y), 11.0f, 4.5f, withAlpha(Color{ 0, 0, 0, 110 }, alpha));
            DrawCircleLines((int)std::round(p.x), (int)std::round(p.y), 12.0f + 1.5f * pulse,
                            withAlpha(Color{ 60, 220, 255, 140 }, alpha));
            DrawCircleLines((int)std::round(p.x), (int)std::round(p.y), 15.5f,
                            withAlpha(Color{ 250, 215, 90, 90 }, alpha));
        }

        // ----------------------------------------------------
        // SWINGING STATE: loadout-specific swing texture
        // ----------------------------------------------------
        Texture2D activeSwingTex = swingBaseTex;
        if (loadout == LOADOUT_SHADOW && swingShadowReady) activeSwingTex = swingShadowTex;
        else if (loadout == LOADOUT_FIRE && swingFireReady) activeSwingTex = swingFireTex;
        else if (loadout == LOADOUT_TIME && swingTimeReady) activeSwingTex = swingTimeTex;
        else if (swingBaseReady) activeSwingTex = swingBaseTex;

        if (isSwinging && activeSwingTex.id != 0) {
            float cellW = (float)activeSwingTex.width / 4.0f;
            float cellH = (float)activeSwingTex.height / 4.0f;
            int row = (dir == DIR_DOWN) ? 0 : ((dir == DIR_LEFT) ? 1 : ((dir == DIR_RIGHT) ? 2 : 3));
            int col = std::clamp((int)(swingProgress * 4.0f), 0, 3);

            Rectangle src = { col * cellW, row * cellH, cellW, cellH };

            float scale = (AVATAR_HEIGHT * g_avatarScale) / 180.0f;
            float normScale = scale * (192.0f / cellH);
            float drawW = cellW * normScale;
            float drawH = cellH * normScale;

            float anchorX = drawW * (138.0f / 256.0f);
            float anchorY = drawH * (188.0f / 192.0f);

            Rectangle dest = { p.x - anchorX, p.y - anchorY, drawW, drawH };
            DrawTexturePro(activeSwingTex, src, dest, Vector2{ 0, 0 }, 0.0f, withAlpha(WHITE, alpha));
            return;
        }

        // ----------------------------------------------------
        // NORMAL IDLE / WALK: NO sword on player
        // ----------------------------------------------------
        if (loadout == LOADOUT_TIME && timeReady) {
            // Render Time Player Sprites
            Texture2D tex = timeUpDownTex;
            Rectangle src{ 0, 0, 0, 0 };
            bool flipH = false;

            if (dir == DIR_DOWN) {
                tex = timeUpDownTex;
                int idx = moving ? (1 + ((int)(animT * 8.0f) % 3)) : 0;
                src = timeDownFrames[idx];
            } else if (dir == DIR_UP) {
                tex = timeUpDownTex;
                int idx = moving ? (1 + ((int)(animT * 8.0f) % 3)) : 0;
                src = timeUpFrames[idx];
            } else if (dir == DIR_LEFT) {
                tex = timeSideTex;
                int idx = moving ? (1 + ((int)(animT * 8.0f) % 3)) : 0;
                src = timeSideFrames[idx];
                flipH = true; // sprite naturally faces right, flip horizontally to face left
            } else { // DIR_RIGHT
                tex = timeSideTex;
                int idx = moving ? (1 + ((int)(animT * 8.0f) % 3)) : 0;
                src = timeSideFrames[idx];
                flipH = false; // sprite naturally faces right
            }

            float scale = (AVATAR_HEIGHT * g_avatarScale) / src.height;
            float drawW = src.width * scale;
            float drawH = (AVATAR_HEIGHT * g_avatarScale);

            Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH, drawW, drawH };
            if (flipH) src.width = -src.width;

            DrawTexturePro(tex, src, dest, Vector2{ 0, 0 }, 0.0f, withAlpha(WHITE, alpha));
            return;
        }

        // BASE / SHADOW / FIRE standard player sprites
        Texture2D tex = downTex;
        Rectangle src{ 0, 0, 0, 0 };
        bool flipH = false;

        if (dir == DIR_DOWN) {
            tex = downTex;
            int idx = moving ? (1 + ((int)(animT * 8.0f) % 6)) : 0;
            src = downFrames[idx];
        } else if (dir == DIR_UP) {
            tex = upTex;
            int idx = moving ? (1 + ((int)(animT * 8.0f) % 6)) : 0;
            src = upFrames[idx];
        } else if (dir == DIR_LEFT) {
            tex = sideTex;
            int idx = moving ? (1 + ((int)(animT * 8.0f) % 5)) : 0;
            src = sideFrames[idx];
            flipH = false; // naturally faces left
        } else { // DIR_RIGHT
            tex = sideTex;
            int idx = moving ? (1 + ((int)(animT * 8.0f) % 5)) : 0;
            src = sideFrames[idx];
            flipH = true; // flip horizontally to face right
        }

        float scale = (AVATAR_HEIGHT * g_avatarScale) / src.height;
        float drawW = src.width * scale;
        float drawH = (AVATAR_HEIGHT * g_avatarScale);

        Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH, drawW, drawH };
        if (flipH) src.width = -src.width;

        Color playerTint = WHITE;
        if (loadout == LOADOUT_SHADOW) {
            playerTint = Color{ 200, 185, 240, 255 }; // Subtle shadow aura tint
        } else if (loadout == LOADOUT_FIRE) {
            playerTint = Color{ 255, 230, 215, 255 }; // Warm fiery tint
        }

        DrawTexturePro(tex, src, dest, Vector2{ 0, 0 }, 0.0f, withAlpha(playerTint, alpha));
    }
};

static void drawPlayerProcedural(Vector2 p, bool moving, float t, int facing, float alpha = 1.0f) {
    int x = (int)std::round(p.x), y = (int)std::round(p.y);
    float swing = moving ? std::sin(t * 14.0f) : 0.0f;
    int bob = moving ? (int)std::round(std::fabs(swing)) : 0;

    DrawEllipse(x, y, 6.0f, 2.5f, withAlpha(Color{ 0, 0, 0, 90 }, alpha));                          // shadow
    DrawRectangle(x - 3, y - 4, 2, 4 + (int)std::round(swing * 1.5f), withAlpha(Color{ 60, 40, 30, 255 }, alpha)); // legs
    DrawRectangle(x + 1, y - 4, 2, 4 - (int)std::round(swing * 1.5f), withAlpha(Color{ 60, 40, 30, 255 }, alpha));
    DrawRectangle(x - 4, y - 11 - bob, 8, 8, withAlpha(Color{ 200, 60, 50, 255 }, alpha));         // tunic
    DrawRectangle(x - 4, y - 6 - bob, 8, 1, withAlpha(Color{ 90, 50, 30, 255 }, alpha));           // belt
    DrawRectangle(x - 3, y - 17 - bob, 6, 6, withAlpha(Color{ 240, 200, 160, 255 }, alpha));       // head
    DrawRectangle(x - 3, y - 18 - bob, 6, 2, withAlpha(Color{ 70, 40, 25, 255 }, alpha));          // hair
    DrawRectangle(x + (facing > 0 ? 1 : -2), y - 14 - bob, 1, 1, withAlpha(BLACK, alpha));         // eye
}

static void drawCharacter(const AvatarRenderer& avatar, Vector2 p, bool moving, float animT,
                          Direction dir, int facing, bool isSwinging = false,
                          float swingProgress = 0.0f, PlayerLoadout loadout = LOADOUT_BASE,
                          float alpha = 1.0f) {
    if (avatar.ready) {
        avatar.draw(p, moving, animT, dir, isSwinging, swingProgress, loadout, alpha);
    } else {
        drawPlayerProcedural(p, moving, animT, facing, alpha);
    }
}

static bool inRect(const Rectangle& r, Vector2 p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

enum FogMode { FOG_FULL = 0, FOG_DIM = 1, FOG_OFF = 2 };
static const char* fogModeName(int m) { return m == FOG_FULL ? "FULL" : (m == FOG_DIM ? "DIM" : "OFF"); }

// ------------------------------------------------------------------
// Wildlife Critters System (Roaming in grass areas outside town)
// ------------------------------------------------------------------
enum CritterType {
    CRITTER_STAG   = 0,
    CRITTER_BOAR   = 1,
    CRITTER_BADGER = 2,
    CRITTER_WOLF   = 3
};

enum CritterState {
    STATE_SLEEPING = 0,
    STATE_IDLE     = 1,
    STATE_WALKING  = 2,
    STATE_RUNNING  = 3
};

enum CritterDir {
    CDIR_SW = 0,
    CDIR_SE = 1,
    CDIR_NW = 2,
    CDIR_NE = 3
};

static CritterDir getCritterDir(Vector2 vel, int facing) {
    if (std::fabs(vel.x) < 0.001f && std::fabs(vel.y) < 0.001f) {
        return (facing >= 0) ? CDIR_SE : CDIR_SW;
    }
    if (vel.y >= 0.0f) {
        return (vel.x >= 0.0f) ? CDIR_SE : CDIR_SW;
    } else {
        return (vel.x >= 0.0f) ? CDIR_NE : CDIR_NW;
    }
}

struct SleepZ {
    Vector2 pos;
    float alpha;
    float life;
    float size;
};

struct Critter {
    Vector2 pos;
    Vector2 target;
    Vector2 vel;
    CritterType type;
    CritterState state;
    float stateTimer;
    float animT;
    float breatheT;
    float zSpawnTimer;
    int facing; // -1 = left, +1 = right
    std::vector<SleepZ> zParticles;
};

struct CritterAssetManager {
    bool ready = false;

    // Stag: 4 dirs (SW=0, SE=1, NW=2, NE=3)
    Texture2D stagWalk[4]{};
    Texture2D stagRun[4]{};
    Texture2D stagIdle[4]{};

    // Badger
    Texture2D badgerWalk[4]{};
    Texture2D badgerIdle[4]{};

    // Boar
    Texture2D boarRun[4]{};
    Texture2D boarIdle[4]{};

    // Wolf
    Texture2D wolfRun{};
    Texture2D wolfIdle{};

    void load() {
        const char* prefix = "critters";
        if (FileExists("critters/stag/critter_stag_SW_walk.png")) {
            prefix = "critters";
        } else if (FileExists("../critters/stag/critter_stag_SW_walk.png")) {
            prefix = "../critters";
        } else if (FileExists("assets/critters/stag/critter_stag_SW_walk.png")) {
            prefix = "assets/critters";
        }

        const char* dirs[4] = { "SW", "SE", "NW", "NE" };
        for (int i = 0; i < 4; ++i) {
            stagWalk[i] = LoadTexture(TextFormat("%s/stag/critter_stag_%s_walk.png", prefix, dirs[i]));
            stagRun[i]  = LoadTexture(TextFormat("%s/stag/critter_stag_%s_run.png", prefix, dirs[i]));
            stagIdle[i] = LoadTexture(TextFormat("%s/stag/critter_stag_%s_idle.png", prefix, dirs[i]));

            badgerWalk[i] = LoadTexture(TextFormat("%s/badger/critter_badger_%s_walk.png", prefix, dirs[i]));
            badgerIdle[i] = LoadTexture(TextFormat("%s/badger/critter_badger_%s_idle.png", prefix, dirs[i]));

            boarRun[i]  = LoadTexture(TextFormat("%s/boar/boar_%s_run_strip.png", prefix, dirs[i]));
            boarIdle[i] = LoadTexture(TextFormat("%s/boar/boar_%s_idle_strip.png", prefix, dirs[i]));

            SetTextureFilter(stagWalk[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(stagRun[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(stagIdle[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(badgerWalk[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(badgerIdle[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(boarRun[i], TEXTURE_FILTER_POINT);
            SetTextureFilter(boarIdle[i], TEXTURE_FILTER_POINT);
        }

        wolfRun  = LoadTexture(TextFormat("%s/wolf/wolf-run.png", prefix));
        wolfIdle = LoadTexture(TextFormat("%s/wolf/wolf-idle.png", prefix));
        SetTextureFilter(wolfRun, TEXTURE_FILTER_POINT);
        SetTextureFilter(wolfIdle, TEXTURE_FILTER_POINT);

        ready = (stagWalk[0].id > 0);
        if (ready) {
            TraceLog(LOG_INFO, "CRITTERS: Successfully loaded animal sprites from %s/", prefix);
        } else {
            TraceLog(LOG_WARNING, "CRITTERS: Could not locate animal sprites from %s/ - falling back to procedural", prefix);
        }
    }

    void unload() {
        if (!ready) return;
        for (int i = 0; i < 4; ++i) {
            UnloadTexture(stagWalk[i]);
            UnloadTexture(stagRun[i]);
            UnloadTexture(stagIdle[i]);
            UnloadTexture(badgerWalk[i]);
            UnloadTexture(badgerIdle[i]);
            UnloadTexture(boarRun[i]);
            UnloadTexture(boarIdle[i]);
        }
        UnloadTexture(wolfRun);
        UnloadTexture(wolfIdle);
        ready = false;
    }
};

static void drawSleepZ(const SleepZ& z) {
    if (z.alpha <= 0.02f) return;
    Color col = Color{ 210, 235, 255, (uint8_t)(z.alpha * 240) };
    DrawText("z", (int)std::round(z.pos.x), (int)std::round(z.pos.y), (int)std::round(z.size), col);
}

// --- Procedural Fallbacks (Used if external sprites are missing) ---
static void drawDeerProcedural(Vector2 p, CritterState state, float animT, float breatheT, int facing) {
    int x = (int)std::round(p.x), y = (int)std::round(p.y);
    Color coat = Color{ 175, 105, 55, 255 };
    Color belly = Color{ 235, 220, 200, 255 };
    Color spot = Color{ 255, 250, 240, 220 };
    Color dark = Color{ 65, 40, 25, 255 };
    DrawEllipse(x, y, 13.0f, 4.5f, Color{ 0, 0, 0, 65 });

    if (state == STATE_SLEEPING) {
        float sx = 13.5f * (1.0f - 0.04f * std::sin(breatheT));
        float sy = 8.0f * (1.0f + 0.06f * std::sin(breatheT));
        DrawEllipse(x, y - 8, sx, sy, coat);
        DrawEllipse(x + facing * 2, y - 7, 8.0f, 4.5f, belly);
        DrawCircle(x - facing * 4, y - 11, 1.2f, spot);
        DrawCircle(x, y - 12, 1.2f, spot);
        DrawCircle(x + facing * 4, y - 11, 1.2f, spot);
        DrawCircle(x + facing * 9, y - 9, 4.5f, coat);
        DrawCircle(x + facing * 12, y - 8, 2.8f, belly);
        DrawCircle(x + facing * 14, y - 8, 1.2f, dark);
        DrawLine(x + facing * 9, y - 10, x + facing * 11, y - 10, dark);
        DrawEllipse(x + facing * 7, y - 13, 3.8f, 1.6f, coat);
    } else {
        float stepSwing = (state == STATE_WALKING) ? std::sin(animT * 7.0f) : 0.0f;
        int headBob = (state == STATE_WALKING) ? (int)(std::fabs(stepSwing) * 1.5f) : 0;
        DrawEllipse(x, y - 15, 12.0f, 7.2f, coat);
        DrawEllipse(x + facing * 1, y - 13, 8.5f, 4.0f, belly);
        DrawLineEx({ (float)(x + facing * 6), (float)(y - 15) },
                   { (float)(x + facing * 12), (float)(y - 23 - headBob) }, 3.5f, coat);
        DrawCircle(x + facing * 12, y - 24 - headBob, 4.5f, coat);
        DrawLineEx({ (float)(x - facing * 5), (float)(y - 9) }, { (float)(x - facing * 5 - stepSwing * 3.5f), (float)y }, 1.8f, dark);
        DrawLineEx({ (float)(x + facing * 5), (float)(y - 9) }, { (float)(x + facing * 5 + stepSwing * 3.5f), (float)y }, 1.8f, dark);
    }
}

// --- Main Critter Renderer ---
static void drawCritterSprite(const CritterAssetManager& assets, const Critter& c) {
    Vector2 p = c.pos;
    CritterDir dir = getCritterDir(c.vel, c.facing);
    int d = (int)dir;

    float breathe = (c.state == STATE_SLEEPING) ? (std::sin(c.breatheT) * 0.06f) : 0.0f;
    float sx = 1.0f - breathe * 0.4f;
    float sy = 1.0f + breathe;

    if (c.type == CRITTER_STAG) {
        DrawEllipse((int)p.x, (int)p.y, 14.0f, 5.0f, Color{ 0, 0, 0, 75 }); // shadow

        Texture2D tex;
        int frameCount = 1;
        float fw = 32.0f, fh = 41.0f;
        int frameIdx = 0;

        if (c.state == STATE_RUNNING) {
            tex = assets.stagRun[d];
            frameCount = 10;
            frameIdx = (int)(c.animT * 14.0f) % frameCount;
        } else if (c.state == STATE_WALKING) {
            tex = assets.stagWalk[d];
            frameCount = 11;
            frameIdx = (int)(c.animT * 9.0f) % frameCount;
        } else { // IDLE or SLEEPING
            tex = assets.stagIdle[d];
            frameCount = 24;
            frameIdx = (c.state == STATE_SLEEPING) ? 0 : ((int)(c.animT * 7.0f) % frameCount);
        }

        float drawH = 46.0f * sy;
        float drawW = (fw / fh) * 46.0f * sx;
        Rectangle src = { (float)(frameIdx * (int)fw), 0.0f, fw, fh };
        Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH + 3.0f, drawW, drawH };
        DrawTexturePro(tex, src, dest, { 0, 0 }, 0.0f, WHITE);
    }
    else if (c.type == CRITTER_BADGER) {
        DrawEllipse((int)p.x, (int)p.y, 11.0f, 4.0f, Color{ 0, 0, 0, 75 });

        Texture2D tex;
        int frameCount = 1;
        float fw = 42.0f, fh = 32.0f;
        int frameIdx = 0;

        if (c.state == STATE_RUNNING || c.state == STATE_WALKING) {
            tex = assets.badgerWalk[d];
            frameCount = 9;
            float fps = (c.state == STATE_RUNNING) ? 16.0f : 9.0f;
            frameIdx = (int)(c.animT * fps) % frameCount;
        } else { // IDLE or SLEEPING
            tex = assets.badgerIdle[d];
            frameCount = 22;
            frameIdx = (c.state == STATE_SLEEPING) ? 0 : ((int)(c.animT * 7.0f) % frameCount);
        }

        float drawH = 28.0f * sy;
        float drawW = (fw / fh) * 28.0f * sx;
        Rectangle src = { (float)(frameIdx * (int)fw), 0.0f, fw, fh };
        Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH + 2.0f, drawW, drawH };
        DrawTexturePro(tex, src, dest, { 0, 0 }, 0.0f, WHITE);
    }
    else if (c.type == CRITTER_BOAR) {
        DrawEllipse((int)p.x, (int)p.y, 12.0f, 4.5f, Color{ 0, 0, 0, 75 });

        Texture2D tex;
        int frameCount = 1;
        float fw = 41.0f, fh = 25.0f;
        int frameIdx = 0;

        if (c.state == STATE_RUNNING || c.state == STATE_WALKING) {
            tex = assets.boarRun[d];
            frameCount = 4;
            float fps = (c.state == STATE_RUNNING) ? 14.0f : 8.0f;
            frameIdx = (int)(c.animT * fps) % frameCount;
        } else { // IDLE or SLEEPING
            tex = assets.boarIdle[d];
            frameCount = 7;
            frameIdx = (c.state == STATE_SLEEPING) ? 0 : ((int)(c.animT * 6.0f) % frameCount);
        }

        float drawH = 28.0f * sy;
        float drawW = (fw / fh) * 28.0f * sx;
        Rectangle src = { (float)(frameIdx * (int)fw), 0.0f, fw, fh };
        Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH + 2.0f, drawW, drawH };
        DrawTexturePro(tex, src, dest, { 0, 0 }, 0.0f, WHITE);
    }
    else { // CRITTER_WOLF
        DrawEllipse((int)p.x, (int)p.y, 12.0f, 4.5f, Color{ 0, 0, 0, 75 });

        // Wolf row mapping: SE=0, SW=1, NE=2, NW=3
        int row = (d == CDIR_SE) ? 0 : ((d == CDIR_SW) ? 1 : ((d == CDIR_NE) ? 2 : 3));
        Texture2D tex;
        int frameCount = 1;
        float fw = 64.0f, fh = 64.0f;
        int frameIdx = 0;

        if (c.state == STATE_RUNNING || c.state == STATE_WALKING) {
            tex = assets.wolfRun;
            frameCount = 8;
            float fps = (c.state == STATE_RUNNING) ? 15.0f : 9.0f;
            frameIdx = (int)(c.animT * fps) % frameCount;
        } else { // IDLE or SLEEPING
            tex = assets.wolfIdle;
            frameCount = 4;
            frameIdx = (c.state == STATE_SLEEPING) ? 0 : ((int)(c.animT * 6.0f) % frameCount);
        }

        float drawH = 40.0f * sy;
        float drawW = 40.0f * sx;
        Rectangle src = { (float)(frameIdx * (int)fw), (float)(row * (int)fh), fw, fh };
        Rectangle dest = { p.x - drawW * 0.5f, p.y - drawH + 6.0f, drawW, drawH };
        DrawTexturePro(tex, src, dest, { 0, 0 }, 0.0f, WHITE);
    }
}

static void drawCritters(const CritterAssetManager& assets, const std::vector<Critter>& critters) {
    for (const auto& c : critters) {
        if (assets.ready) {
            drawCritterSprite(assets, c);
        } else {
            drawDeerProcedural(c.pos, c.state, c.animT, c.breatheT, c.facing);
        }
        for (const auto& z : c.zParticles) {
            drawSleepZ(z);
        }
    }
}

static std::vector<Critter> initCritters(const CritterGrassMask& mask, int count = 14) {
    std::vector<Critter> list;
    if (mask.validCoords.empty()) return list;

    for (int i = 0; i < count; ++i) {
        Critter c;
        c.pos = mask.getRandomPoint();
        c.target = c.pos;
        c.vel = { 0.0f, 0.0f };

        // 30% stag, 30% boar, 25% badger, 15% wolf
        int typeRoll = GetRandomValue(0, 99);
        if (typeRoll < 30) c.type = CRITTER_STAG;
        else if (typeRoll < 60) c.type = CRITTER_BOAR;
        else if (typeRoll < 85) c.type = CRITTER_BADGER;
        else c.type = CRITTER_WOLF;

        // Diverse initial states: ~35% sleeping, ~35% walking, ~15% running, ~15% idle
        int stateRoll = GetRandomValue(0, 99);
        if (stateRoll < 35) {
            c.state = STATE_SLEEPING;
            c.stateTimer = (float)GetRandomValue(80, 200) * 0.1f;
        } else if (stateRoll < 70) {
            c.state = STATE_WALKING;
            c.stateTimer = (float)GetRandomValue(40, 100) * 0.1f;
            c.target = mask.getRandomPointNear(c.pos, 80.0f, 220.0f);
        } else if (stateRoll < 85) {
            c.state = STATE_RUNNING;
            c.stateTimer = (float)GetRandomValue(30, 60) * 0.1f;
            c.target = mask.getRandomPointNear(c.pos, 100.0f, 260.0f);
        } else {
            c.state = STATE_IDLE;
            c.stateTimer = (float)GetRandomValue(25, 60) * 0.1f;
        }

        c.animT = (float)GetRandomValue(0, 1000) * 0.01f;
        c.breatheT = (float)GetRandomValue(0, 628) * 0.01f;
        c.zSpawnTimer = (float)GetRandomValue(0, 100) * 0.01f;
        c.facing = (GetRandomValue(0, 1) == 0) ? -1 : 1;
        list.push_back(c);
    }
    return list;
}

static void updateCritters(std::vector<Critter>& critters, float dt,
                           const CritterGrassMask& mask, Vector2 playerPos, bool isSwinging,
                           bool freezeActive = false, Vector2 freezeCenter = { 0.0f, 0.0f }, float freezeRadius = 0.0f) {
    (void)playerPos;   // Critters are friendly/peaceful and do NOT flee from the player
    (void)isSwinging;

    for (auto& c : critters) {
        if (freezeActive && v2Dist(c.pos, freezeCenter) <= freezeRadius) continue; // TIME STOP
        c.stateTimer -= dt;
        c.breatheT += dt * 2.2f;

        // State Machine (natural ambient transitions - never runs away from player)
        if (c.state == STATE_SLEEPING) {
            c.vel = { 0.0f, 0.0f };

            // Emit floating 'z' particles periodically
            c.zSpawnTimer += dt;
            if (c.zSpawnTimer >= 1.05f) {
                c.zSpawnTimer = 0.0f;
                SleepZ z;
                z.pos = { c.pos.x + c.facing * 3.0f, c.pos.y - 12.0f };
                z.alpha = 0.95f;
                z.life = 0.0f;
                z.size = (float)GetRandomValue(10, 13);
                c.zParticles.push_back(z);
            }

            // Natural wake up after cozy nap
            if (c.stateTimer <= 0.0f) {
                c.state = STATE_IDLE;
                c.stateTimer = (float)GetRandomValue(30, 60) * 0.1f;
            }
        } else if (c.state == STATE_IDLE) {
            c.vel = { 0.0f, 0.0f };
            c.animT += dt * 0.5f;

            if (c.stateTimer <= 0.0f) {
                // Natural choices: 35% sleep, 45% leisurely walk, 20% playful run/frolic
                int roll = GetRandomValue(0, 99);
                if (roll < 35) {
                    c.state = STATE_SLEEPING;
                    c.stateTimer = (float)GetRandomValue(100, 240) * 0.1f;
                } else if (roll < 80) {
                    c.state = STATE_WALKING;
                    c.stateTimer = (float)GetRandomValue(50, 110) * 0.1f;
                    c.target = mask.getRandomPointNear(c.pos, 50.0f, 180.0f);
                    c.facing = (c.target.x >= c.pos.x) ? 1 : -1;
                } else {
                    c.state = STATE_RUNNING;
                    c.stateTimer = (float)GetRandomValue(35, 65) * 0.1f;
                    c.target = mask.getRandomPointNear(c.pos, 100.0f, 260.0f);
                    c.facing = (c.target.x >= c.pos.x) ? 1 : -1;
                }
            }
        } else if (c.state == STATE_WALKING) {
            float walkSpeed = (c.type == CRITTER_STAG) ? 44.0f : ((c.type == CRITTER_BOAR) ? 40.0f : ((c.type == CRITTER_BADGER) ? 34.0f : 52.0f));
            c.animT += dt * (walkSpeed / 25.0f);

            Vector2 toTarget = { c.target.x - c.pos.x, c.target.y - c.pos.y };
            float dist = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);

            if (dist < 6.0f || c.stateTimer <= 0.0f) {
                int roll = GetRandomValue(0, 99);
                if (roll < 20) {
                    c.state = STATE_RUNNING;
                    c.stateTimer = (float)GetRandomValue(30, 60) * 0.1f;
                    c.target = mask.getRandomPointNear(c.pos, 90.0f, 240.0f);
                } else {
                    c.state = STATE_IDLE;
                    c.stateTimer = (float)GetRandomValue(25, 55) * 0.1f;
                    c.vel = { 0.0f, 0.0f };
                }
            } else {
                Vector2 moveDir = { toTarget.x / dist, toTarget.y / dist };
                c.vel = { moveDir.x * walkSpeed, moveDir.y * walkSpeed };
                c.facing = (moveDir.x >= 0.0f) ? 1 : -1;

                Vector2 nextPos = { c.pos.x + c.vel.x * dt, c.pos.y + c.vel.y * dt };
                if (mask.isValidF(nextPos.x, nextPos.y)) {
                    c.pos = nextPos;
                } else {
                    c.target = mask.getRandomPointNear(c.pos, 50.0f, 160.0f);
                    c.state = STATE_IDLE;
                    c.stateTimer = (float)GetRandomValue(15, 35) * 0.1f;
                    c.vel = { 0.0f, 0.0f };
                }
            }
        } else if (c.state == STATE_RUNNING) {
            float runSpeed = (c.type == CRITTER_STAG) ? 175.0f : ((c.type == CRITTER_BOAR) ? 165.0f : ((c.type == CRITTER_BADGER) ? 150.0f : 195.0f));
            c.animT += dt * (runSpeed / 20.0f);

            Vector2 toTarget = { c.target.x - c.pos.x, c.target.y - c.pos.y };
            float dist = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);

            if (dist < 8.0f || c.stateTimer <= 0.0f) {
                c.state = STATE_IDLE;
                c.stateTimer = (float)GetRandomValue(25, 55) * 0.1f;
                c.vel = { 0.0f, 0.0f };
            } else {
                Vector2 runDir = { toTarget.x / dist, toTarget.y / dist };
                c.vel = { runDir.x * runSpeed, runDir.y * runSpeed };
                c.facing = (runDir.x >= 0.0f) ? 1 : -1;

                Vector2 nextPos = { c.pos.x + c.vel.x * dt, c.pos.y + c.vel.y * dt };
                if (mask.isValidF(nextPos.x, nextPos.y)) {
                    c.pos = nextPos;
                } else {
                    c.target = mask.getRandomPointNear(c.pos, 60.0f, 180.0f);
                    c.state = STATE_WALKING;
                    c.stateTimer = (float)GetRandomValue(30, 60) * 0.1f;
                }
            }
        }

        // Failsafe: keep critter strictly within valid outside grass
        if (!mask.isValidF(c.pos.x, c.pos.y)) {
            c.pos = mask.getRandomPointNear(c.pos, 10.0f, 60.0f);
        }

        // Update sleep Z particles
        for (size_t i = 0; i < c.zParticles.size(); ) {
            auto& z = c.zParticles[i];
            z.life += dt;
            z.pos.y -= 10.0f * dt;
            z.pos.x += std::sin(z.life * 3.5f) * 4.5f * dt;
            z.alpha = std::clamp(1.0f - z.life / 1.6f, 0.0f, 1.0f);
            if (z.alpha <= 0.0f || z.life >= 1.6f) {
                c.zParticles.erase(c.zParticles.begin() + i);
            } else {
                ++i;
            }
        }
    }
}

// ------------------------------------------------------------------
// Enemies & Combat System (Slimes, Golem, Cyclops)
// ------------------------------------------------------------------
enum EnemyType {
    ENEMY_SLIME = 0,
    ENEMY_GOLEM = 1,
    ENEMY_CYCLOPS = 2,
    ENEMY_ORC = 3
};

enum EnemyState {
    ESTATE_IDLE = 0,
    ESTATE_MOVE = 1,
    ESTATE_ATTACK = 2,
    ESTATE_DEAD = 3
};

struct FloatingText {
    Vector2 pos;
    std::string text;
    Color color;
    float timer = 0.0f;
    float life = 0.85f;
};

struct Enemy {
    int id = 0;
    EnemyType type = ENEMY_SLIME;
    Vector2 pos = { 0.0f, 0.0f };
    Vector2 spawnPos = { 0.0f, 0.0f };
    Vector2 vel = { 0.0f, 0.0f };
    int facing = 1; // 1 = right, -1 = left

    int hp = 10;
    int maxHp = 10;
    int damage = 10;

    EnemyState state = ESTATE_IDLE;
    EnemyState prevState = ESTATE_IDLE;
    float stateTimer = 0.0f;
    float animT = 0.0f;
    float attackCooldown = 0.0f;
    float hitFlashTimer = 0.0f;
    float deathTimer = 0.0f;
    bool isDead = false;
    bool isReturning = false;
    float scale = 1.0f; // visual/hit scale (King Slime etc.)
};

static Texture2D loadEnemyTexture(const std::string& path) {
    if (!FileExists(path.c_str())) {
        return Texture2D{ 0 };
    }
    Image img = LoadImage(path.c_str());
    if (img.data == nullptr) {
        return Texture2D{ 0 };
    }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D tex = LoadTextureFromImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    UnloadImage(img);
    return tex;
}

struct EnemyAssetManager {
    bool ready = false;
    // Floor 2 Enemies (blob, golem, cyclops)
    Texture2D blobTex{};   // 1280x768 (10 cols x 6 rows of 128x128)
    Texture2D golemTex{};  // 1280x640 (10 cols x 5 rows of 128x128)

    // Cyclops (3-* set with horizontal flip)
    Texture2D cyclopsIdle[4]{};
    Texture2D cyclopsMove[4]{};
    Texture2D cyclopsBlow[6]{};
    Texture2D cyclopsDeath[6]{};
    bool cyclopsReady = false;

    // Floor 1 Enemies (orc / goblin)
    Texture2D orcTex{};    // 1024x640 (8 cols x 5 rows of 128x128)
    bool floor1Ready = false;

    void load() {
        // Floor 2 prefix (prefer floor2/enemies or floor2 folder)
        const char* prefix2 = "floor2/enemies";
        if (FileExists("floor2/enemies/blob.png")) prefix2 = "floor2/enemies";
        else if (FileExists("floor2/blob.png")) prefix2 = "floor2";
        else if (FileExists("../floor2/enemies/blob.png")) prefix2 = "../floor2/enemies";
        else if (FileExists("enemies/blob.png")) prefix2 = "enemies";
        else if (FileExists("../enemies/blob.png")) prefix2 = "../enemies";
        else if (FileExists("assets/enemies/blob.png")) prefix2 = "assets/enemies";

        std::string blobPath = std::string(prefix2) + "/blob.png";
        blobTex = loadEnemyTexture(blobPath);

        std::string golemPath = std::string(prefix2) + "/golem.png";
        golemTex = loadEnemyTexture(golemPath);

        std::string cycDir = std::string(prefix2) + "/cyclops";
        for (int i = 0; i < 4; ++i) {
            cyclopsIdle[i] = loadEnemyTexture(cycDir + "/3-idle" + std::to_string(i) + ".png");
        }
        for (int i = 0; i < 4; ++i) {
            cyclopsMove[i] = loadEnemyTexture(cycDir + "/3-move" + std::to_string(i + 1) + ".png");
        }
        for (int i = 0; i < 6; ++i) {
            cyclopsBlow[i] = loadEnemyTexture(cycDir + "/3-blow" + std::to_string(i + 1) + ".png");
        }
        for (int i = 0; i < 6; ++i) {
            cyclopsDeath[i] = loadEnemyTexture(cycDir + "/3-death" + std::to_string(i + 1) + ".png");
        }
        cyclopsReady = (cyclopsIdle[0].id != 0);
        ready = (blobTex.id != 0 && golemTex.id != 0);

        // Floor 1 prefix (prefer floor1 folder)
        const char* prefix1 = "floor1";
        if (FileExists("floor1/orc.png")) prefix1 = "floor1";
        else if (FileExists("../floor1/orc.png")) prefix1 = "../floor1";
        else if (FileExists("assets/floor1/orc.png")) prefix1 = "assets/floor1";

        orcTex = loadEnemyTexture(std::string(prefix1) + "/orc.png");
        floor1Ready = (orcTex.id != 0);
    }

    void unload() {
        if (blobTex.id != 0) UnloadTexture(blobTex);
        if (golemTex.id != 0) UnloadTexture(golemTex);
        for (int i = 0; i < 4; ++i) {
            if (cyclopsIdle[i].id != 0) UnloadTexture(cyclopsIdle[i]);
            if (cyclopsMove[i].id != 0) UnloadTexture(cyclopsMove[i]);
        }
        for (int i = 0; i < 6; ++i) {
            if (cyclopsBlow[i].id != 0) UnloadTexture(cyclopsBlow[i]);
            if (cyclopsDeath[i].id != 0) UnloadTexture(cyclopsDeath[i]);
        }
        if (orcTex.id != 0) UnloadTexture(orcTex);
    }
};

// Time Stop state (shared with drawing so frozen enemies get an icy tint)
static bool    g_tsActive = false;
static Vector2 g_tsCenter = { 0.0f, 0.0f };
static float   g_tsRadius = 0.0f;

static void drawEnemy(const EnemyAssetManager& assets, const Enemy& e) {
    if (e.isDead) return;

    // Ground shadow
    float shadowW = (e.type == ENEMY_SLIME) ? 10.0f : ((e.type == ENEMY_GOLEM) ? 20.0f : ((e.type == ENEMY_CYCLOPS) ? 24.0f : 18.0f));
    float shadowH = (e.type == ENEMY_SLIME) ? 4.0f : ((e.type == ENEMY_GOLEM) ? 8.0f : ((e.type == ENEMY_CYCLOPS) ? 9.0f : 7.0f));
    DrawEllipse((int)std::round(e.pos.x), (int)std::round(e.pos.y), shadowW, shadowH, Color{ 0, 0, 0, 95 });

    Color tint = WHITE;
    if (e.hitFlashTimer > 0.0f) {
        tint = Color{ 255, 110, 110, 255 }; // red hit flash
    } else if (g_tsActive && e.state != ESTATE_DEAD && v2Dist(e.pos, g_tsCenter) <= g_tsRadius) {
        tint = Color{ 140, 195, 255, 255 }; // frozen in time
    }

    if (e.type == ENEMY_SLIME && assets.blobTex.id != 0) {
        int row = 0;
        int maxFrames = 8;
        if (e.state == ESTATE_MOVE)        { row = 1; maxFrames = 7; }
        else if (e.state == ESTATE_ATTACK) { row = 2; maxFrames = 7; }
        else if (e.state == ESTATE_DEAD)   { row = 3; maxFrames = 6; }

        int col = (e.state == ESTATE_DEAD) ? std::clamp((int)(e.deathTimer * 8.0f), 0, maxFrames - 1)
                                           : ((int)e.animT % maxFrames);
        Rectangle src = { col * 128.0f, row * 128.0f, 128.0f, 128.0f };
        if (e.facing < 0) src.width = -src.width;

        float drawW = 38.0f * e.scale, drawH = 38.0f * e.scale;
        Rectangle dest = { std::round(e.pos.x - drawW * 0.5f), std::round(e.pos.y - drawH + 4.0f), drawW, drawH };
        DrawTexturePro(assets.blobTex, src, dest, { 0, 0 }, 0.0f, tint);

        // Floating health bar when damaged
        if (e.hp < e.maxHp && e.hp > 0) {
            float barW = 28.0f, barH = 3.5f;
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)barW, (int)barH, Color{ 35, 10, 10, 220 });
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)(barW * ((float)e.hp / e.maxHp)), (int)barH, GREEN);
            DrawRectangleLines((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)barW, (int)barH, BLACK);
        }
    } else if (e.type == ENEMY_GOLEM && assets.golemTex.id != 0) {
        int row = 0;
        int maxFrames = 9;
        if (e.state == ESTATE_MOVE)        { row = 1; maxFrames = 7; }
        else if (e.state == ESTATE_ATTACK) { row = 2; maxFrames = 7; }
        else if (e.state == ESTATE_DEAD)   { row = 3; maxFrames = 6; }

        int col = (e.state == ESTATE_DEAD) ? std::clamp((int)(e.deathTimer * 8.0f), 0, maxFrames - 1)
                                           : ((int)e.animT % maxFrames);
        Rectangle src = { col * 128.0f, row * 128.0f, 128.0f, 128.0f };
        if (e.facing < 0) src.width = -src.width;

        float drawW = 82.0f, drawH = 82.0f;
        Rectangle dest = { std::round(e.pos.x - drawW * 0.5f), std::round(e.pos.y - drawH + 8.0f), drawW, drawH };
        DrawTexturePro(assets.golemTex, src, dest, { 0, 0 }, 0.0f, tint);

        // Sub-Boss overhead indicator & health bar
        if (e.hp > 0) {
            float barW = 54.0f, barH = 5.5f;
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 5.0f), (int)barW, (int)barH, Color{ 35, 10, 10, 230 });
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 5.0f), (int)(barW * ((float)e.hp / e.maxHp)), (int)barH, GOLD);
            DrawRectangleLines((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 5.0f), (int)barW, (int)barH, RAYWHITE);
            const char* txt = TextFormat("Stone Golem %d/%d", e.hp, e.maxHp);
            DrawText(txt, (int)std::round(e.pos.x - MeasureText(txt, 10) / 2.0f), (int)std::round(e.pos.y - drawH - 17.0f), 10, GOLD);
        }
    } else if (e.type == ENEMY_CYCLOPS) {
        Texture2D tex{ 0 };
        if (e.state == ESTATE_MOVE) {
            tex = assets.cyclopsMove[((int)e.animT) % 4];
        } else if (e.state == ESTATE_ATTACK) {
            tex = assets.cyclopsBlow[((int)e.animT) % 6];
        } else if (e.state == ESTATE_DEAD) {
            int f = std::clamp((int)(e.deathTimer * 8.0f), 0, 5);
            tex = assets.cyclopsDeath[f];
        } else {
            tex = assets.cyclopsIdle[((int)e.animT) % 4];
        }

        // Robust fallback: if requested animation frame is missing, fall back to idle[0]
        if (tex.id == 0) tex = assets.cyclopsIdle[0];

        if (tex.id != 0) {
            float drawW = 86.0f, drawH = 84.0f;
            Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
            // 3-* sprites face left by default; flip horizontally if facing right
            if (e.facing > 0) src.width = -src.width;

            Rectangle dest = { std::round(e.pos.x - drawW * 0.5f), std::round(e.pos.y - drawH + 8.0f), drawW, drawH };
            DrawTexturePro(tex, src, dest, { 0, 0 }, 0.0f, tint);
        } else {
            // Safety fallback so boss is never invisible
            DrawCircle((int)std::round(e.pos.x), (int)std::round(e.pos.y - 25.0f), 25.0f, Color{ 220, 60, 60, 220 });
        }

        // Main Boss overhead indicator & health bar
        if (e.hp > 0) {
            float barW = 60.0f, barH = 6.0f;
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - 84.0f), (int)barW, (int)barH, Color{ 35, 10, 10, 230 });
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - 84.0f), (int)(barW * ((float)e.hp / e.maxHp)), (int)barH, Color{ 255, 75, 75, 255 });
            DrawRectangleLines((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - 84.0f), (int)barW, (int)barH, RAYWHITE);
            const char* txt = TextFormat("Wild Cyclops (Boss) %d/%d", e.hp, e.maxHp);
            DrawText(txt, (int)std::round(e.pos.x - MeasureText(txt, 11) / 2.0f), (int)std::round(e.pos.y - 99.0f), 11, Color{ 255, 130, 130, 255 });
        }
    } else if (e.type == ENEMY_ORC && assets.orcTex.id != 0) {
        int row = 0;
        int maxFrames = 8;
        if (e.state == ESTATE_MOVE)        { row = 1; maxFrames = 7; }
        else if (e.state == ESTATE_ATTACK) { row = 2; maxFrames = 7; }
        else if (e.state == ESTATE_DEAD)   { row = 3; maxFrames = 7; }

        int col = (e.state == ESTATE_DEAD) ? std::clamp((int)(e.deathTimer * 8.0f), 0, maxFrames - 1)
                                           : ((int)e.animT % maxFrames);
        Rectangle src = { col * 128.0f, row * 128.0f, 128.0f, 128.0f };
        if (e.facing < 0) src.width = -src.width;

        float drawW = 68.0f, drawH = 68.0f;
        Rectangle dest = { std::round(e.pos.x - drawW * 0.5f), std::round(e.pos.y - drawH + 8.0f), drawW, drawH };
        DrawTexturePro(assets.orcTex, src, dest, { 0, 0 }, 0.0f, tint);

        // Overhead health bar & name
        if (e.hp > 0) {
            float barW = 44.0f, barH = 5.0f;
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)barW, (int)barH, Color{ 35, 10, 10, 230 });
            DrawRectangle((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)(barW * ((float)e.hp / e.maxHp)), (int)barH, Color{ 90, 220, 90, 255 });
            DrawRectangleLines((int)std::round(e.pos.x - barW * 0.5f), (int)std::round(e.pos.y - drawH - 3.0f), (int)barW, (int)barH, BLACK);
            const char* txt = (e.maxHp >= 50) ? TextFormat("Goblin Chieftain %d/%d", e.hp, e.maxHp)
                                              : TextFormat("Goblin Warrior %d/%d", e.hp, e.maxHp);
            DrawText(txt, (int)std::round(e.pos.x - MeasureText(txt, 10) / 2.0f), (int)std::round(e.pos.y - drawH - 16.0f), 10, Color{ 160, 240, 160, 255 });
        }
    }

    // King Slime name tag
    if (e.type == ENEMY_SLIME && e.scale > 1.2f && e.hp > 0) {
        const char* txt = TextFormat("King Slime %d/%d", e.hp, e.maxHp);
        DrawText(txt, (int)std::round(e.pos.x - MeasureText(txt, 10) / 2.0f),
                 (int)std::round(e.pos.y - 38.0f * e.scale - 16.0f), 10, Color{ 160, 255, 160, 255 });
    }
}

static void drawEnemies(const EnemyAssetManager& assets, const std::vector<Enemy>& enemies) {
    for (const auto& e : enemies) {
        drawEnemy(assets, e);
    }
}

static void drawFloatingTexts(const std::vector<FloatingText>& texts) {
    for (const auto& ft : texts) {
        float alpha = std::clamp(1.0f - ft.timer / ft.life, 0.0f, 1.0f);
        DrawText(ft.text.c_str(), (int)ft.pos.x - MeasureText(ft.text.c_str(), 14) / 2, (int)ft.pos.y, 14, ColorAlpha(ft.color, alpha));
    }
}

static std::vector<Enemy> initEnemiesMap1() {
    std::vector<Enemy> list;

    // Slimes patrol the outer forest trail (OUTSIDE the city boundary, x >= 1400).
    // (They replace the old goblins whose sprite animation did not play correctly.)
    // The last one guards the Map 2 portal as a bigger "King Slime".
    const Vector2 slimeSpawns[8] = {
        { 1420.0f, 620.0f }, // Forest trailhead
        { 1620.0f, 615.0f }, // Mid-forest hollow patrol
        { 1740.0f, 600.0f }, // Between hollow and grove
        { 1820.0f, 590.0f }, // Shadowed grove watchpost
        { 2020.0f, 540.0f }, // Eastern valley trail
        { 2130.0f, 500.0f }, // Valley bend
        { 2220.0f, 460.0f }, // Mountain pass
        { 2380.0f, 446.0f }  // King Slime guarding the Map 2 portal
    };

    for (int i = 0; i < 8; ++i) {
        Enemy g;
        g.id = 100 + i;
        g.type = ENEMY_SLIME;
        g.pos = slimeSpawns[i];
        g.spawnPos = g.pos;
        bool king = (i == 7);
        g.hp = king ? 40 : 10;
        g.maxHp = g.hp;
        g.scale = king ? 1.7f : 1.0f;
        g.damage = 10;
        g.facing = (i % 2 == 0) ? -1 : 1;
        g.state = ESTATE_IDLE;
        g.prevState = ESTATE_IDLE;
        list.push_back(g);
    }

    return list;
}

// Floor 4 (map4.png): Pentagram Arena - Stone Golem + slimes
static std::vector<Enemy> initEnemiesFloor4() {
    std::vector<Enemy> list;
    const Vector2 slimeSpawns[6] = {
        { 300.0f, 200.0f }, { 720.0f, 200.0f }, { 260.0f, 320.0f },
        { 770.0f, 300.0f }, { 420.0f, 385.0f }, { 610.0f, 385.0f }
    };
    for (int i = 0; i < 6; ++i) {
        Enemy s;
        s.id = 300 + i;
        s.type = ENEMY_SLIME;
        s.pos = slimeSpawns[i];
        s.spawnPos = s.pos;
        s.hp = 10; s.maxHp = 10; s.damage = 10;
        s.facing = (i % 2 == 0) ? 1 : -1;
        list.push_back(s);
    }
    Enemy golem;
    golem.id = 310;
    golem.type = ENEMY_GOLEM;
    golem.pos = { 512.0f, 170.0f };
    golem.spawnPos = golem.pos;
    golem.hp = 90; golem.maxHp = 90; golem.damage = 10;
    list.push_back(golem);
    return list;
}

static std::vector<Enemy> initEnemiesMap2() {
    std::vector<Enemy> list;

    // 1) Stone Golem (Sub-Boss) - Level 2 top center clearing: 75 HP, 10 DMG
    {
        Enemy golem;
        golem.id = 1;
        golem.type = ENEMY_GOLEM;
        golem.pos = { 856.0f, 320.0f };
        golem.spawnPos = golem.pos;
        golem.hp = 75;
        golem.maxHp = 75;
        golem.damage = 10;
        golem.facing = 1;
        list.push_back(golem);
    }

    // 2) Wild Cyclopes - 3 Cyclopes at the right center battle arena: 75 HP, 10 DMG each
    const Vector2 cyclopsSpawns[3] = {
        { 1250.0f, 585.0f }, // Center of the large circular battle arena (Main Boss)
        { 1175.0f, 545.0f }, // Arena north-west flank
        { 1315.0f, 615.0f }  // Arena south-east flank
    };

    for (int i = 0; i < 3; ++i) {
        Enemy cyc;
        cyc.id = 2 + i;
        cyc.type = ENEMY_CYCLOPS;
        cyc.pos = cyclopsSpawns[i];
        cyc.spawnPos = cyc.pos;
        cyc.hp = 75;
        cyc.maxHp = 75;
        cyc.damage = 10;
        cyc.facing = (i % 2 == 0) ? -1 : 1;
        list.push_back(cyc);
    }

    // 3) 15 Slimes (10 along Level 2 dirt walking paths + 5 more inside the Cyclops arena): 10 HP, 10 DMG each
    const Vector2 slimeSpawns[15] = {
        {   80.0f, 565.0f }, // West entrance trail
        {  130.0f, 400.0f }, // North-west forest fork
        {  230.0f, 335.0f }, // Upper clearing path
        {  360.0f, 665.0f }, // Road in front of shop
        {  500.0f, 640.0f }, // Lower road east of shop
        {  600.0f, 530.0f }, // Middle path bend
        {  740.0f, 440.0f }, // Forest crossroads
        {  980.0f, 490.0f }, // Arena western entrance
        { 1100.0f, 540.0f }, // Arena outer path
        { 1250.0f, 450.0f }, // Arena upper perimeter
        // 5 additional slimes at the Cyclops arena:
        { 1205.0f, 595.0f }, // Arena west flank
        { 1240.0f, 535.0f }, // Arena north flank
        { 1285.0f, 560.0f }, // Arena north-east flank
        { 1275.0f, 625.0f }, // Arena south-east flank
        { 1345.0f, 580.0f }  // Arena east ridge
    };

    for (int i = 0; i < 15; ++i) {
        Enemy s;
        s.id = 10 + i;
        s.type = ENEMY_SLIME;
        s.pos = slimeSpawns[i];
        s.spawnPos = s.pos;
        s.hp = 10;
        s.maxHp = 10;
        s.damage = 10;
        s.facing = (i % 2 == 0) ? 1 : -1;
        list.push_back(s);
    }

    return list;
}

static inline bool isInsideFloor1City(Vector2 p) {
    // City boundary on Map 1: village and city roads/roofs are at x <= 1350.0f
    return (p.x <= 1350.0f);
}

static void updateEnemies(std::vector<Enemy>& enemies, const WalkMask& walk, float dt, Vector2 playerPos,
                          int& playerHp, float& playerInvulnTimer, float& playerHurtFlash,
                          bool damageSoundLoaded, Sound damageSound, Vector2 defaultSpawn,
                          std::vector<FloatingText>& floatingTexts, int mapId = 1,
                          bool freezeActive = false, Vector2 freezeCenter = { 0.0f, 0.0f }, float freezeRadius = 0.0f) {
    for (auto& e : enemies) {
        if (e.isDead) continue;

        if (e.hitFlashTimer > 0.0f)   e.hitFlashTimer -= dt;

        if (e.state == ESTATE_DEAD) {
            e.deathTimer += dt;
            if (e.deathTimer >= 0.75f) {
                e.isDead = true;
            }
            continue;
        }

        // TIME STOP: everything near the player is frozen (no movement, animation or attacks)
        if (freezeActive && v2Dist(e.pos, freezeCenter) <= freezeRadius) continue;

        if (e.attackCooldown > 0.0f)  e.attackCooldown -= dt;

        float dist = v2Dist(e.pos, playerPos);
        float aggroRange  = (e.type == ENEMY_SLIME) ? 140.0f : ((e.type == ENEMY_GOLEM) ? 190.0f : ((e.type == ENEMY_CYCLOPS) ? 240.0f : 180.0f));
        float attackRange = (e.type == ENEMY_SLIME) ? 28.0f : ((e.type == ENEMY_GOLEM) ? 48.0f : ((e.type == ENEMY_CYCLOPS) ? 54.0f : 42.0f));
        float moveSpeed   = (e.type == ENEMY_SLIME) ? 42.0f : ((e.type == ENEMY_GOLEM) ? 38.0f : ((e.type == ENEMY_CYCLOPS) ? 50.0f : 46.0f));

        // Attack state hysteresis: only exit attack state if dist > attackRange * 1.35f
        bool inAttackZone = (e.state == ESTATE_ATTACK) ? (dist <= attackRange * 1.35f) : (dist <= attackRange);

        if (inAttackZone) {
            e.state = ESTATE_ATTACK;
            float animSpeed = (e.type == ENEMY_ORC) ? 9.0f : ((e.type == ENEMY_GOLEM) ? 7.0f : ((e.type == ENEMY_CYCLOPS) ? 7.5f : 9.0f));
            e.animT += dt * animSpeed;
            if (std::abs(playerPos.x - e.pos.x) > 6.0f) {
                e.facing = (playerPos.x >= e.pos.x) ? 1 : -1;
            }

            if (e.attackCooldown <= 0.0f) {
                e.attackCooldown = (e.type == ENEMY_SLIME) ? 1.15f : 1.35f;
                // Deal 10 damage to player
                if (playerInvulnTimer <= 0.0f) {
                    playerHp = std::max(0, playerHp - 10);
                    playerInvulnTimer = 0.65f;
                    playerHurtFlash = 0.25f;
                    if (damageSoundLoaded) PlaySound(damageSound);
                    floatingTexts.push_back({ { playerPos.x, playerPos.y - 30.0f }, "-10", Color{ 255, 60, 60, 255 }, 0.0f, 0.85f });

                    if (playerHp <= 0) {
                        playerHp = 100;
                        floatingTexts.push_back({ defaultSpawn, "RESPAWNED!", GOLD, 0.0f, 1.5f });
                    }
                }
            }
        } else if (dist <= aggroRange && !(mapId == 1 && isInsideFloor1City(playerPos))) {
            e.state = ESTATE_MOVE;
            float animSpeed = (e.type == ENEMY_ORC) ? 8.0f : ((e.type == ENEMY_GOLEM) ? 7.0f : ((e.type == ENEMY_CYCLOPS) ? 6.0f : 8.0f));
            e.animT += dt * animSpeed;
            Vector2 toPlayer = { playerPos.x - e.pos.x, playerPos.y - e.pos.y };
            float d = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
            if (d > 0.01f) {
                Vector2 moveStep = { (toPlayer.x / d) * moveSpeed * dt, (toPlayer.y / d) * moveSpeed * dt };
                if (std::abs(toPlayer.x) > 6.0f) {
                    e.facing = (toPlayer.x >= 0.0f) ? 1 : -1;
                }
                // Strictly constrain enemy movement to walkable path (and never enter city boundary on Floor 1)
                Vector2 nextX = { e.pos.x + moveStep.x, e.pos.y };
                if (moveStep.x != 0.0f && walk.canStand(nextX) && !(mapId == 1 && isInsideFloor1City(nextX))) {
                    e.pos.x += moveStep.x;
                }
                Vector2 nextY = { e.pos.x, e.pos.y + moveStep.y };
                if (moveStep.y != 0.0f && walk.canStand(nextY) && !(mapId == 1 && isInsideFloor1City(nextY))) {
                    e.pos.y += moveStep.y;
                }
            }
        } else {
            // Patrol / Return to spawn with hysteresis
            float dSpawn = v2Dist(e.pos, e.spawnPos);
            if (e.isReturning) {
                if (dSpawn <= 6.0f) {
                    e.isReturning = false;
                }
            } else {
                if (dSpawn > 25.0f) {
                    e.isReturning = true;
                }
            }

            if (e.isReturning) {
                e.state = ESTATE_MOVE;
                float animSpeed = (e.type == ENEMY_ORC) ? 8.0f : ((e.type == ENEMY_GOLEM) ? 7.0f : ((e.type == ENEMY_CYCLOPS) ? 6.0f : 8.0f));
                e.animT += dt * animSpeed;

                Vector2 toSpawn = { e.spawnPos.x - e.pos.x, e.spawnPos.y - e.pos.y };
                float d = std::sqrt(toSpawn.x * toSpawn.x + toSpawn.y * toSpawn.y);
                if (d > 0.01f) {
                    Vector2 step = { (toSpawn.x / d) * (moveSpeed * 0.5f) * dt, (toSpawn.y / d) * (moveSpeed * 0.5f) * dt };
                    if (std::abs(toSpawn.x) > 6.0f) {
                        e.facing = (toSpawn.x >= 0.0f) ? 1 : -1;
                    }
                    Vector2 nextX = { e.pos.x + step.x, e.pos.y };
                    if (step.x != 0.0f && walk.canStand(nextX) && !(mapId == 1 && isInsideFloor1City(nextX))) e.pos.x += step.x;
                    Vector2 nextY = { e.pos.x, e.pos.y + step.y };
                    if (step.y != 0.0f && walk.canStand(nextY) && !(mapId == 1 && isInsideFloor1City(nextY))) e.pos.y += step.y;
                }
            } else {
                e.state = ESTATE_IDLE;
                float animSpeed = (e.type == ENEMY_ORC) ? 6.0f : ((e.type == ENEMY_GOLEM) ? 5.0f : ((e.type == ENEMY_CYCLOPS) ? 5.0f : 6.0f));
                e.animT += dt * animSpeed;
            }
        }

        // Seamless state transition: reset animT on state changes to prevent frame popping
        if (e.state != e.prevState) {
            e.animT = 0.0f;
            e.prevState = e.state;
        }
    }
}

// ------------------------------------------------------------------
// Map Data & Multi-Map Transition System
// ------------------------------------------------------------------
struct MapData {
    int id = 1;
    std::string name;
    std::string subtitle;
    int w = 0, h = 0;
    Texture2D mapTex{};
    Texture2D maskTex{};
    Texture2D roofTex{};
    WalkMask walk;
    CritterGrassMask critterMask;
    std::vector<Critter> critters;
    std::vector<Enemy> enemies;
    std::vector<Rectangle> zones;
    FogMap fog;
    Texture2D fogTex{};
    std::vector<Color> fogUpload;
    Vector2 lastReveal = { -9999.0f, -9999.0f };
    float lastRevealRadius = -1.0f;
    Vector2 defaultSpawn = { 0.0f, 0.0f };
};

// Stamps a walkable circular corridor along the line between two points
static void stampPathCorridor(std::vector<uint8_t>& cell, std::vector<uint8_t>& behind, int w, int h,
                              int x0, int y0, int x1, int y1, int radius) {
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        for (int cy = std::max(0, y0 - radius); cy <= std::min(h - 1, y0 + radius); ++cy) {
            for (int cx = std::max(0, x0 - radius); cx <= std::min(w - 1, x0 + radius); ++cx) {
                if ((cx - x0) * (cx - x0) + (cy - y0) * (cy - y0) <= radius * radius) {
                    size_t idx = (size_t)cy * w + cx;
                    if (!cell[idx]) {
                        behind[idx] = 1;
                    }
                    cell[idx] = 1;
                }
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static WalkMask buildWalkMaskMap2(const Image& img, Vector2 spawn,
                                  std::vector<Color>& treePixels,
                                  std::vector<Rectangle>& zones,
                                  CritterGrassMask& critterMask) {
    WalkMask wm;
    wm.w = img.width;
    wm.h = img.height;
    wm.cell.assign((size_t)wm.w * wm.h, 0);
    wm.behind.assign((size_t)wm.w * wm.h, 0);

    Color* px = LoadImageColors(img);
    for (size_t i = 0; i < wm.cell.size(); ++i) {
        Vector3 hsv = ColorToHSV(px[i]);
        uint8_t r = px[i].r, g = px[i].g, b = px[i].b;
        // Warm brown / earthy dirt road in the deep forest
        bool isPath = (hsv.x >= 12.0f && hsv.x <= 52.0f &&
                       hsv.y >= 0.10f &&
                       hsv.z >= 0.18f && hsv.z <= 0.75f &&
                       r >= b && (r + 4 >= g));
        wm.cell[i] = isPath ? 1 : 0;
    }

    // Tree canopy and foliage coverage mask on Map 2
    std::vector<uint8_t> treeOcc((size_t)wm.w * wm.h, 0);
    for (size_t i = 0; i < treeOcc.size(); ++i) {
        Vector3 hsv = ColorToHSV(px[i]);
        if ((hsv.x >= 45.0f && hsv.x <= 210.0f) || (hsv.z < 0.22f)) {
            treeOcc[i] = 1;
        }
    }

    // 1) Bridge horizontal & vertical stretches of tree canopies that sit over the road
    std::vector<uint8_t> treeBridges(treeOcc.size(), 0);
    bridgeLines(treeOcc, wm.cell, treeBridges, wm.w, wm.h, true);
    bridgeLines(treeOcc, wm.cell, treeBridges, wm.w, wm.h, false);
    for (size_t i = 0; i < treeBridges.size(); ++i) {
        if (treeBridges[i] && !wm.cell[i]) {
            wm.behind[i] = 1;
            wm.cell[i] = 1;
        }
    }

    // 2) Connect all significant road pieces across tree canopies / foliage
    std::vector<int> areas;
    std::vector<int> lab = labelComponents(wm.cell, wm.w, wm.h, 1, areas);

    int sx = std::clamp((int)spawn.x, 0, wm.w - 1);
    int sy = std::clamp((int)spawn.y, 0, wm.h - 1);
    int spawnLabel = 0;
    for (int dy = -25; dy <= 25 && !spawnLabel; ++dy) {
        for (int dx = -25; dx <= 25; ++dx) {
            int nx = std::clamp(sx + dx, 0, wm.w - 1);
            int ny = std::clamp(sy + dy, 0, wm.h - 1);
            if (lab[(size_t)ny * wm.w + nx]) {
                spawnLabel = lab[(size_t)ny * wm.w + nx];
                break;
            }
        }
    }
    if (!spawnLabel) spawnLabel = 1;

    // Sample points for each component >= 300 px
    std::vector<std::vector<Vector2>> compSamples(areas.size());
    for (int y = 0; y < wm.h; y += 4) {
        for (int x = 0; x < wm.w; x += 4) {
            int l = lab[(size_t)y * wm.w + x];
            if (l > 0 && areas[l] >= 300) {
                compSamples[l].push_back({ (float)x, (float)y });
            }
        }
    }

    std::vector<int> connectedLabels = { spawnLabel };
    std::vector<bool> isConnected(areas.size(), false);
    isConnected[spawnLabel] = true;

    for (int pass = 0; pass < 12; ++pass) {
        bool addedAny = false;
        for (size_t l = 1; l < areas.size(); ++l) {
            if (isConnected[l] || areas[l] < 300 || compSamples[l].empty()) continue;

            float bestD = 999999.0f;
            Vector2 bestP1{}, bestP0{};

            for (int connL : connectedLabels) {
                const auto& ptsConn = compSamples[connL];
                for (const auto& p1 : compSamples[l]) {
                    for (const auto& p0 : ptsConn) {
                        float d = (p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y);
                        if (d < bestD) {
                            bestD = d;
                            bestP1 = p1;
                            bestP0 = p0;
                        }
                    }
                }
            }

            // Gaps between road components covered by trees are connected with wide walkable corridors
            if (bestD <= 140.0f * 140.0f) {
                stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h,
                                  (int)bestP0.x, (int)bestP0.y,
                                  (int)bestP1.x, (int)bestP1.y, 13);
                isConnected[l] = true;
                connectedLabels.push_back((int)l);
                addedAny = true;
            }
        }
        if (!addedAny) break;
    }

    // 2.5) Ensure the walking path across the marketplace and past tree foliage is fully connected and wide
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 150, 585, 220, 615, 15);
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 220, 615, 330, 655, 15);
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 330, 655, 430, 660, 15);
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 385, 655, 385, 636, 14); // Connector to Market Store entrance
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 430, 660, 520, 610, 15);
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 520, 610, 600, 540, 15);
    // Floor 4 portal at the east end of the Cyclops arena (guaranteed walkable + connected to the arena centre)
    stampPathCorridor(wm.cell, wm.behind, wm.w, wm.h, 1250, 585, (int)FLOOR4_PORTAL.x, (int)FLOOR4_PORTAL.y, 16);

    // 3) Morphological close & hole fill
    closeMask(wm.cell, wm.w, wm.h, 4);
    fillSmallHoles(wm.cell, wm.w, wm.h, 400);

    // Keep all large connected pieces
    if (!keepLargePieces(wm.cell, wm.w, wm.h, spawn)) {
        TraceLog(LOG_WARNING, "Map2: no walkable path found near spawn!");
    }

    // 3.5) Carve out shop cabin interior, counters, and roof so they cannot be stepped on, leaving the walking path in front fully open
    for (int y = 465; y <= 598; ++y) {
        for (int x = 250; x <= 432; ++x) {
            size_t idx = (size_t)y * wm.w + x;
            wm.cell[idx] = 0;
            wm.behind[idx] = 0;
        }
    }
    // Wooden fence to the left of the cabin (y: 510..585, x: 200..245)
    for (int y = 510; y <= 585; ++y) {
        int fx = (int)(200 + (y - 510) * 45.0f / 75.0f);
        for (int x = std::max(0, fx - 5); x <= std::min(wm.w - 1, fx + 5); ++x) {
            size_t idx = (size_t)y * wm.w + x;
            wm.cell[idx] = 0;
            wm.behind[idx] = 0;
        }
    }
    // Campsite tent & campfire
    for (int y = 220; y <= 295; ++y) {
        for (int x = 140; x <= 205; ++x) {
            size_t idx = (size_t)y * wm.w + x;
            wm.cell[idx] = 0;
            wm.behind[idx] = 0;
        }
    }

    // 4) On Map 2, player always appears at the top of all overlapping trees/obstacles
    zones.clear();
    treePixels.assign((size_t)wm.w * wm.h, Color{ 0, 0, 0, 0 });

    // 5) Forest clearings for wildlife critters outside the walking path
    critterMask.w = wm.w;
    critterMask.h = wm.h;
    critterMask.grid.assign((size_t)wm.w * wm.h, 0);

    std::vector<uint8_t> rawForest((size_t)wm.w * wm.h, 0);
    for (int y = 0; y < wm.h; ++y) {
        for (int x = 0; x < wm.w; ++x) {
            size_t i = (size_t)y * wm.w + x;
            if (x < 40 || x >= wm.w - 40 || y < 40 || y >= wm.h - 40) continue;
            if (wm.cell[i]) continue; // strictly not on the walking path

            Vector3 hsv = ColorToHSV(px[i]);
            // Forest floor / clearing green
            if (hsv.x >= 55.0f && hsv.x <= 155.0f && hsv.y >= 0.18f && hsv.z >= 0.18f && hsv.z <= 0.65f && px[i].g > px[i].r) {
                rawForest[i] = 1;
            }
        }
    }

    std::vector<uint8_t> erodedForest(rawForest.size(), 0);
    boxPass(rawForest, erodedForest, wm.w, wm.h, 5, true, false);
    boxPass(erodedForest, critterMask.grid, wm.w, wm.h, 5, false, false);

    critterMask.validCoords.clear();
    for (int y = 40; y < wm.h - 40; y += 8) {
        for (int x = 40; x < wm.w - 40; x += 8) {
            if (critterMask.isValid(x, y)) {
                critterMask.validCoords.push_back({ (float)x, (float)y });
            }
        }
    }

    UnloadImageColors(px);
    return wm;
}

// Floor 4 walkable mask: central arena ellipse + bottom exit corridor, minus the braziers
static WalkMask buildWalkMaskFloor4(int w, int h) {
    WalkMask wm;
    wm.w = w; wm.h = h;
    wm.cell.assign((size_t)w * h, 0);
    wm.behind.assign((size_t)w * h, 0);
    const float cx = 512.0f, cy = 300.0f, rx = 345.0f, ry = 165.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float nx = (x - cx) / rx, ny = (y - cy) / ry;
            bool inside = (nx * nx + ny * ny <= 1.0f);
            bool corridor = (x >= 462 && x <= 562 && y >= 440);
            if (inside || corridor) wm.cell[(size_t)y * w + x] = 1;
        }
    }
    const Vector2 braziers[8] = {
        { 513, 218 }, { 388, 258 }, { 638, 258 }, { 349, 340 },
        { 678, 340 }, { 390, 420 }, { 638, 420 }, { 515, 462 }
    };
    for (const Vector2& b : braziers) {
        for (int y = (int)b.y - 16; y <= (int)b.y + 16; ++y)
            for (int x = (int)b.x - 16; x <= (int)b.x + 16; ++x) {
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                if ((x - b.x) * (x - b.x) + (y - b.y) * (y - b.y) <= 16.0f * 16.0f)
                    wm.cell[(size_t)y * w + x] = 0;
            }
    }
    return wm;
}

enum TransitionState {
    TRANS_IDLE = 0,
    TRANS_FADE_OUT,
    TRANS_FADE_IN
};

struct MapTransition {
    TransitionState state = TRANS_IDLE;
    float timer = 0.0f;
    float duration = 0.45f;
    float alpha = 0.0f;
    int targetMapId = 1;
    Vector2 targetPos = { 0.0f, 0.0f };
    Direction targetFacing = DIR_RIGHT;
    const char* title = "";
    const char* subtitle = "";
    const char* tip = "";
};

struct ShadowBolt {
    bool active = false;
    Vector2 from{ 0, 0 }, to{ 0, 0 }, pos{ 0, 0 }, dir{ 1, 0 };
    float travelled = 0.0f, total = 0.0f;
};
struct ShadowPuff { Vector2 p; float t = 0.0f; };

// ------------------------------------------------------------------
int main() {
    wp::GameSettings settings = wp::LoadSettings();

    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (settings.vsync) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);

    InitWindow(1280, 720, "Path Explorer - Two Worlds");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);
    SetTargetFPS(settings.vsync ? 60 : 144);

    // ---- audio ----
    InitAudioDevice();

    wp::MainMenu menu;
    menu.Init(&settings, "assets/menu");

    AppState state = AppState::Menu;
    bool quit = false;

    const char* bgmCandidates[] = {
        "GAMEEE/origin_bg.mp3",
        "GAMEE/origin_bg.mp3",
        "assets/origin_bg.mp3",
        "origin_bg.mp3"
    };
    const char* bgmPath = nullptr;
    for (const char* c : bgmCandidates) if (FileExists(c)) { bgmPath = c; break; }

    Music bgm{};
    bool bgmLoaded = false;
    bool bgmMuted = false;
    float bgmVolume = 0.20f; // default background music volume
    if (bgmPath) {
        bgm = LoadMusicStream(bgmPath);
        bgm.looping = true;
        SetMusicVolume(bgm, bgmVolume * settings.music);
        bgmLoaded = true;
    }

    // ---- walk & run sound ----
    const char* walkCandidates[] = {
        "GAMEEE/walking + running.mp3",
        "GAMEE/walking + running.mp3",
        "assets/walking + running.mp3",
        "walking + running.mp3"
    };
    const char* walkPath = nullptr;
    for (const char* c : walkCandidates) if (FileExists(c)) { walkPath = c; break; }

    Sound walkSound{};
    bool walkSoundLoaded = false;
    if (walkPath) {
        walkSound = LoadSound(walkPath);
        SetSoundVolume(walkSound, 0.50f);
        walkSoundLoaded = true;
    }
    float stepSoundTimer = 0.0f;

    // ---- sword swing sound ----
    const char* swingSoundCandidates[] = {
        "GAMEEE/sword_swing.mp3",
        "GAMEE/sword_swing.mp3",
        "assets/sword_swing.mp3",
        "sword_swing.mp3"
    };
    const char* swingSoundPath = nullptr;
    for (const char* c : swingSoundCandidates) if (FileExists(c)) { swingSoundPath = c; break; }

    Sound swingSound{};
    bool swingSoundLoaded = false;
    if (swingSoundPath) {
        swingSound = LoadSound(swingSoundPath);
        SetSoundVolume(swingSound, 0.65f);
        swingSoundLoaded = true;
    }

    // ---- candidate maps ----
    const char* map1Candidates[] = { "assets/map.png", "map.png", "assets/map.jpg", "map.jpg" };
    const char* map1Path = nullptr;
    for (const char* c : map1Candidates) if (FileExists(c)) { map1Path = c; break; }

    const char* map2Candidates[] = { "assets/map2.png", "map2.png", "assets/map2.jpg", "map2.jpg" };
    const char* map2Path = nullptr;
    for (const char* c : map2Candidates) if (FileExists(c)) { map2Path = c; break; }

    if (!map1Path) {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(Color{ 30, 20, 20, 255 });
            DrawText("Map 1 image not found!", 40, 40, 32, RED);
            DrawText("Put your map at  assets/map.png  (next to the executable).", 40, 90, 22, RAYWHITE);
            DrawText(TextFormat("Working dir: %s", GetWorkingDirectory()), 40, 130, 18, GRAY);
            EndDrawing();
        }
        if (bgmLoaded) {
            StopMusicStream(bgm);
            UnloadMusicStream(bgm);
        }
        CloseAudioDevice();
        CloseWindow();
        return 1;
    }

    // loading screen (building the masks takes a moment)
    BeginDrawing();
    ClearBackground(Color{ 18, 26, 22, 255 });
    DrawText("Generating World Maps & Navigational Paths...", 40, 40, 28, RAYWHITE);
    DrawText("Map 1 (Village Outskirts) & Map 2 (The Deep Wild Forest)", 40, 85, 20, GOLD);
    EndDrawing();

    // ---- Load Map 1 ----
    MapData map1;
    map1.id = 1;
    map1.name = "Village Outskirts";
    map1.subtitle = "Map 1";

    Image map1Img = LoadImage(map1Path);
    map1.w = map1Img.width;
    map1.h = map1Img.height;
    map1.defaultSpawn = { map1.w * SPAWN_FX, map1.h * SPAWN_FY };

    PathColorRule rule;
    RoofColorRule roofRule;
    std::vector<Color> roofPixels;
    map1.walk = buildWalkMask(map1Img, rule, roofRule, map1.defaultSpawn, roofPixels, map1.zones, map1.critterMask);

    map1.mapTex = LoadTextureFromImage(map1Img);
    SetTextureFilter(map1.mapTex, TEXTURE_FILTER_POINT);
    UnloadImage(map1Img);

    {
        Image im{ roofPixels.data(), map1.w, map1.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        map1.roofTex = LoadTextureFromImage(im);
        SetTextureFilter(map1.roofTex, TEXTURE_FILTER_POINT);
    }
    roofPixels.clear();
    roofPixels.shrink_to_fit();

    // Debug overlay texture for Map 1
    {
        std::vector<Color> ov((size_t)map1.w * map1.h);
        for (size_t i = 0; i < ov.size(); ++i) {
            if (map1.walk.behind[i])    ov[i] = Color{ 255, 40, 255, 130 };
            else if (map1.walk.cell[i]) ov[i] = Color{ 40, 255, 90, 110 };
            else                        ov[i] = Color{ 0, 0, 0, 0 };
        }
        Image im{ ov.data(), map1.w, map1.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        map1.maskTex = LoadTextureFromImage(im);
    }

    map1.fog.init(map1.w, map1.h);
    {
        Image im{ map1.fog.px.data(), map1.fog.w, map1.fog.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        map1.fogTex = LoadTextureFromImage(im);
        SetTextureFilter(map1.fogTex, TEXTURE_FILTER_BILINEAR);
    }

    map1.critters = initCritters(map1.critterMask, 14);
    map1.enemies = initEnemiesMap1();

    // ---- Load Map 2 (if present) ----
    MapData map2;
    bool map2Ready = false;
    if (map2Path) {
        map2.id = 2;
        map2.name = "The Deep Wild Forest";
        map2.subtitle = "Map 2 (Walking Path Only)";

        Image map2Img = LoadImage(map2Path);
        map2.w = map2Img.width;
        map2.h = map2Img.height;
        map2.defaultSpawn = { 50.0f, 575.0f };

        std::vector<Color> treePixels;
        map2.walk = buildWalkMaskMap2(map2Img, map2.defaultSpawn, treePixels, map2.zones, map2.critterMask);

        map2.mapTex = LoadTextureFromImage(map2Img);
        SetTextureFilter(map2.mapTex, TEXTURE_FILTER_POINT);
        UnloadImage(map2Img);

        {
            Image im{ treePixels.data(), map2.w, map2.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            map2.roofTex = LoadTextureFromImage(im);
            SetTextureFilter(map2.roofTex, TEXTURE_FILTER_POINT);
        }
        treePixels.clear();
        treePixels.shrink_to_fit();

        // Debug overlay texture for Map 2 (green = open path, magenta = path under tree canopy)
        {
            std::vector<Color> ov((size_t)map2.w * map2.h);
            for (size_t i = 0; i < ov.size(); ++i) {
                if (map2.walk.behind[i])    ov[i] = Color{ 255, 40, 255, 130 };
                else if (map2.walk.cell[i]) ov[i] = Color{ 40, 255, 90, 110 };
                else                        ov[i] = Color{ 0, 0, 0, 0 };
            }
            Image im{ ov.data(), map2.w, map2.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            map2.maskTex = LoadTextureFromImage(im);
        }

        map2.fog.init(map2.w, map2.h);
        {
            Image im{ map2.fog.px.data(), map2.fog.w, map2.fog.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            map2.fogTex = LoadTextureFromImage(im);
            SetTextureFilter(map2.fogTex, TEXTURE_FILTER_BILINEAR);
        }

        map2.critters = initCritters(map2.critterMask, 8);
        map2.enemies = initEnemiesMap2();
        map2Ready = true;
    }

    // ---- Load Floor 4 (map4.png): the Pentagram Arena ----
    MapData map4;
    bool map4Ready = false;
    {
        const char* map4Candidates[] = { "assets/map4.png", "map4.png", "../assets/map4.png" };
        const char* map4Path = nullptr;
        for (const char* c : map4Candidates) if (FileExists(c)) { map4Path = c; break; }
        if (map4Path) {
            map4.id = 4;
            map4.name = "Floor 4: The Pentagram Arena";
            map4.subtitle = "Floor 4";
            Image img4 = LoadImage(map4Path);
            map4.w = img4.width;
            map4.h = img4.height;
            map4.defaultSpawn = FLOOR4_SPAWN;
            map4.walk = buildWalkMaskFloor4(map4.w, map4.h);
            map4.mapTex = LoadTextureFromImage(img4);
            SetTextureFilter(map4.mapTex, TEXTURE_FILTER_POINT);
            UnloadImage(img4);
            {
                std::vector<Color> ov((size_t)map4.w * map4.h);
                for (size_t i = 0; i < ov.size(); ++i)
                    ov[i] = map4.walk.cell[i] ? Color{ 40, 255, 90, 110 } : Color{ 0, 0, 0, 0 };
                Image im{ ov.data(), map4.w, map4.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                map4.maskTex = LoadTextureFromImage(im);
            }
            map4.fog.init(map4.w, map4.h);
            {
                Image im{ map4.fog.px.data(), map4.fog.w, map4.fog.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                map4.fogTex = LoadTextureFromImage(im);
                SetTextureFilter(map4.fogTex, TEXTURE_FILTER_BILINEAR);
            }
            map4.enemies = initEnemiesFloor4();
            map4Ready = true;
        }
    }

    // ---- Load Market Store Interior (store1.jpg & store2.jpg) ----
    const char* store1Candidates[] = { "store/store1.jpg", "../store/store1.jpg", "assets/store/store1.jpg", "assets/store1.jpg" };
    const char* store2Candidates[] = { "store/store2.jpg", "../store/store2.jpg", "assets/store/store2.jpg", "assets/store2.jpg" };
    Texture2D store1Tex{};
    Texture2D store2Tex{};
    for (const char* c : store1Candidates) {
        if (FileExists(c)) {
            store1Tex = LoadTexture(c);
            if (store1Tex.id != 0) {
                SetTextureFilter(store1Tex, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
    }
    for (const char* c : store2Candidates) {
        if (FileExists(c)) {
            store2Tex = LoadTexture(c);
            if (store2Tex.id != 0) {
                SetTextureFilter(store2Tex, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
    }
    bool storeReady = (store1Tex.id != 0 && store2Tex.id != 0);

    // Initialize Shop system from shop.h
    Shop shop;
    shop.gold = 500;
    shop.Add("Iron Sword", "A sturdy adventurer blade.", 120, "assets/items/sword.png");
    shop.Add("Health Potion", "Restores +35 HP immediately.", 35, "assets/items/potion.png");
    shop.Add("Greater Healing Elixir", "Potent brew that restores +80 HP.", 75, "assets/items/elixir.png");
    shop.Add("Rare Alchemical Herb", "A pungent forest blossom sought by master alchemists.", 50, "assets/items/herb.png");
    shop.Add("Flame Ember Edge", "Enchanted blade with roaring fire.", 180, "GAMEEE/fire_swing.png");
    shop.Add("Shadow Slicer", "Blade cloaked in swift shadow magic.", 220, "GAMEEE/shadow_skill.png");
    shop.LoadTextures(true);

    // ---- base avatar sprites ----
    const char* downCandidates[]  = { "GAMEEE/down.png", "GAMEE/down.png", "assets/down.png", "down.png" };
    const char* upCandidates[]    = { "GAMEEE/up.png", "GAMEE/up.png", "assets/up.png", "up.png" };
    const char* sideCandidates[]  = { "GAMEEE/left_right.png", "GAMEE/left_right.png", "assets/left_right.png", "left_right.png" };

    // ---- sword swing candidates for all 4 loadouts ----
    const char* swingBaseCandidates[] = {
        "GAMEEE/base_sword_swing.png",
        "GAMEEE/base_sword_swing (1).png",
        "GAMEEE/swinging.png",
        "GAMEE/swinging.png",
        "assets/swinging.png"
    };
    const char* swingShadowCandidates[] = {
        "GAMEEE/shadow_swinging.png",
        "GAMEE/shadow_swinging.png",
        "assets/shadow_swinging.png"
    };
    const char* swingFireCandidates[] = {
        "GAMEEE/fire_swing.png",
        "GAMEE/fire_swing.png",
        "assets/fire_swing.png"
    };
    const char* swingTimeCandidates[] = {
        "GAMEEE/time_swing.png",
        "GAMEE/time_swing.png",
        "assets/time_swing.png"
    };

    // ---- time player walk/idle sprites ----
    const char* timeUpDownCandidates[] = {
        "GAMEEE/time_sword_up_down.png",
        "GAMEE/time_sword_up_down.png",
        "assets/time_sword_up_down.png"
    };
    const char* timeSideCandidates[] = {
        "GAMEEE/time_sword_left_right.png",
        "GAMEE/time_sword_left_right.png",
        "assets/time_sword_left_right.png"
    };

    const char* downPath        = nullptr;
    const char* upPath          = nullptr;
    const char* sidePath        = nullptr;
    const char* swingBasePath   = nullptr;
    const char* swingShadowPath = nullptr;
    const char* swingFirePath   = nullptr;
    const char* swingTimePath   = nullptr;
    const char* timeUpDownPath  = nullptr;
    const char* timeSidePath    = nullptr;

    for (const char* c : downCandidates)        if (FileExists(c)) { downPath        = c; break; }
    for (const char* c : upCandidates)          if (FileExists(c)) { upPath          = c; break; }
    for (const char* c : sideCandidates)        if (FileExists(c)) { sidePath        = c; break; }
    for (const char* c : swingBaseCandidates)   if (FileExists(c)) { swingBasePath   = c; break; }
    for (const char* c : swingShadowCandidates) if (FileExists(c)) { swingShadowPath = c; break; }
    for (const char* c : swingFireCandidates)   if (FileExists(c)) { swingFirePath   = c; break; }
    for (const char* c : swingTimeCandidates)   if (FileExists(c)) { swingTimePath   = c; break; }
    for (const char* c : timeUpDownCandidates)  if (FileExists(c)) { timeUpDownPath  = c; break; }
    for (const char* c : timeSideCandidates)    if (FileExists(c)) { timeSidePath    = c; break; }

    AvatarRenderer avatar;
    avatar.load(downPath, upPath, sidePath,
                swingBasePath, swingShadowPath, swingFirePath, swingTimePath,
                timeUpDownPath, timeSidePath);

    CritterAssetManager critterAssets;
    critterAssets.load();

    EnemyAssetManager enemyAssets;
    enemyAssets.load();

    // ---- damage sound ----
    const char* damageCandidates[] = {
        "GAMEEE/damage_sound .mp3",
        "GAMEEE/damage_sound.mp3",
        "assets/damage_sound .mp3",
        "assets/damage_sound.mp3"
    };
    const char* damagePath = nullptr;
    for (const char* c : damageCandidates) if (FileExists(c)) { damagePath = c; break; }

    Sound damageSound{};
    bool damageSoundLoaded = false;
    if (damagePath) {
        damageSound = LoadSound(damagePath);
        SetSoundVolume(damageSound, 0.70f);
        damageSoundLoaded = true;
    }

    // ---- combat & player stats ----
    int playerHp = 100;
    int playerMaxHp = 100;
    float playerInvulnTimer = 0.0f;
    float playerHurtFlash = 0.0f;
    bool swingDealtDamage = false;
    std::vector<FloatingText> floatingTexts;

    // ---- state ----
    PlayerLoadout curLoadout = LOADOUT_BASE;
    int activeMapId = 1;
    Vector2 pos = map1.defaultSpawn;
    int facing = 1;
    Direction facingDir = DIR_DOWN;
    float animT = 0.0f;
    float swingTimer = 0.0f;
    bool showMask = false, showMini = true, showHud = true;
    int fogMode = FOG_FULL;
    float radius = RADIUS_START;

    MapTransition trans;

    Camera2D cam{};
    cam.zoom = 3.0f;
    cam.target = pos;

    // ---- Wire Shop Item Purchases (shop.h) ----
    shop.onBuy = [&](int index) {
        if (index >= 0 && index < (int)shop.items.size()) {
            const std::string& iname = shop.items[index].name;
            if (iname == "Health Potion") {
                int oldHp = playerHp;
                playerHp = std::min(playerMaxHp, playerHp + 35);
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, TextFormat("+%d HP", playerHp - oldHp), GREEN, 0.0f, 1.2f });
            } else if (iname == "Greater Healing Elixir") {
                int oldHp = playerHp;
                playerHp = std::min(playerMaxHp, playerHp + 80);
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, TextFormat("+%d HP", playerHp - oldHp), GREEN, 0.0f, 1.2f });
            } else if (iname == "Iron Sword") {
                curLoadout = LOADOUT_BASE;
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, "Equipped Steel Blade!", SKYBLUE, 0.0f, 1.2f });
            } else if (iname == "Flame Ember Edge") {
                curLoadout = LOADOUT_FIRE;
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, "Equipped Flame Blade!", ORANGE, 0.0f, 1.2f });
            } else if (iname == "Shadow Slicer") {
                curLoadout = LOADOUT_SHADOW;
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, "Equipped Shadow Blade!", PURPLE, 0.0f, 1.2f });
            } else if (iname == "Rare Alchemical Herb") {
                floatingTexts.push_back({ { pos.x, pos.y - 32.0f }, "Acquired Alchemical Herb!", GOLD, 0.0f, 1.2f });
            }
        }
    };

    // ---- Logo + Intro video (shown after pressing Start) ----
    const float LOGO_DURATION = 4.0f;
    Texture2D logoTex{};
    for (const char* c : { "logo.jpg", "../logo.jpg", "assets/logo.jpg" })
        if (FileExists(c)) { logoTex = LoadTexture(c); if (logoTex.id) { SetTextureFilter(logoTex, TEXTURE_FILTER_BILINEAR); break; } }
    float logoTimer = 0.0f;

    plm_t* introPlm = nullptr;
    Texture2D introTex{};
    AudioStream introAudio{};
    std::vector<float> introAudioQueue;
    std::vector<unsigned char> introRGB;
    bool introFrameReady = false;
    auto closeIntro = [&]() {
        if (introPlm) { plm_destroy(introPlm); introPlm = nullptr; }
        if (introAudio.buffer) { StopAudioStream(introAudio); UnloadAudioStream(introAudio); introAudio = AudioStream{}; }
        if (introTex.id) { UnloadTexture(introTex); introTex = Texture2D{}; }
        introAudioQueue.clear();
    };
    auto openIntro = [&]() -> bool {
        const char* path = nullptr;
        for (const char* c : { "intro.mpg", "../intro.mpg", "assets/intro.mpg" }) if (FileExists(c)) { path = c; break; }
        if (!path) return false;
        introPlm = plm_create_with_filename(path);
        if (!introPlm) return false;
        plm_set_loop(introPlm, false);
        int iw = plm_get_width(introPlm), ih = plm_get_height(introPlm);
        introRGB.assign((size_t)iw * ih * 3, 0);
        Image img{ introRGB.data(), iw, ih, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
        introTex = LoadTextureFromImage(img);
        SetTextureFilter(introTex, TEXTURE_FILTER_BILINEAR);
        static std::pair<std::vector<unsigned char>*, bool*> videoUser;
        videoUser = { &introRGB, &introFrameReady };
        plm_set_video_decode_callback(introPlm, [](plm_t*, plm_frame_t* frame, void* user) {
            auto* self = (std::pair<std::vector<unsigned char>*, bool*>*)user;
            plm_frame_to_rgb(frame, self->first->data(), frame->width * 3);
            *self->second = true;
        }, &videoUser);
        if (plm_get_num_audio_streams(introPlm) > 0) {
            plm_set_audio_enabled(introPlm, true);
            plm_set_audio_stream(introPlm, 0);
            int rate = plm_get_samplerate(introPlm);
            SetAudioStreamBufferSizeDefault(PLM_AUDIO_SAMPLES_PER_FRAME);
            introAudio = LoadAudioStream((unsigned)rate, 32, 2);
            SetAudioStreamBufferSizeDefault(0);
            plm_set_audio_lead_time(introPlm, (double)PLM_AUDIO_SAMPLES_PER_FRAME * 3.0 / rate);
            static std::vector<float>* audioUser;
            audioUser = &introAudioQueue;
            plm_set_audio_decode_callback(introPlm, [](plm_t*, plm_samples_t* samples, void* user) {
                auto* q = (std::vector<float>*)user;
                q->insert(q->end(), samples->interleaved, samples->interleaved + samples->count * 2);
            }, audioUser);
            PlayAudioStream(introAudio);
        }
        introFrameReady = false;
        return true;
    };
    auto drawFitted = [](Texture2D t, float alpha) {
        float sw2 = (float)GetScreenWidth(), sh2 = (float)GetScreenHeight();
        float sc = std::min(sw2 / t.width, sh2 / t.height);
        float dw = t.width * sc, dh = t.height * sc;
        DrawTexturePro(t, { 0, 0, (float)t.width, (float)t.height },
                       { (sw2 - dw) * 0.5f, (sh2 - dh) * 0.5f, dw, dh }, { 0, 0 }, 0.0f, ColorAlpha(WHITE, alpha));
    };
    auto beginGame = [&]() {
        state = AppState::Game;
        if (bgmLoaded && !bgmMuted) {
            SetMusicVolume(bgm, bgmVolume * settings.music);
            PlayMusicStream(bgm);
        }
    };

    // Store view: zoom out so the whole shop interior fits on screen
    float outdoorZoom = 3.0f;
    bool wasInStore = false;

    // ---- Special abilities ----
    //  [T]              Time Stop  : 5 s, freezes every enemy / animal near the player (not the player)
    //  [Q] / Right-Click Shadow Step: throw a shadow at the cursor and teleport to where it lands
    const float TIME_STOP_DURATION = 5.0f;
    const float TIME_STOP_COOLDOWN = 15.0f;   // counts down while the stop is running + after
    const float TIME_STOP_RADIUS   = 280.0f;
    const float SHADOW_RANGE       = 300.0f;
    const float SHADOW_SPEED       = 760.0f;
    const float SHADOW_COOLDOWN    = 3.0f;
    float timeStopTimer = 0.0f, timeStopCooldown = 0.0f, shadowCooldown = 0.0f;
    ShadowBolt shadowBolt;
    std::vector<ShadowPuff> shadowPuffs;

    auto drawTimeStopZone = [&]() {
        if (timeStopTimer <= 0.0f) return;
        float t = (float)GetTime();
        float fade = std::min(1.0f, std::min(timeStopTimer / 0.5f, (TIME_STOP_DURATION - timeStopTimer) / 0.25f + 0.05f));
        int cx = (int)std::round(pos.x), cy = (int)std::round(pos.y);
        DrawCircleGradient(cx, cy, TIME_STOP_RADIUS, ColorAlpha(Color{ 90, 170, 255, 255 }, 0.04f * fade),
                           ColorAlpha(Color{ 60, 120, 255, 255 }, 0.22f * fade));
        float pulse = 2.0f * std::sin(t * 5.0f);
        DrawRing({ (float)cx, (float)cy }, TIME_STOP_RADIUS - 3.0f + pulse, TIME_STOP_RADIUS + pulse, 0.0f, 360.0f, 72,
                 ColorAlpha(Color{ 150, 215, 255, 255 }, 0.65f * fade));
        for (int i = 0; i < 12; ++i) {
            float a = i * (PI / 6.0f);
            float r0 = TIME_STOP_RADIUS - 16.0f, r1 = TIME_STOP_RADIUS - 5.0f;
            DrawLineEx({ cx + std::cos(a) * r0, cy + std::sin(a) * r0 }, { cx + std::cos(a) * r1, cy + std::sin(a) * r1 },
                       2.0f, ColorAlpha(Color{ 255, 225, 120, 255 }, 0.8f * fade));
        }
        // clock hand sweeping around the zone (completes one turn over the full duration)
        float ang = -PI * 0.5f + (1.0f - timeStopTimer / TIME_STOP_DURATION) * 2.0f * PI;
        DrawLineEx({ (float)cx, (float)cy }, { cx + std::cos(ang) * (TIME_STOP_RADIUS - 12.0f), cy + std::sin(ang) * (TIME_STOP_RADIUS - 12.0f) },
                   2.0f, ColorAlpha(Color{ 255, 240, 170, 255 }, 0.55f * fade));
        DrawCircleLines(cx, cy, 14.0f + 3.0f * std::sin(t * 6.0f), ColorAlpha(Color{ 120, 230, 255, 255 }, 0.7f * fade));
    };

    auto drawShadowFx = [&]() {
        for (const auto& pf : shadowPuffs) {
            float k = std::clamp(pf.t / 0.55f, 0.0f, 1.0f);
            float a = 1.0f - k;
            DrawCircleGradient((int)pf.p.x, (int)(pf.p.y - 14.0f), 10.0f + 26.0f * k,
                               ColorAlpha(Color{ 190, 110, 255, 255 }, 0.55f * a), ColorAlpha(Color{ 40, 0, 70, 255 }, 0.0f));
            DrawEllipseLines((int)pf.p.x, (int)pf.p.y, 8.0f + 30.0f * k, 3.0f + 11.0f * k, ColorAlpha(Color{ 215, 150, 255, 255 }, a));
            for (int i = 0; i < 8; ++i) {
                float ang = i * (PI / 4.0f) + k * 1.5f;
                float r = 6.0f + 34.0f * k;
                DrawCircle((int)(pf.p.x + std::cos(ang) * r), (int)(pf.p.y - 14.0f + std::sin(ang) * r * 0.7f),
                           2.4f * a + 0.4f, ColorAlpha(Color{ 170, 90, 240, 255 }, a));
            }
        }
        if (shadowBolt.active) {
            DrawEllipseLines((int)shadowBolt.to.x, (int)shadowBolt.to.y, 13.0f, 5.0f, ColorAlpha(Color{ 215, 150, 255, 255 }, 0.8f));
            DrawEllipse((int)shadowBolt.to.x, (int)shadowBolt.to.y, 8.0f, 3.0f, Color{ 90, 30, 140, 120 });
            for (int i = 6; i >= 1; --i) {
                float tx = shadowBolt.pos.x - shadowBolt.dir.x * i * 8.0f;
                float ty = shadowBolt.pos.y - shadowBolt.dir.y * i * 8.0f - 18.0f;
                DrawCircle((int)tx, (int)ty, 6.0f - i * 0.7f, ColorAlpha(Color{ 120, 50, 200, 255 }, 0.12f * (7 - i)));
            }
            DrawCircleGradient((int)shadowBolt.pos.x, (int)(shadowBolt.pos.y - 18.0f), 15.0f,
                               ColorAlpha(Color{ 210, 140, 255, 255 }, 0.9f), ColorAlpha(Color{ 50, 0, 90, 255 }, 0.0f));
            DrawCircle((int)shadowBolt.pos.x, (int)(shadowBolt.pos.y - 18.0f), 4.5f, Color{ 25, 5, 45, 255 });
            DrawCircleLines((int)shadowBolt.pos.x, (int)(shadowBolt.pos.y - 18.0f), 5.5f, Color{ 230, 190, 255, 255 });
        }
    };

    while (!quit && !WindowShouldClose()) {
        float dt = GetFrameTime();

        if (state == AppState::Menu) {
            menu.Update(dt);
            menu.Draw();

            if (menu.action == wp::MenuAction::StartGame) {
                menu.StopMusic();
                logoTimer = 0.0f;
                state = AppState::Logo;
            } else if (menu.action == wp::MenuAction::Quit) {
                quit = true;
            }
        } else if (state == AppState::Logo) {
            logoTimer += dt;
            float a = 1.0f;
            if (logoTimer < 0.5f) a = logoTimer / 0.5f;
            else if (logoTimer > LOGO_DURATION - 0.5f) a = std::max(0.0f, (LOGO_DURATION - logoTimer) / 0.5f);
            BeginDrawing();
            ClearBackground(BLACK);
            if (logoTex.id) drawFitted(logoTex, a);
            EndDrawing();
            if (logoTimer >= LOGO_DURATION) {
                if (openIntro()) state = AppState::Intro;
                else beginGame();
            }
        } else if (state == AppState::Intro) {
            double vdt = std::min((double)dt, 0.1);
            plm_decode(introPlm, vdt);
            if (introAudio.buffer) {
                const size_t chunk = PLM_AUDIO_SAMPLES_PER_FRAME * 2;
                while (introAudioQueue.size() >= chunk && IsAudioStreamProcessed(introAudio)) {
                    UpdateAudioStream(introAudio, introAudioQueue.data(), PLM_AUDIO_SAMPLES_PER_FRAME);
                    introAudioQueue.erase(introAudioQueue.begin(), introAudioQueue.begin() + chunk);
                }
            }
            if (introFrameReady) { UpdateTexture(introTex, introRGB.data()); introFrameReady = false; }
            BeginDrawing();
            ClearBackground(BLACK);
            if (introTex.id) drawFitted(introTex, 1.0f);
            DrawText("Press SPACE / ENTER to skip", 16, GetScreenHeight() - 28, 16, Color{ 255, 255, 255, 120 });
            EndDrawing();
            bool skip = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            if (skip || plm_has_ended(introPlm)) {
                closeIntro();
                beginGame();
            }
        } else {
            // ---- In-Game Mode ----
            bool inStore = (activeMapId == 3);
            g_avatarScale = inStore ? 2.2f : 1.0f; // bigger player inside the store
            bool nearAlchemist = (inStore && pos.y <= 590.0f && pos.x >= 490.0f && pos.x <= 680.0f);

            if (shop.open) {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    shop.Close();
                    continue;
                }
            } else if (inStore) {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    // Leaving store to Map 2
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 2;
                    trans.targetPos = LEVEL2_STORE_LEAVING_POINT;
                    trans.targetFacing = DIR_DOWN;
                    trans.title = "MAP 2";
                    trans.subtitle = "The Deep Wild Forest";
                    trans.tip = "Leaving Market Store";
                    continue;
                }
            } else {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    if (bgmLoaded) PauseMusicStream(bgm);
                    menu.Reset();
                    state = AppState::Menu;
                    continue;
                }
            }

            int sw = GetScreenWidth(), sh = GetScreenHeight();
            MapData& curMap = (activeMapId == 4 && map4Ready) ? map4 : ((activeMapId == 2 && map2Ready) ? map2 : map1);

            SetMasterVolume(settings.master);

            // ---- music stream update ----
            if (bgmLoaded) {
                UpdateMusicStream(bgm);
            }

            // ---- Shop update when inside store ----
            if (inStore) {
                shop.Update();
                if (nearAlchemist && !shop.open && trans.state == TRANS_IDLE) {
                    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_E)) {
                        shop.Open();
                    }
                }
            }

        // ---- map transition state machine ----
        if (trans.state != TRANS_IDLE) {
            trans.timer += dt;
            float half = trans.duration;
            if (trans.state == TRANS_FADE_OUT) {
                trans.alpha = std::clamp(trans.timer / half, 0.0f, 1.0f);
                if (trans.timer >= half) {
                    activeMapId = trans.targetMapId;
                    pos = trans.targetPos;
                    facingDir = trans.targetFacing;
                    facing = (facingDir == DIR_LEFT) ? -1 : 1;
                    cam.target = pos;
                    trans.state = TRANS_FADE_IN;
                    trans.timer = 0.0f;
                }
            } else if (trans.state == TRANS_FADE_IN) {
                trans.alpha = 1.0f - std::clamp(trans.timer / half, 0.0f, 1.0f);
                if (trans.timer >= half) {
                    trans.state = TRANS_IDLE;
                    trans.alpha = 0.0f;
                }
            }
        }

        // ---- input ----
        if (IsKeyPressed(KEY_F1)) showMask = !showMask;
        if (IsKeyPressed(KEY_F2)) fogMode = (fogMode + 1) % 3;
        if (IsKeyPressed(KEY_F3)) showMini = !showMini;
        if (IsKeyPressed(KEY_F4)) showHud = !showHud;

        // ---- volume keyboard controls ----
        if (bgmLoaded) {
            if (IsKeyPressed(KEY_F5) || IsKeyPressed(KEY_NINE)) {
                bgmVolume = std::clamp(bgmVolume - 0.05f, 0.0f, 1.0f);
                if (!bgmMuted) SetMusicVolume(bgm, bgmVolume);
            }
            if (IsKeyPressed(KEY_F6) || IsKeyPressed(KEY_ZERO)) {
                bgmVolume = std::clamp(bgmVolume + 0.05f, 0.0f, 1.0f);
                if (!bgmMuted) SetMusicVolume(bgm, bgmVolume);
            }
            if (IsKeyPressed(KEY_M)) {
                bgmMuted = !bgmMuted;
                SetMusicVolume(bgm, bgmMuted ? 0.0f : bgmVolume);
            }
        }

        float radiusDir = 0.0f;
        if (IsKeyDown(KEY_RIGHT_BRACKET) || IsKeyDown(KEY_EQUAL) || IsKeyDown(KEY_KP_ADD))      radiusDir += 1.0f;
        if (IsKeyDown(KEY_LEFT_BRACKET)  || IsKeyDown(KEY_MINUS) || IsKeyDown(KEY_KP_SUBTRACT)) radiusDir -= 1.0f;
        if (radiusDir != 0.0f) {
            float speedMult = RADIUS_SPEED * (IsKeyDown(KEY_LEFT_CONTROL) ? 4.0f : 1.0f);
            radius = std::clamp(radius + radiusDir * speedMult * dt, RADIUS_MIN, RADIUS_MAX);
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) cam.zoom *= (1.0f + wheel * 0.1f);
        // Store interior is 1024x1024: fit the whole room on screen (zoomed out)
        float storeFitZoom = std::min((float)sw, (float)sh) / 1024.0f;
        if (inStore && !wasInStore) { outdoorZoom = cam.zoom; cam.zoom = storeFitZoom; }
        if (!inStore && wasInStore) { cam.zoom = outdoorZoom; }
        wasInStore = inStore;
        float minZoom = inStore ? storeFitZoom : std::max(1.0f, std::max((float)sw / curMap.w, (float)sh / curMap.h));
        cam.zoom = std::clamp(cam.zoom, minZoom, 8.0f);

        Vector2 dir = { 0.0f, 0.0f };
        if (trans.state == TRANS_IDLE && !shop.open) {
            dir = {
                (float)((int)(IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (int)(IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))),
                (float)((int)(IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  - (int)(IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)))
            };
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 0.0f) { dir.x /= len; dir.y /= len; }
        }

        float speed = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? RUN_SPEED : WALK_SPEED;
        Vector2 step = { dir.x * speed * dt, dir.y * speed * dt };

        // ---- movement with axis-separated collision strictly on the walking path or store floor ----
        Vector2 before = pos;
        if (inStore) {
            if (step.x != 0.0f && canStandInStore({ pos.x + step.x, pos.y })) pos.x += step.x;
            if (step.y != 0.0f && canStandInStore({ pos.x, pos.y + step.y })) pos.y += step.y;
        } else {
            if (step.x != 0.0f && curMap.walk.canStand({ pos.x + step.x, pos.y })) pos.x += step.x;
            if (step.y != 0.0f && curMap.walk.canStand({ pos.x, pos.y + step.y })) pos.y += step.y;
        }

        bool moving = (std::fabs(pos.x - before.x) + std::fabs(pos.y - before.y)) > 0.001f;
        if (moving) {
            animT += dt * (speed / WALK_SPEED);
            if (std::fabs(step.x) > std::fabs(step.y)) {
                if (step.x > 0.01f) { facingDir = DIR_RIGHT; facing = 1; }
                else if (step.x < -0.01f) { facingDir = DIR_LEFT; facing = -1; }
            } else {
                if (step.y > 0.01f) { facingDir = DIR_DOWN; }
                else if (step.y < -0.01f) { facingDir = DIR_UP; }
            }
        }

        // ---- check map triggers ----
        if (trans.state == TRANS_IDLE) {
            if (activeMapId == 1 && map2Ready) {
                // Exit of Map 1 eastern forest road
                if (pos.x >= 2520.0f && pos.y >= 410.0f && pos.y <= 490.0f) {
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 2;
                    trans.targetPos = { 50.0f, 575.0f };
                    trans.targetFacing = DIR_RIGHT;
                    trans.title = "MAP 2";
                    trans.subtitle = "The Deep Wild Forest";
                    trans.tip = "Walking Path Only - Follow The Trail";
                }
            } else if (activeMapId == 2) {
                // Return to Map 1 western trailhead
                if (pos.x <= 34.0f && pos.y >= 540.0f && pos.y <= 610.0f) {
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 1;
                    trans.targetPos = { 2505.0f, 448.0f };
                    trans.targetFacing = DIR_LEFT;
                    trans.title = "MAP 1";
                    trans.subtitle = "Village Outskirts";
                    trans.tip = "Returning to Settlement Territory";
                } else if (map4Ready && v2Dist(pos, FLOOR4_PORTAL) <= 42.0f &&
                           (IsKeyPressed(KEY_E) || v2Dist(pos, FLOOR4_PORTAL) <= 16.0f)) {
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 4;
                    trans.targetPos = FLOOR4_SPAWN;
                    trans.targetFacing = DIR_UP;
                    trans.title = "FLOOR 4";
                    trans.subtitle = "The Pentagram Arena";
                    trans.tip = "Walk down the south corridor to return to Map 2";
                } else if (storeReady) {
                    // Enter Market Store on Map 2
                    float distStore = v2Dist(pos, LEVEL2_STORE_ENTRANCE);
                    if (distStore <= 42.0f && (IsKeyPressed(KEY_E) || distStore <= 16.0f)) {
                        trans.state = TRANS_FADE_OUT;
                        trans.timer = 0.0f;
                        trans.targetMapId = 3;
                        trans.targetPos = { 580.0f, 880.0f };
                        trans.targetFacing = DIR_UP;
                        trans.title = "MARKET STORE";
                        trans.subtitle = "The Alchemist's Study";
                        trans.tip = "Walk up to the counter to browse goods";
                    }
                }
            } else if (activeMapId == 4) {
                // Leave Floor 4 through the south corridor, back to the Floor 4 portal on Map 2
                if (pos.y >= 574.0f) {
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 2;
                    trans.targetPos = { FLOOR4_PORTAL.x - 26.0f, FLOOR4_PORTAL.y };
                    trans.targetFacing = DIR_LEFT;
                    trans.title = "MAP 2";
                    trans.subtitle = "The Deep Wild Forest";
                    trans.tip = "Returning from the Pentagram Arena";
                }
            } else if (activeMapId == 3) {
                // Exit Market Store back to Level 2 store leaving point (385, 655)
                if (pos.y >= 925.0f || (pos.y >= 870.0f && (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_S)))) {
                    trans.state = TRANS_FADE_OUT;
                    trans.timer = 0.0f;
                    trans.targetMapId = 2;
                    trans.targetPos = LEVEL2_STORE_LEAVING_POINT;
                    trans.targetFacing = DIR_DOWN;
                    trans.title = "MAP 2";
                    trans.subtitle = "The Deep Wild Forest";
                    trans.tip = "Leaving Market Store";
                }
            }
        }

        // ---- player loadout switching (from above / hotkeys 1, 2, 3, 4) ----
        if (trans.state == TRANS_IDLE) {
            if (IsKeyPressed(KEY_ONE)   || IsKeyPressed(KEY_KP_1)) curLoadout = LOADOUT_BASE;
            if (IsKeyPressed(KEY_TWO)   || IsKeyPressed(KEY_KP_2)) curLoadout = LOADOUT_SHADOW;
            if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) curLoadout = LOADOUT_FIRE;
            if (IsKeyPressed(KEY_FOUR)  || IsKeyPressed(KEY_KP_4)) curLoadout = LOADOUT_TIME;
        }

        // ---- special abilities: Time Stop [T], Shadow Step [Q / Right-Click] ----
        if (timeStopCooldown > 0.0f) timeStopCooldown -= dt;
        if (shadowCooldown > 0.0f)   shadowCooldown   -= dt;
        if (timeStopTimer > 0.0f) {
            timeStopTimer -= dt;
            if (timeStopTimer <= 0.0f) {
                timeStopTimer = 0.0f;
                floatingTexts.push_back({ { pos.x, pos.y - 34.0f }, "Time resumes", Color{ 150, 215, 255, 255 }, 0.0f, 1.1f });
            }
        }
        for (auto& pf : shadowPuffs) pf.t += dt;
        shadowPuffs.erase(std::remove_if(shadowPuffs.begin(), shadowPuffs.end(), [](const ShadowPuff& pf) { return pf.t >= 0.55f; }), shadowPuffs.end());
        if (trans.state != TRANS_IDLE || shop.open) shadowBolt.active = false;

        auto canStandHere = [&](Vector2 q) { return inStore ? canStandInStore(q) : curMap.walk.canStand(q); };

        if (trans.state == TRANS_IDLE && !shop.open) {
            if (IsKeyPressed(KEY_T) && !inStore && timeStopTimer <= 0.0f && timeStopCooldown <= 0.0f) {
                timeStopTimer = TIME_STOP_DURATION;
                timeStopCooldown = TIME_STOP_DURATION + TIME_STOP_COOLDOWN;
                floatingTexts.push_back({ { pos.x, pos.y - 40.0f }, "TIME STOP!", Color{ 140, 215, 255, 255 }, 0.0f, 1.3f });
                if (swingSoundLoaded) { SetSoundPitch(swingSound, 0.45f); PlaySound(swingSound); }
            }

            bool throwPressed = IsKeyPressed(KEY_Q) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
            if (throwPressed && !shadowBolt.active && shadowCooldown <= 0.0f) {
                Vector2 mw = GetScreenToWorld2D(GetMousePosition(), cam);
                Vector2 d = { mw.x - pos.x, mw.y - pos.y };
                float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len < 4.0f) {
                    d = (facingDir == DIR_LEFT) ? Vector2{ -1, 0 } : (facingDir == DIR_RIGHT) ? Vector2{ 1, 0 }
                      : (facingDir == DIR_UP) ? Vector2{ 0, -1 } : Vector2{ 0, 1 };
                    len = 1.0f;
                }
                Vector2 dirN = { d.x / len, d.y / len };
                float reach = std::min(len, SHADOW_RANGE);
                // walk the ray; the shadow lands on the last walkable point before any wall
                Vector2 landing = pos;
                for (float t = 2.0f; t <= reach; t += 2.0f) {
                    Vector2 q = { pos.x + dirN.x * t, pos.y + dirN.y * t };
                    if (!canStandHere(q)) break;
                    landing = q;
                }
                float flown = v2Dist(landing, pos);
                if (flown >= 10.0f) {
                    shadowBolt.active = true;
                    shadowBolt.from = pos;
                    shadowBolt.to = landing;
                    shadowBolt.pos = pos;
                    shadowBolt.dir = dirN;
                    shadowBolt.travelled = 0.0f;
                    shadowBolt.total = flown;
                    if (std::fabs(dirN.x) > std::fabs(dirN.y)) { facingDir = (dirN.x > 0) ? DIR_RIGHT : DIR_LEFT; facing = (dirN.x > 0) ? 1 : -1; }
                    else facingDir = (dirN.y > 0) ? DIR_DOWN : DIR_UP;
                    if (swingSoundLoaded) { SetSoundPitch(swingSound, 0.7f); PlaySound(swingSound); }
                } else {
                    floatingTexts.push_back({ { pos.x, pos.y - 34.0f }, "Blocked!", Color{ 200, 140, 255, 255 }, 0.0f, 0.8f });
                }
            }
        }

        if (shadowBolt.active) {
            shadowBolt.travelled += SHADOW_SPEED * dt;
            float f = std::clamp(shadowBolt.travelled / std::max(1.0f, shadowBolt.total), 0.0f, 1.0f);
            shadowBolt.pos = { shadowBolt.from.x + (shadowBolt.to.x - shadowBolt.from.x) * f,
                               shadowBolt.from.y + (shadowBolt.to.y - shadowBolt.from.y) * f };
            if (f >= 1.0f) {
                // jump to where the shadow landed
                shadowPuffs.push_back({ pos, 0.0f });
                pos = shadowBolt.to;
                shadowPuffs.push_back({ pos, 0.0f });
                shadowBolt.active = false;
                shadowCooldown = SHADOW_COOLDOWN;
                floatingTexts.push_back({ { pos.x, pos.y - 34.0f }, "Shadow Step", Color{ 205, 150, 255, 255 }, 0.0f, 0.9f });
            }
        }

        // time stop state shared with enemy/critter updates and drawing
        g_tsActive = (timeStopTimer > 0.0f) && !inStore;
        g_tsCenter = pos;
        g_tsRadius = TIME_STOP_RADIUS;

        float topBarW = 580.0f;
        float topBarH = 44.0f;
        float topBarX = (sw - topBarW) * 0.5f;
        float topBarY = 10.0f;
        Rectangle topBarRect = { topBarX, topBarY, topBarW, topBarH };

        // ---- swinging input & update (press O, Space, Left-Click, or J) ----
        float hudBoxW = 550.0f;
        float hudBoxH = bgmLoaded ? 74.0f : 48.0f;
        float mw = 220.0f, mh = mw * curMap.h / curMap.w;
        float mx = sw - mw - 12.0f, my = 12.0f;

        bool swingPressed = false;
        if (trans.state == TRANS_IDLE && !inStore) {
            swingPressed = IsKeyPressed(KEY_O) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mpos = GetMousePosition();
                bool onHud = showHud && (mpos.x >= 8 && mpos.x <= 8 + hudBoxW + 16 && mpos.y >= 8 && mpos.y <= 8 + hudBoxH);
                bool onMini = showMini && (mpos.x >= mx - 3 && mpos.x <= mx + mw + 3 && mpos.y >= my - 3 && mpos.y <= my + mh + 3);
                bool onTopBar = CheckCollisionPointRec(mpos, topBarRect);
                if (onTopBar) {
                    float tabW = (topBarW - 16.0f) / 4.0f;
                    for (int i = 0; i < 4; ++i) {
                        Rectangle tabRect = { topBarX + 8.0f + i * tabW, topBarY + 5.0f, tabW, topBarH - 10.0f };
                        if (CheckCollisionPointRec(mpos, tabRect)) {
                            curLoadout = (PlayerLoadout)i;
                            break;
                        }
                    }
                } else if (!onHud && !onMini) {
                    swingPressed = true;
                }
            }
        }

        if (swingPressed && swingTimer <= 0.0f) {
            swingTimer = 0.32f;
            swingDealtDamage = false;
            if (swingSoundLoaded) {
                float pitch = (curLoadout == LOADOUT_TIME) ? 1.35f :
                              ((curLoadout == LOADOUT_SHADOW) ? 0.82f :
                              ((curLoadout == LOADOUT_FIRE) ? 0.95f : 1.0f));
                SetSoundPitch(swingSound, pitch);
                PlaySound(swingSound);
            }
        }

        bool isSwinging = (swingTimer > 0.0f);
        float swingProgress = 0.0f;
        if (isSwinging) {
            swingTimer -= dt;
            swingProgress = 1.0f - std::clamp(swingTimer / 0.32f, 0.0f, 1.0f);

            // Sword melee hit detection (10 damage dealt by player to enemies)
            if (!swingDealtDamage && swingProgress >= 0.15f) {
                for (auto& enemy : curMap.enemies) {
                    if (enemy.isDead || enemy.hp <= 0) continue;
                    Vector2 toEnemy = { enemy.pos.x - pos.x, enemy.pos.y - pos.y };
                    float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y);
                    float hitDist = (enemy.type == ENEMY_SLIME) ? 46.0f * enemy.scale : 66.0f;

                    bool facingMatch = true;
                    if (facingDir == DIR_RIGHT && toEnemy.x < -16.0f) facingMatch = false;
                    if (facingDir == DIR_LEFT  && toEnemy.x > 16.0f)  facingMatch = false;
                    if (facingDir == DIR_UP    && toEnemy.y > 16.0f)  facingMatch = false;
                    if (facingDir == DIR_DOWN  && toEnemy.y < -16.0f) facingMatch = false;

                    if (dist <= hitDist && (facingMatch || dist <= 32.0f)) {
                        enemy.hp = std::max(0, enemy.hp - 10);
                        enemy.hitFlashTimer = 0.22f;
                        if (dist > 0.01f) {
                            Vector2 kb = { (toEnemy.x / dist) * 12.0f, (toEnemy.y / dist) * 12.0f };
                            Vector2 nX = { enemy.pos.x + kb.x, enemy.pos.y };
                            if (curMap.walk.canStand(nX) && !(activeMapId == 1 && isInsideFloor1City(nX))) enemy.pos.x += kb.x;
                            Vector2 nY = { enemy.pos.x, enemy.pos.y + kb.y };
                            if (curMap.walk.canStand(nY) && !(activeMapId == 1 && isInsideFloor1City(nY))) enemy.pos.y += kb.y;
                        }
                        if (damageSoundLoaded) PlaySound(damageSound);
                        floatingTexts.push_back({ { enemy.pos.x, enemy.pos.y - 28.0f }, "-10", GOLD, 0.0f, 0.85f });

                        if (enemy.hp <= 0) {
                            enemy.state = ESTATE_DEAD;
                            enemy.deathTimer = 0.0f;
                            int bounty = (enemy.type == ENEMY_SLIME) ? (enemy.scale > 1.2f ? 60 : 15) :
                                         ((enemy.type == ENEMY_GOLEM) ? 80 :
                                         ((enemy.type == ENEMY_CYCLOPS) ? 150 : 35));
                            shop.gold += bounty;
                            const char* defMsg = (enemy.type == ENEMY_SLIME) ? (enemy.scale > 1.2f ? "KING SLIME DEFEATED! +60G" : "SLIME DEFEATED! +15G") :
                                                 ((enemy.type == ENEMY_GOLEM) ? "STONE GOLEM SLAIN! +80G" :
                                                 ((enemy.type == ENEMY_CYCLOPS) ? "CYCLOPS SLAIN! +150G" : "GOBLIN SLAIN! +35G"));
                            Color msgCol = (enemy.type == ENEMY_SLIME) ? GREEN : GOLD;
                            floatingTexts.push_back({ { enemy.pos.x, enemy.pos.y - 48.0f }, defMsg, msgCol, 0.0f, 1.4f });
                        }
                        swingDealtDamage = true;
                    }
                }
            }

            if (swingTimer <= 0.0f) {
                isSwinging = false;
                swingProgress = 1.0f;
                swingDealtDamage = false;
            }
        }

        // ---- invulnerability & damage flash timers ----
        if (playerInvulnTimer > 0.0f) playerInvulnTimer -= dt;
        if (playerHurtFlash > 0.0f)   playerHurtFlash -= dt;

        if (!inStore) {
            // ---- enemy combat & AI updates strictly on walking path ----
            updateEnemies(curMap.enemies, curMap.walk, dt, pos, playerHp, playerInvulnTimer, playerHurtFlash,
                          damageSoundLoaded, damageSound, curMap.defaultSpawn, floatingTexts, activeMapId,
                          g_tsActive, pos, TIME_STOP_RADIUS);
            // ---- wildlife critters update (wander/run/sleep in outside grass/clearings) ----
            updateCritters(curMap.critters, dt, curMap.critterMask, pos, isSwinging, g_tsActive, pos, TIME_STOP_RADIUS);

            // ---- exploration fog update ----
            float rdx = pos.x - curMap.lastReveal.x, rdy = pos.y - curMap.lastReveal.y;
            bool radiusChanged = std::fabs(radius - curMap.lastRevealRadius) >= 1.0f;
            if (rdx * rdx + rdy * rdy >= 4.0f || radiusChanged) {
                int x0, y0, x1, y1;
                if (curMap.fog.reveal(pos, radius, x0, y0, x1, y1)) {
                    int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
                    curMap.fogUpload.resize((size_t)rw * rh);
                    for (int y = 0; y < rh; ++y)
                        for (int x = 0; x < rw; ++x)
                            curMap.fogUpload[(size_t)y * rw + x] = curMap.fog.px[(size_t)(y0 + y) * curMap.fog.w + (x0 + x)];
                    UpdateTextureRec(curMap.fogTex, Rectangle{ (float)x0, (float)y0, (float)rw, (float)rh }, curMap.fogUpload.data());
                }
                curMap.lastReveal = pos;
                curMap.lastRevealRadius = radius;
            }
        }

        // ---- floating combat text update ----
        for (size_t i = 0; i < floatingTexts.size(); ) {
            floatingTexts[i].timer += dt;
            floatingTexts[i].pos.y -= 22.0f * dt;
            if (floatingTexts[i].timer >= floatingTexts[i].life) {
                floatingTexts.erase(floatingTexts.begin() + i);
            } else {
                ++i;
            }
        }

        // ---- footstep sound ----
        if (walkSoundLoaded && moving && !isSwinging) {
            stepSoundTimer -= dt;
            if (stepSoundTimer <= 0.0f) {
                bool running = (speed >= RUN_SPEED - 1.0f);
                float stepInterval = running ? 0.28f : 0.42f;
                float pitch = running ? 1.30f : 1.0f;
                SetSoundPitch(walkSound, pitch);
                SetSoundVolume(walkSound, 0.50f);
                PlaySound(walkSound);
                stepSoundTimer = stepInterval;
            }
        } else {
            stepSoundTimer = 0.0f;
        }

        // ---- roof occlusion on active map ----
        bool anyZone = false;
        for (const Rectangle& z : curMap.zones) if (inRect(z, pos)) { anyZone = true; break; }
        bool behindRoof = curMap.walk.isBehind(pos);

        // ---- camera follow, smoothly clamped to active map bounds or store interior ----
        cam.offset = { sw * 0.5f, sh * 0.5f };
        float k = 1.0f - std::exp(-8.0f * dt);
        cam.target.x += (pos.x - cam.target.x) * k;
        cam.target.y += (pos.y - cam.target.y) * k;
        float halfW = sw / (2.0f * cam.zoom), halfH = sh / (2.0f * cam.zoom);
        if (inStore) {
            cam.target.x = (2 * halfW >= 1024.0f) ? 512.0f : std::clamp(cam.target.x, halfW, 1024.0f - halfW);
            cam.target.y = (2 * halfH >= 1024.0f) ? 512.0f : std::clamp(cam.target.y, halfH, 1024.0f - halfH);
        } else {
            cam.target.x = (2 * halfW >= curMap.w) ? curMap.w * 0.5f : std::clamp(cam.target.x, halfW, curMap.w - halfW);
            cam.target.y = (2 * halfH >= curMap.h) ? curMap.h * 0.5f : std::clamp(cam.target.y, halfH, curMap.h - halfH);
        }

        Color fogTint = (fogMode == FOG_DIM) ? Color{ 255, 255, 255, FOG_DIM_ALPHA } : WHITE;

        // ---- draw ----
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(cam);
            if (inStore) {
                // In Store:
                // When entering/walking around: store1Tex (png 1)
                // When reaching the Old Alchemist counter: store2Tex (png 2, which includes dialogue box)
                Texture2D curStoreTex = (nearAlchemist && store2Tex.id != 0) ? store2Tex : store1Tex;
                if (curStoreTex.id != 0) {
                    DrawTexture(curStoreTex, 0, 0, WHITE);
                } else {
                    DrawRectangle(0, 0, 1024, 1024, Color{ 25, 20, 18, 255 });
                }

                // Doorway exit pulse at bottom
                float pulse = (std::sin((float)GetTime() * 4.5f) + 1.0f) * 0.5f;
                Color glowGold = ColorAlpha(GOLD, 0.40f + 0.35f * pulse);
                DrawCircleLines(580, 920, 22.0f + 3.0f * pulse, glowGold);
                DrawText("EXIT", 566, 912, 13, ColorAlpha(GOLD, 0.9f));

                if (nearAlchemist) {
                    DrawCircleGradient(580, 520, 42.0f, ColorAlpha(GOLD, 0.25f), ColorAlpha(GOLD, 0.0f));
                }

                float playerDrawAlpha = (playerInvulnTimer > 0.0f && ((int)(playerInvulnTimer * 16.0f) % 2 == 0)) ? 0.35f : 1.0f;
                drawCharacter(avatar, pos, moving, animT, facingDir, facing, isSwinging, swingProgress, curLoadout, playerDrawAlpha);
                drawShadowFx();
                drawFloatingTexts(floatingTexts);
            } else {
                DrawTexture(curMap.mapTex, 0, 0, WHITE);
                if (showMask) DrawTexture(curMap.maskTex, 0, 0, WHITE);

                // On-path road transition indicators
                float pulse = (std::sin((float)GetTime() * 4.5f) + 1.0f) * 0.5f;
                if (activeMapId == 1 && map2Ready) {
                    // Forest ending on Map 1
                    Color glowGold = ColorAlpha(GOLD, 0.45f + 0.35f * pulse);
                    Color glowAqua = ColorAlpha(SKYBLUE, 0.50f + 0.35f * pulse);
                    DrawCircleLines(2510, 448, 28.0f + 4.0f * pulse, glowGold);
                    DrawCircleLines(2510, 448, 16.0f, glowAqua);
                    DrawText(">>> TO MAP 2 >>>", 2445, 416, 13, GOLD);
                    DrawText("The Deep Wild Forest", 2435, 465, 12, RAYWHITE);
                } else if (activeMapId == 2) {
                    // Trailhead on Map 2
                    Color glowGold = ColorAlpha(GOLD, 0.45f + 0.35f * pulse);
                    Color glowAqua = ColorAlpha(SKYBLUE, 0.50f + 0.35f * pulse);
                    DrawCircleLines(45, 575, 26.0f + 4.0f * pulse, glowGold);
                    DrawCircleLines(45, 575, 15.0f, glowAqua);
                    DrawText("<<< TO MAP 1 <<<", 12, 545, 12, GOLD);
                    DrawText("Village Outskirts", 12, 595, 11, RAYWHITE);

                    // Market Store Entrance on Map 2
                    if (storeReady) {
                        DrawCircleLines((int)LEVEL2_STORE_ENTRANCE.x, (int)LEVEL2_STORE_ENTRANCE.y, 22.0f + 3.0f * pulse, glowGold);
                        DrawCircleLines((int)LEVEL2_STORE_ENTRANCE.x, (int)LEVEL2_STORE_ENTRANCE.y, 12.0f, glowAqua);
                        DrawText("MARKET STORE", (int)LEVEL2_STORE_ENTRANCE.x - 48, (int)LEVEL2_STORE_ENTRANCE.y - 32, 12, GOLD);
                    }
                }

                // Floor 4 portal marker on Map 2 / exit marker on Floor 4
                if (activeMapId == 2 && map4Ready) {
                    Color pr = ColorAlpha(Color{ 255, 90, 90, 255 }, 0.55f + 0.35f * pulse);
                    DrawCircleGradient((int)FLOOR4_PORTAL.x, (int)FLOOR4_PORTAL.y, 30.0f, ColorAlpha(Color{ 255, 70, 70, 255 }, 0.30f), ColorAlpha(Color{ 120, 0, 40, 255 }, 0.0f));
                    DrawCircleLines((int)FLOOR4_PORTAL.x, (int)FLOOR4_PORTAL.y, 24.0f + 3.0f * pulse, pr);
                    DrawCircleLines((int)FLOOR4_PORTAL.x, (int)FLOOR4_PORTAL.y, 13.0f, ColorAlpha(GOLD, 0.8f));
                    const char* ptxt = "FLOOR 4 - PENTAGRAM ARENA";
                    DrawText(ptxt, (int)FLOOR4_PORTAL.x - MeasureText(ptxt, 12) / 2, (int)FLOOR4_PORTAL.y - 44, 12, Color{ 255, 140, 120, 255 });
                } else if (activeMapId == 4) {
                    DrawEllipseLines(512, 580, 24.0f + 3.0f * pulse, 9.0f, ColorAlpha(GOLD, 0.8f));
                    DrawText("EXIT - MAP 2", 512 - MeasureText("EXIT - MAP 2", 12) / 2, 556, 12, GOLD);
                }

                drawTimeStopZone();
                drawCritters(critterAssets, curMap.critters);
                drawEnemies(enemyAssets, curMap.enemies);
                float playerDrawAlpha = (playerInvulnTimer > 0.0f && ((int)(playerInvulnTimer * 16.0f) % 2 == 0)) ? 0.35f : 1.0f;
                drawCharacter(avatar, pos, moving, animT, facingDir, facing, isSwinging, swingProgress, curLoadout, playerDrawAlpha);
                // Roof occlusion only on Map 1 (Blue village house roofs).
                // On Map 2, player always appears at the very top of all overlapping trees & scenery.
                if (activeMapId == 1 && anyZone && curMap.roofTex.id != 0) {
                    for (const Rectangle& z : curMap.zones)
                        if (inRect(z, pos)) DrawTextureRec(curMap.roofTex, z, Vector2{ z.x, z.y }, WHITE);
                    if (behindRoof) drawCharacter(avatar, pos, moving, animT, facingDir, facing, isSwinging, swingProgress, curLoadout, 0.45f);
                }
                if (activeMapId == 2 && behindRoof) {
                    // Subtle radiant backlight when walking across tree-covered sections on Map 2
                    DrawCircleGradient((int)std::round(pos.x), (int)std::round(pos.y - 18.0f), 24.0f,
                                       ColorAlpha(GOLD, 0.20f), ColorAlpha(GOLD, 0.0f));
                }
                drawShadowFx();
                drawFloatingTexts(floatingTexts);
            }
        EndMode2D();

        // Time Stop screen tint
        if (timeStopTimer > 0.0f) {
            float ta = std::min(1.0f, std::min(timeStopTimer / 0.5f, (TIME_STOP_DURATION - timeStopTimer) / 0.25f + 0.05f));
            DrawRectangle(0, 0, sw, sh, ColorAlpha(Color{ 40, 80, 170, 255 }, 0.13f * ta));
        }

        // minimap (only outside store)
        if (showMini && !inStore) {
            DrawRectangle((int)mx - 3, (int)my - 3, (int)mw + 6, (int)mh + 6, Color{ 0, 0, 0, 200 });
            DrawTexturePro(curMap.mapTex, Rectangle{ 0, 0, (float)curMap.w, (float)curMap.h },
                           Rectangle{ mx, my, mw, mh }, Vector2{ 0, 0 }, 0.0f, WHITE);
            if (fogMode != FOG_OFF) {
                DrawTexturePro(curMap.fogTex, Rectangle{ 0, 0, (float)curMap.fog.w, (float)curMap.fog.h },
                               Rectangle{ mx, my, mw, mh }, Vector2{ 0, 0 }, 0.0f, fogTint);
            }
            float sx = mw / curMap.w;
            // Wildlife critters on minimap
            for (const auto& c : curMap.critters) {
                Color critColor = (c.type == CRITTER_STAG) ? Color{ 235, 185, 110, 180 } :
                                  ((c.type == CRITTER_BOAR) ? Color{ 190, 145, 110, 180 } :
                                  ((c.type == CRITTER_BADGER) ? Color{ 215, 215, 220, 180 } : Color{ 150, 175, 205, 180 }));
                DrawCircle((int)(mx + c.pos.x * sx), (int)(my + c.pos.y * sx), 1.2f, critColor);
            }
            // Enemies on minimap
            for (const auto& e : curMap.enemies) {
                if (e.isDead || e.hp <= 0) continue;
                if (e.type == ENEMY_SLIME) {
                    DrawCircle((int)(mx + e.pos.x * sx), (int)(my + e.pos.y * sx), 1.8f, Color{ 255, 60, 60, 220 });
                } else if (e.type == ENEMY_GOLEM) {
                    DrawCircle((int)(mx + e.pos.x * sx), (int)(my + e.pos.y * sx), 3.5f, GOLD);
                } else if (e.type == ENEMY_CYCLOPS) {
                    DrawCircle((int)(mx + e.pos.x * sx), (int)(my + e.pos.y * sx), 3.5f, Color{ 255, 65, 180, 255 });
                } else if (e.type == ENEMY_ORC) {
                    DrawCircle((int)(mx + e.pos.x * sx), (int)(my + e.pos.y * sx), 2.8f, Color{ 60, 220, 90, 255 });
                }
            }
            DrawCircle((int)(mx + pos.x * sx), (int)(my + pos.y * sx), 3.0f, RED);
            DrawRectangleLines((int)(mx + (cam.target.x - halfW) * sx), (int)(my + (cam.target.y - halfH) * sx),
                               (int)(2 * halfW * sx), (int)(2 * halfH * sx), WHITE);
        }

        // Floor 4 portal / exit notices
        if (trans.state == TRANS_IDLE && activeMapId == 2 && map4Ready) {
            float dP = v2Dist(pos, FLOOR4_PORTAL);
            if (dP < 110.0f) {
                float a = std::clamp((110.0f - dP) / 50.0f, 0.0f, 1.0f);
                const char* msg = "Portal to FLOOR 4 — The Pentagram Arena — Press [E] or step in";
                int tmw = MeasureText(msg, 18);
                int px = (sw - tmw) / 2, py = 85;
                DrawRectangleRounded(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, ColorAlpha(Color{ 25, 10, 15, 235 }, a));
                DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, 2.0f, ColorAlpha(Color{ 255, 110, 100, 255 }, a * 0.85f));
                DrawText(msg, px, py, 18, ColorAlpha(Color{ 255, 150, 130, 255 }, a));
            }
        }
        if (trans.state == TRANS_IDLE && activeMapId == 4 && pos.y > 480.0f) {
            const char* msg = "Floor 4 exit — walk down the south corridor to return to MAP 2";
            int tmw = MeasureText(msg, 18);
            int px = (sw - tmw) / 2, py = 85;
            DrawRectangleRounded(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, Color{ 10, 20, 15, 235 });
            DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, 2.0f, ColorAlpha(GOLD, 0.85f));
            DrawText(msg, px, py, 18, GOLD);
        }

        // Floating path proximity notice
        if (trans.state == TRANS_IDLE) {
            if (activeMapId == 1 && map2Ready) {
                float dExit = v2Dist(pos, Vector2{ 2510.0f, 448.0f });
                if (dExit < 190.0f) {
                    float a = std::clamp((190.0f - dExit) / 70.0f, 0.0f, 1.0f);
                    const char* msg = "1st Forest Ending — Path leads into MAP 2 (The Deep Wild Forest)";
                    int tmw = MeasureText(msg, 18);
                    int px = (sw - tmw) / 2, py = 85;
                    DrawRectangleRounded(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, ColorAlpha(Color{ 10, 20, 15, 235 }, a));
                    DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, 2.0f, ColorAlpha(GOLD, a * 0.85f));
                    DrawText(msg, px, py, 18, ColorAlpha(GOLD, a));
                }
            } else if (activeMapId == 2) {
                float dEnt = v2Dist(pos, Vector2{ 45.0f, 575.0f });
                if (dEnt < 190.0f) {
                    float a = std::clamp((190.0f - dEnt) / 70.0f, 0.0f, 1.0f);
                    const char* msg = "Forest Trailhead — Path returns to MAP 1 (Village Outskirts)";
                    int tmw = MeasureText(msg, 18);
                    int px = (sw - tmw) / 2, py = 85;
                    DrawRectangleRounded(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, ColorAlpha(Color{ 10, 20, 15, 235 }, a));
                    DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, 2.0f, ColorAlpha(GOLD, a * 0.85f));
                    DrawText(msg, px, py, 18, ColorAlpha(GOLD, a));
                } else if (storeReady) {
                    float dStore = v2Dist(pos, LEVEL2_STORE_ENTRANCE);
                    if (dStore < 95.0f) {
                        float a = std::clamp((95.0f - dStore) / 45.0f, 0.0f, 1.0f);
                        const char* msg = "Market Store Entrance — Press [E] to Enter The Alchemist's Study";
                        int tmw = MeasureText(msg, 18);
                        int px = (sw - tmw) / 2, py = 85;
                        DrawRectangleRounded(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, ColorAlpha(Color{ 10, 20, 15, 235 }, a));
                        DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 18, (float)py - 6, (float)tmw + 36, 36 }, 0.3f, 6, 2.0f, ColorAlpha(GOLD, a * 0.85f));
                        DrawText(msg, px, py, 18, ColorAlpha(GOLD, a));
                    }
                }
            } else if (inStore && !shop.open) {
                if (nearAlchemist) {
                    const char* msg = "[E] or [Enter] Browse Goods & Alchemy Supplies  |  [ESC] Exit Store";
                    int tmw = MeasureText(msg, 17);
                    int px = (sw - tmw) / 2, py = sh - 58;
                    DrawRectangleRounded(Rectangle{ (float)px - 16, (float)py - 6, (float)tmw + 32, 34 }, 0.3f, 6, Color{ 14, 18, 26, 235 });
                    DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 16, (float)py - 6, (float)tmw + 32, 34 }, 0.3f, 6, 1.8f, GOLD);
                    DrawText(msg, px, py + 1, 17, GOLD);
                } else if (pos.y >= 860.0f) {
                    const char* msg = "Doorway — Walk Down or Press [E] / [S] to Return to Map 2";
                    int tmw = MeasureText(msg, 16);
                    int px = (sw - tmw) / 2, py = sh - 58;
                    DrawRectangleRounded(Rectangle{ (float)px - 16, (float)py - 6, (float)tmw + 32, 32 }, 0.3f, 6, Color{ 14, 18, 26, 220 });
                    DrawRectangleRoundedLinesEx(Rectangle{ (float)px - 16, (float)py - 6, (float)tmw + 32, 32 }, 0.3f, 6, 1.2f, RAYWHITE);
                    DrawText(msg, px, py + 1, 16, RAYWHITE);
                }
            }
        }

        // Fullscreen cinematic transition banner
        if (trans.state != TRANS_IDLE) {
            DrawRectangle(0, 0, sw, sh, ColorAlpha(BLACK, trans.alpha));

            float bw = 520.0f, bh = 145.0f;
            float bx = (sw - bw) * 0.5f, by = (sh - bh) * 0.5f;
            DrawRectangleRounded(Rectangle{ bx, by, bw, bh }, 0.15f, 8, ColorAlpha(Color{ 12, 16, 22, 245 }, trans.alpha));
            DrawRectangleRoundedLinesEx(Rectangle{ bx, by, bw, bh }, 0.15f, 8, 2.0f, ColorAlpha(GOLD, trans.alpha * 0.9f));
            DrawLine((int)bx + 30, (int)by + 54, (int)(bx + bw - 30), (int)by + 54, ColorAlpha(GOLD, trans.alpha * 0.7f));

            int tW = MeasureText(trans.title, 26);
            DrawText(trans.title, (int)(sw - tW) * 0.5f, (int)by + 20, 26, ColorAlpha(GOLD, trans.alpha));

            int sW = MeasureText(trans.subtitle, 20);
            DrawText(trans.subtitle, (int)(sw - sW) * 0.5f, (int)by + 66, 20, ColorAlpha(RAYWHITE, trans.alpha));

            int tipW = MeasureText(trans.tip, 15);
            DrawText(trans.tip, (int)(sw - tipW) * 0.5f, (int)by + 102, 15, ColorAlpha(Color{ 160, 220, 255, 255 }, trans.alpha));
        }

        // Damage flash vignette
        if (playerHurtFlash > 0.0f) {
            float a = std::clamp(playerHurtFlash / 0.25f, 0.0f, 1.0f);
            DrawRectangle(0, 0, sw, sh, ColorAlpha(RED, a * 0.22f));
        }

        // ---- Top Loadout Selection Bar ("from the above") ----
        {
            Vector2 mpos = GetMousePosition();
            DrawRectangleRounded(topBarRect, 0.35f, 8, Color{ 14, 18, 26, 235 });
            DrawRectangleRoundedLinesEx(topBarRect, 0.35f, 8, 1.5f, Color{ 65, 88, 118, 190 });

            struct LoadoutTabInfo {
                const char* title;
                const char* sub;
                Color color;
            };
            const LoadoutTabInfo tabs[4] = {
                { "1 Base",   "Steel Blade",   Color{ 185, 215, 235, 255 } },
                { "2 Shadow", "Dark Katana",   Color{ 195, 115, 255, 255 } },
                { "3 Fire",   "Flame Blade",   Color{ 255, 130,  50, 255 } },
                { "4 Time",   "Temporal Edge", Color{  75, 230, 255, 255 } }
            };

            float tabW = (topBarW - 16.0f) / 4.0f;
            for (int i = 0; i < 4; ++i) {
                Rectangle tabRect = { topBarX + 8.0f + i * tabW, topBarY + 5.0f, tabW, topBarH - 10.0f };
                bool isSelected = (curLoadout == i);
                bool isHovered = CheckCollisionPointRec(mpos, tabRect);

                if (isSelected) {
                    DrawRectangleRounded(tabRect, 0.28f, 6, ColorAlpha(tabs[i].color, 0.28f));
                    DrawRectangleRoundedLinesEx(tabRect, 0.28f, 6, 1.8f, tabs[i].color);
                    DrawCircle((int)(tabRect.x + 13.0f), (int)(tabRect.y + tabRect.height * 0.5f), 4.5f, tabs[i].color);
                } else if (isHovered) {
                    DrawRectangleRounded(tabRect, 0.28f, 6, Color{ 38, 48, 65, 190 });
                    DrawRectangleRoundedLinesEx(tabRect, 0.28f, 6, 1.0f, ColorAlpha(tabs[i].color, 0.65f));
                    DrawCircle((int)(tabRect.x + 13.0f), (int)(tabRect.y + tabRect.height * 0.5f), 3.5f, ColorAlpha(tabs[i].color, 0.7f));
                } else {
                    DrawCircle((int)(tabRect.x + 13.0f), (int)(tabRect.y + tabRect.height * 0.5f), 3.0f, Color{ 95, 115, 135, 180 });
                }

                Color textCol = isSelected ? WHITE : (isHovered ? RAYWHITE : Color{ 175, 190, 205, 220 });
                DrawText(tabs[i].title, (int)(tabRect.x + 23.0f), (int)(tabRect.y + 4.0f), 14, textCol);
                DrawText(tabs[i].sub, (int)(tabRect.x + 23.0f), (int)(tabRect.y + 19.0f), 10, isSelected ? tabs[i].color : Color{ 130, 145, 160, 200 });
            }
        }

        // Grand Boss Health Bar (Orc Chieftain on Map 1, Golem / Cyclops on Map 2)
        if (activeMapId == 1) {
            for (const auto& enemy : curMap.enemies) {
                if (enemy.type == ENEMY_SLIME && enemy.scale > 1.2f && !enemy.isDead && enemy.hp > 0) {
                    float dist = v2Dist(pos, enemy.pos);
                    if (dist < 320.0f) {
                        float bossW = 380.0f, bossH = 24.0f;
                        float bossX = (sw - bossW) * 0.5f, bossY = 58.0f;
                        Color bCol = Color{ 80, 230, 110, 255 };
                        const char* bTitle = "KING SLIME — FOREST GATE GUARDIAN";
                        DrawRectangleRounded(Rectangle{ bossX - 8, bossY - 4, bossW + 16, bossH + 20 }, 0.25f, 6, Color{ 12, 16, 22, 240 });
                        DrawRectangleRoundedLinesEx(Rectangle{ bossX - 8, bossY - 4, bossW + 16, bossH + 20 }, 0.25f, 6, 1.5f, bCol);
                        DrawText(bTitle, (int)(sw - MeasureText(bTitle, 13)) * 0.5f, (int)bossY - 1, 13, bCol);
                        DrawRectangle((int)bossX, (int)bossY + 16, (int)bossW, 8, Color{ 15, 40, 20, 240 });
                        DrawRectangle((int)bossX, (int)bossY + 16, (int)(bossW * ((float)enemy.hp / enemy.maxHp)), 8, bCol);
                        DrawRectangleLines((int)bossX, (int)bossY + 16, (int)bossW, 8, WHITE);
                        break;
                    }
                }
            }
        } else if (activeMapId == 2 || activeMapId == 4) {
            for (const auto& enemy : curMap.enemies) {
                if ((enemy.type == ENEMY_GOLEM || enemy.type == ENEMY_CYCLOPS) && !enemy.isDead && enemy.hp > 0) {
                    float dist = v2Dist(pos, enemy.pos);
                    float triggerDist = (enemy.type == ENEMY_CYCLOPS) ? 380.0f : 340.0f;
                    if (dist < triggerDist) {
                        float bossW = 380.0f, bossH = 24.0f;
                        float bossX = (sw - bossW) * 0.5f, bossY = 58.0f;
                        Color bCol = (enemy.type == ENEMY_GOLEM) ? GOLD : Color{ 255, 75, 75, 255 };
                        const char* bTitle = (enemy.type == ENEMY_GOLEM) ? (activeMapId == 4 ? "STONE GOLEM — ARENA GUARDIAN" : "STONE GOLEM — LEVEL 2 GUARDIAN") : "WILD CYCLOPS — MAIN BOSS";
                        DrawRectangleRounded(Rectangle{ bossX - 8, bossY - 4, bossW + 16, bossH + 20 }, 0.25f, 6, Color{ 12, 16, 22, 240 });
                        DrawRectangleRoundedLinesEx(Rectangle{ bossX - 8, bossY - 4, bossW + 16, bossH + 20 }, 0.25f, 6, 1.5f, bCol);
                        DrawText(bTitle, (int)(sw - MeasureText(bTitle, 13)) * 0.5f, (int)bossY - 1, 13, bCol);
                        DrawRectangle((int)bossX, (int)bossY + 16, (int)bossW, 8, Color{ 45, 15, 15, 240 });
                        DrawRectangle((int)bossX, (int)bossY + 16, (int)(bossW * ((float)enemy.hp / enemy.maxHp)), 8, bCol);
                        DrawRectangleLines((int)bossX, (int)bossY + 16, (int)bossW, 8, WHITE);
                        break;
                    }
                }
            }
        }

        // HUD overlay
        if (showHud) {
            const char* locStr = inStore ? "Market Store: The Alchemist's Study" :
                                 ((activeMapId == 1) ? "Map 1: Village Outskirts" : ((activeMapId == 4) ? "Floor 4: The Pentagram Arena" : "Map 2: The Deep Wild Forest"));
            const char* l1 = inStore ?
                TextFormat("[%s]  Gold: %d G  |  Loadout: 1-4  |  [E]/Enter: Shop  |  ESC: Exit", locStr, shop.gold) :
                TextFormat("[%s]  Gold: %d G  |  Loadout: 1-4  |  O/Space/Click swing", locStr, shop.gold);
            const char* l2 = inStore ?
                TextFormat("Walk up to the Old Alchemist behind the counter to view wares") :
                TextFormat("[ ] vision: %d    F2 fog: %s    Enemies: %d    Wildlife: %d    M mute",
                           (int)radius, fogModeName(fogMode), (int)curMap.enemies.size(), (int)curMap.critters.size());
            int tw = std::max(MeasureText(l1, 16), MeasureText(l2, 16));
            tw = std::max(tw, 580);

            float hudH = bgmLoaded ? 74.0f : 48.0f;
            DrawRectangle(8, 8, tw + 16, (int)hudH, Color{ 0, 0, 0, 160 });
            DrawText(l1, 14, 13, 16, inStore ? GOLD : ((activeMapId == 1) ? RAYWHITE : GOLD));
            DrawText(l2, 14, 33, 16, Color{ 255, 220, 120, 255 });

            if (bgmLoaded) {
                float rowY = 53.0f;
                DrawText("BGM Vol:", 14, (int)rowY + 1, 14, RAYWHITE);

                Rectangle decBtn = { 85.0f, rowY, 20.0f, 16.0f };
                Rectangle barRect = { 110.0f, rowY + 2.0f, 100.0f, 12.0f };
                Rectangle incBtn = { 215.0f, rowY, 20.0f, 16.0f };

                Vector2 mpos = GetMousePosition();
                bool overDec = CheckCollisionPointRec(mpos, decBtn);
                bool overInc = CheckCollisionPointRec(mpos, incBtn);
                bool overBar = CheckCollisionPointRec(mpos, barRect);

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (overDec) {
                        bgmVolume = std::clamp(bgmVolume - 0.05f, 0.0f, 1.0f);
                        if (!bgmMuted) SetMusicVolume(bgm, bgmVolume);
                    } else if (overInc) {
                        bgmVolume = std::clamp(bgmVolume + 0.05f, 0.0f, 1.0f);
                        if (!bgmMuted) SetMusicVolume(bgm, bgmVolume);
                    } else if (overBar) {
                        bgmVolume = std::clamp((mpos.x - barRect.x) / barRect.width, 0.0f, 1.0f);
                        if (!bgmMuted) SetMusicVolume(bgm, bgmVolume);
                    }
                }

                DrawRectangleRec(decBtn, overDec ? Color{ 80, 80, 80, 255 } : Color{ 45, 45, 45, 220 });
                DrawRectangleLinesEx(decBtn, 1.0f, overDec ? GOLD : GRAY);
                DrawText("-", (int)decBtn.x + 7, (int)decBtn.y + 1, 14, RAYWHITE);

                DrawRectangleRec(barRect, Color{ 30, 30, 30, 220 });
                DrawRectangle((int)barRect.x, (int)barRect.y, (int)(barRect.width * bgmVolume), (int)barRect.height,
                              bgmMuted ? GRAY : Color{ 80, 180, 240, 220 });
                DrawRectangleLinesEx(barRect, 1.0f, GRAY);

                DrawRectangleRec(incBtn, overInc ? Color{ 80, 80, 80, 255 } : Color{ 45, 45, 45, 220 });
                DrawRectangleLinesEx(incBtn, 1.0f, overInc ? GOLD : GRAY);
                DrawText("+", (int)incBtn.x + 5, (int)incBtn.y + 1, 14, RAYWHITE);

                DrawText(TextFormat("%3d%% %s", (int)std::round(bgmVolume * 100.0f), bgmMuted ? "(MUTED)" : ""),
                         (int)incBtn.x + 26, (int)rowY + 1, 14, bgmMuted ? Color{ 255, 100, 100, 255 } : RAYWHITE);
                DrawText("(Click or F5/F6/9/0)", (int)incBtn.x + 115, (int)rowY + 1, 13, LIGHTGRAY);
            }

            // Player Health Bar (Top-left below HUD)
            float hpBarX = 14.0f, hpBarY = hudH + 14.0f;
            float hpBarW = 160.0f, hpBarH = 16.0f;
            DrawRectangleRounded(Rectangle{ hpBarX - 4, hpBarY - 4, hpBarW + 82, hpBarH + 8 }, 0.3f, 6, Color{ 16, 20, 26, 230 });
            DrawRectangleRoundedLinesEx(Rectangle{ hpBarX - 4, hpBarY - 4, hpBarW + 82, hpBarH + 8 }, 0.3f, 6, 1.2f, Color{ 80, 100, 120, 180 });
            DrawText("HP", (int)hpBarX, (int)hpBarY + 1, 14, Color{ 255, 100, 100, 255 });

            DrawRectangle((int)hpBarX + 26, (int)hpBarY + 2, (int)hpBarW, (int)hpBarH - 4, Color{ 45, 15, 15, 230 });
            float hpPct = std::clamp((float)playerHp / (float)playerMaxHp, 0.0f, 1.0f);
            Color hpCol = (hpPct > 0.5f) ? GREEN : ((hpPct > 0.25f) ? GOLD : RED);
            DrawRectangle((int)hpBarX + 26, (int)hpBarY + 2, (int)(hpBarW * hpPct), (int)hpBarH - 4, hpCol);
            DrawRectangleLines((int)hpBarX + 26, (int)hpBarY + 2, (int)hpBarW, (int)hpBarH - 4, GRAY);
            DrawText(TextFormat("%d/%d", playerHp, playerMaxHp), (int)hpBarX + (int)hpBarW + 34, (int)hpBarY + 1, 13, RAYWHITE);

            if (settings.showFps) DrawFPS(8, (int)hpBarY + 26);
        }

        // Ability bar (Time Stop / Shadow Step)
        if (showHud && !shop.open) {
            float slotW = 190.0f, slotH = 40.0f, gap = 12.0f;
            float baseX = (sw - (slotW * 2 + gap)) * 0.5f;
            float baseY = (float)sh - (inStore ? 112.0f : 58.0f);
            struct Slot { const char* key; const char* name; float cd; float cdMax; float active; float activeMax; Color col; bool disabled; };
            Slot slots[2] = {
                { "T", "Time Stop", std::max(0.0f, timeStopCooldown - timeStopTimer), TIME_STOP_COOLDOWN, timeStopTimer, TIME_STOP_DURATION, Color{ 110, 200, 255, 255 }, inStore },
                { "Q", "Shadow Step", shadowCooldown, SHADOW_COOLDOWN, shadowBolt.active ? 1.0f : 0.0f, 1.0f, Color{ 190, 120, 255, 255 }, false }
            };
            for (int i = 0; i < 2; ++i) {
                const Slot& sl = slots[i];
                Rectangle r = { baseX + i * (slotW + gap), baseY, slotW, slotH };
                DrawRectangleRounded(r, 0.25f, 6, Color{ 10, 14, 22, 215 });
                float frac = 1.0f;
                const char* status = "READY";
                if (sl.disabled)          { frac = 0.0f; status = "n/a here"; }
                else if (sl.active > 0.0f && i == 0) { frac = sl.active / sl.activeMax; status = TextFormat("%.1fs", sl.active); }
                else if (sl.active > 0.0f)           { frac = 1.0f; status = "..."; }
                else if (sl.cd > 0.0f)    { frac = 1.0f - sl.cd / sl.cdMax; status = TextFormat("%.1fs", sl.cd); }
                Color fill = ColorAlpha(sl.col, (sl.active > 0.0f) ? 0.65f : ((sl.cd > 0.0f || sl.disabled) ? 0.25f : 0.45f));
                DrawRectangleRounded(Rectangle{ r.x + 3, r.y + slotH - 9, (r.width - 6) * std::clamp(frac, 0.0f, 1.0f), 6 }, 0.5f, 4, fill);
                DrawRectangleRoundedLinesEx(r, 0.25f, 6, 1.6f, ColorAlpha(sl.col, (sl.cd > 0.0f || sl.disabled) ? 0.45f : 1.0f));
                DrawRectangle((int)r.x + 6, (int)r.y + 6, 20, 20, ColorAlpha(sl.col, 0.9f));
                DrawText(sl.key, (int)r.x + 12, (int)r.y + 9, 16, Color{ 10, 14, 22, 255 });
                DrawText(sl.name, (int)r.x + 34, (int)r.y + 5, 15, RAYWHITE);
                DrawText(status, (int)r.x + 34, (int)r.y + 21, 11, ColorAlpha(sl.col, 0.95f));
            }
            DrawText("Shadow Step: Q / Right-Click toward the cursor", (int)baseX, (int)baseY - 15, 11, Color{ 200, 200, 220, 170 });
        }

        // Shop UI Overlay (when activeMapId == 3 and shop.open)
        shop.Draw();

        wp::DrawBrightnessOverlay(settings);
        EndDrawing();
        }
    }

    wp::SaveSettings(settings);
    closeIntro();
    if (logoTex.id) UnloadTexture(logoTex);
    menu.Unload();

    if (bgmLoaded) {
        StopMusicStream(bgm);
        UnloadMusicStream(bgm);
    }
    if (walkSoundLoaded) {
        UnloadSound(walkSound);
    }
    if (swingSoundLoaded) {
        UnloadSound(swingSound);
    }
    if (damageSoundLoaded) {
        UnloadSound(damageSound);
    }
    CloseAudioDevice();

    avatar.unload();
    critterAssets.unload();
    enemyAssets.unload();
    UnloadTexture(map1.mapTex);
    UnloadTexture(map1.maskTex);
    UnloadTexture(map1.roofTex);
    UnloadTexture(map1.fogTex);

    if (map4Ready) {
        UnloadTexture(map4.mapTex);
        UnloadTexture(map4.maskTex);
        UnloadTexture(map4.fogTex);
    }
    if (map2Ready) {
        UnloadTexture(map2.mapTex);
        UnloadTexture(map2.maskTex);
        if (map2.roofTex.id != 0) UnloadTexture(map2.roofTex);
        UnloadTexture(map2.fogTex);
    }

    if (store1Tex.id != 0) UnloadTexture(store1Tex);
    if (store2Tex.id != 0) UnloadTexture(store2Tex);
    shop.Unload();

    CloseWindow();
    return 0;
}


