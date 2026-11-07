// Drawing the places of Szary: the dunes with grass in the wind, the pier
// in the rain, the rooms, the people, the weather and the lighthouse beam.
// Everything is ASCII in eight shades of grey.

#include <cmath>
#include <cstring>

#include "32blit.hpp"
#include "font.hpp"
#include "szary.hpp"
#include "wind.hpp"

using namespace blit;

namespace sz {
namespace world {

float beam = 0.0f;
float aurora = 0.0f;
bool show_player = true;

namespace {

// ---------------------------------------------------------------- layout

constexpr float P_SKY = 0.04f, P_SEA = 0.22f, P_BACK = 0.5f, P_FRONT = 1.35f;
constexpr float HORIZON = 72.0f;
constexpr float DUNES_W = 720.0f;
constexpr float LH_X = 150.0f;       // lighthouse on the dunes (door)
constexpr float HALINA_X = 494.0f, BARREL_X = 514.0f;
constexpr float BUNKER_X = 662.0f, CAR_X = 44.0f, BIKE_X = 176.0f;
constexpr int ROOM_FLOOR = 114;
constexpr int PIER_FROM = 110;
const char SOLID = 127;

inline float ground_y(float x) { return 108.0f + 4.0f * sinf(x * 0.013f) + 2.0f * sinf(x * 0.037f + 2.0f); }
inline float back_dune_y(float x) { return 93.0f + 5.0f * sinf(x * 0.02f) + 3.0f * sinf(x * 0.051f + 1.0f); }

inline uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float hashf(uint32_t x) { return float(hash(x) & 0xffff) / 65535.0f; }

// ---------------------------------------------------------------- state

wind::Field field;

constexpr int MID_BLADES = 130, BACK_BLADES = 120, FRONT_BLADES = 40;
wind::Blade mid[MID_BLADES], back[BACK_BLADES], front[FRONT_BLADES];

struct Grain { float x, y, vy; };
constexpr int GRAINS = 26;
Grain grains[GRAINS];

struct Mote { float x, y; };
constexpr int MOTES = 28;
Mote motes[MOTES];

struct Star { uint8_t x, y, kind; float phase; };
constexpr int STARS = 46;
Star stars[STARS];

struct Drop { float x, y; };
constexpr int DROPS = 70;
Drop drops[DROPS];

float cam = 0.0f;
float t = 0.0f;
float flicker = 1.0f;
int pressed_for = -1;  // chapter the trail was laid out for
int px = 64, py = 100; // the player on screen, for the torch

float wind_mult() {
    static const float m[6] = {1.0f, 1.0f, 1.2f, 1.5f, 2.1f, 0.5f};
    int ch = st.chapter > 5 ? 5 : st.chapter;
    return m[ch];
}
bool raining() { return st.chapter == 3 || st.chapter == 4; }

// Grass grows in tufts: a few blades from one spot, fanned out.
void place_blades(wind::Blade *blades, int n, float from, float to, float hmin, float hmax, float stiff, uint32_t salt) {
    float x = from;
    int i = 0;
    uint32_t k = salt;
    while(i < n) {
        int in_tuft = 3 + int(hash(k++) % 4u);
        float spread = 1.5f + hashf(k++) * 3.0f;
        float tall = hmin + (hmax - hmin) * hashf(k++);
        for(int j = 0; j < in_tuft && i < n; j++, i++) {
            auto &b = blades[i];
            float f = in_tuft > 1 ? float(j) / float(in_tuft - 1) - 0.5f : 0.0f;
            b.x = x + f * spread * 2.0f;
            b.height = tall * (0.65f + 0.35f * (1.0f - fabsf(f) * 1.6f)) * (0.85f + 0.3f * hashf(k++));
            b.rest = f * 0.7f;
            b.stiff = stiff * (0.75f + 0.5f * hashf(k++));
            b.phase = 6.28f * hashf(k++);
            b.angle = b.rest + 0.2f;
            b.vel = 0.0f;
            b.pressed = false;
        }
        x += (to - from) / float(n) * float(in_tuft) * (0.6f + 0.8f * hashf(k++));
        if(x > to) x = from + hashf(k++) * (to - from);
    }
}

// Which grass lies against the wind depends on the chapter: the first
// trail until the storm buries it, then three tufts towards the bunker.
void lay_trail() {
    pressed_for = st.chapter;
    for(auto &b : mid) {
        bool p = false;
        if(st.chapter < 4) p = b.x > 250.0f && b.x < 330.0f;
        else p = fabsf(b.x - 392.0f) < 9.0f || fabsf(b.x - 450.0f) < 9.0f || fabsf(b.x - 604.0f) < 9.0f;
        b.pressed = p;
    }
}

int sx_of(float world_x, float parallax) { return int(floorf(world_x - cam * parallax)); }

// ---------------------------------------------------------------- light

struct Lamp { float x, y, radius, power; bool flickers; };

int lamps_of(Scene s, Lamp out[4]) {
    int n = 0;
    switch(s) {
    case S_DUNES:
        if(!(flag(F_RELIT) && st.chapter >= 5))
            out[n++] = {BARREL_X + 4.0f, ground_y(BARREL_X) - 14.0f, 38.0f, 3.0f, true};
        if(beam > 0.0f) out[n++] = {LH_X, 30.0f, 40.0f, 2.0f, false};
        break;
    case S_STATION: out[n++] = {40.0f, 74.0f, 46.0f, 3.0f, false}; break;
    case S_BAR:
        out[n++] = {36.0f, 50.0f, 50.0f, 2.6f, true};
        out[n++] = {110.0f, 50.0f, 50.0f, 2.6f, true};
        break;
    case S_OFFICE: out[n++] = {50.0f, 60.0f, 54.0f, 3.0f, false}; break;
    case S_LIGHTHOUSE: out[n++] = {64.0f, 30.0f, 40.0f, flag(F_RELIT) ? 3.0f : 1.0f, false}; break;
    case S_PIER: out[n++] = {404.0f, 70.0f, 36.0f, 2.4f, true}; break;
    default: break;
    }
    return n;
}

Lamp lamps[4];
int nlamps = 0;

// Light on the screen at (x, y) from the torch and lamps, 0..4.
int light_at(int x, int y) {
    float v = 0.0f;
    if(st.torch) {
        float ox = float(px + st.facing * 7), oy = float(py - 15);
        float dx = (float(x) - ox) * float(st.facing);
        if(dx > 0.0f) {
            float dy = float(y) - oy - dx * 0.22f;
            float spread = 3.0f + dx * 0.4f;
            float ady = fabsf(dy);
            if(ady < spread && dx < 92.0f) v += (1.0f - dx / 92.0f) * (1.0f - 0.5f * ady / spread) * 5.4f * flicker;
        }
    }
    for(int i = 0; i < nlamps; i++) {
        const Lamp &l = lamps[i];
        float lx = float(sx_of(l.x, 1.0f)), ly = l.y;
        float ddx = float(x) - lx, ddy = (float(y) - ly) * 1.3f;
        float d2 = ddx * ddx + ddy * ddy;
        if(d2 >= l.radius * l.radius) continue;
        float k = 1.0f - sqrtf(d2) / l.radius;
        if(l.flickers) k *= 0.85f + 0.15f * sinf(t * 17.0f + float(x));
        v += k * l.power;
    }
    int iv = int(v + 0.5f);
    return iv > 4 ? 4 : iv;
}

// ---------------------------------------------------------------- art

// ASCII art: rows split by '\n', 4 px per column, `dy` px per row. Spaces
// are see-through, '@' is a solid dark block that hides what is behind.
void art(int x, int y, const char *s, int shade, bool lit = true, int dy = 6) {
    int col = 0, row = 0;
    for(const char *p = s; *p; p++) {
        if(*p == '\n') { row++; col = 0; continue; }
        int cx = x + col * 4, cy = y + row * dy;
        col++;
        if(*p == ' ') continue;
        if(cx < -4 || cx >= SCREEN || cy < -7 || cy >= SCREEN) continue;
        if(*p == '@') {
            screen.pen = Pen(0, 0, 0);
            screen.rectangle(Rect(cx, cy, 4, dy));
            continue;
        }
        int l = lit ? light_at(cx + 1, cy + 3) : 0;
        font::ch(cx, cy, *p, shade + l);
    }
}

struct Part { int8_t dx, dy; char c; int8_t shade; };

void draw_parts(const Part *parts, int n, int x, int y, int dir, int base_shade) {
    for(int i = 0; i < n; i++) {
        const Part &p = parts[i];
        char c = p.c;
        int dx = p.dx;
        if(dir < 0) {
            dx = 8 - dx;
            if(c == '/') c = '\\'; else if(c == '\\') c = '/';
            else if(c == '(') c = ')'; else if(c == ')') c = '(';
            else if(c == '<') c = '>'; else if(c == '>') c = '<';
        }
        int s = base_shade + p.shade;
        if(s < 1) s = 1;
        font::ch(x + dx - 4, y + p.dy, c, s);
    }
}

// Faces are small pixel pictures, not letters: an 'o' for a head read as a
// light bulb. Drawn facing right, 8 px wide, centred on the body; dir < 0
// mirrors them.
const char *const FACE_SZARY[] = {
    ".HSSSSS.",
    ".HSSSES.",
    ".HSSSSSS",
    ".HSSSs..",
    "..SSs...",
    "...ss...",
};
const char *const FACE_HALINA[] = {
    "..HHHH..",
    ".HHHHHH.",
    "HHHSSSS.",
    "HHHSSES.",
    "HHHSSSSS",
    ".HHSSs..",
    "..HHss..",
    "...HH...",
};
const char *const FACE_KUBA[] = {
    "..HHHH..",
    ".HHHHHH.",
    "HHHHSSS.",
    "HHHSSES.",
    "HHHSSSSS",
    "HHHSSs..",
    ".HHH....",
};
const char *const FACE_RYSIEK[] = {
    "..SSSS..",
    ".SSSSSS.",
    ".SSSSES.",
    ".HSSSSSS",
    ".HSHHHH.",
    ".SSSSS..",
    "..sss...",
};
const char *const FACE_JASINSKI[] = {
    "..HHHH..",
    ".hhhhhh.",
    ".HHHHHHH",
    ".HSSSES.",
    ".HSSSSSS",
    ".HHSSSS.",
    ".HHHHHH.",
    "..HHHH..",
};
const char *const FACE_BOGDAN[] = {  // lying on his back: crown left, nose up
    "....S...",
    ".HSESS..",
    "HHSSSSS.",
    ".HHSSs..",
};

template <int N>
void face(const char *const (&rows)[N], int x, int y, int dir, int skin, int hair) {
    font::pixels(x - 3, y, rows, N, dir, skin, hair);
}

// ---------------------------------------------------------------- sky, sea

void draw_sky(bool clouds_heavy) {
    bool dawn = st.chapter >= 5;
    if(dawn) {
        // the sky greys from the land side: rows of dots, denser low down
        for(int y = 40; y < int(HORIZON); y += 3) {
            int dens = (y - 40) / 6 + 1;
            for(int x = (y / 3) % 4; x < SCREEN; x += 9 - dens) font::micro(x, y, '.', 1 + (y > 60));
        }
    }
    for(auto &s : stars) {
        if(clouds_heavy && s.kind > 1) continue;
        if(dawn && (s.y > 30 || s.kind > 2)) continue;
        int x = int(s.x) - int(cam * P_SKY);
        if(x < -2 || x >= SCREEN) continue;
        float tw = 0.5f + 0.5f * sinf(t * (0.7f + float(s.kind) * 0.3f) + s.phase);
        int shade = 1 + int(tw * (s.kind < 2 ? 3.0f : 1.6f));
        font::micro(x, s.y, s.kind == 0 ? '+' : (s.kind == 1 ? ':' : '.'), shade);
    }
    if(!clouds_heavy && show_player) {  // no moon behind the title letters
        int mx = 24 - int(cam * P_SKY), my = dawn ? 46 : 18;
        int dim = dawn ? 2 : 0;
        for(int y = -9; y <= 9; y += 2)
            for(int x = -9; x <= 9; x += 2) {
                float d = sqrtf(float(x * x + y * y));
                if(d > 9.2f) continue;
                bool sea = (x > -6 && x < 0 && y > -6 && y < 0) || (x > 1 && x < 6 && y > 1 && y < 6);
                font::micro(mx + x, my + y - 2, '.', (sea ? 4 : (d > 7.5f ? 5 : 7)) - dim);
            }
        if(!dawn)
            for(int a = 0; a < 16; a++) {
                float ang = float(a) * 0.3927f + t * 0.02f;
                font::micro(mx + int(cosf(ang) * 13.0f), my + int(sinf(ang) * 12.0f), '.', 2 + (a % 3 == 0));
            }
    }
    int nclouds = clouds_heavy ? 8 : 4;
    float wm = wind_mult();
    for(int k = 0; k < nclouds; k++) {
        float cx = fmodf(float(k) * 61.0f + t * (3.0f + float(k)) * wm - cam * 0.08f, 220.0f) - 50.0f;
        int cy = 6 + (k % 5) * 8 + (k > 4 ? 3 : 0);
        int len = 6 + int(hash(uint32_t(k) * 7u) % 6u) + (clouds_heavy ? 6 : 0);
        for(int i = 0; i < len; i++) {
            char c = (i == 0 || i == len - 1) ? '.' : ((i + k) % 3 ? '-' : '~');
            font::micro(int(cx) + i * 3, cy + ((i * 5 + k) % 3 == 0 ? 1 : 0), c, k == 1 ? 2 : 1);
        }
    }
    if(!clouds_heavy) {
        float bx = fmodf(t * 14.0f, 260.0f) - 40.0f;
        font::ch(int(bx), 30 + int(3.0f * sinf(t * 1.7f)), 'v', 2);
    }
}

// The Aurora's lights on the horizon in the last chapter.
void draw_aurora() {
    if(st.chapter < 5) return;
    float k = aurora;
    int x = 124 - int(k * 70.0f);
    int y = int(HORIZON) - 2;
    if(flag(F_RELIT) && beam > 2.0f) {
        // she turns: the lights pull apart and drift off
        float turn = fminf((beam - 2.0f) / 6.0f, 1.0f);
        x += int(turn * 30.0f);
        y -= int(turn * 2.0f);
    }
    // a dark hull with a mast under the lights
    for(int i = -3; i <= 3; i++) font::micro(x + i * 3, y + 1, '_', 2);
    font::micro(x, y - 6, '|', 2);
    bool blink = fmodf(t, 1.2f) < 0.9f;
    font::micro(x, y - 4, '.', 6);
    font::micro(x + 4, y - 2, '.', blink ? 7 : 4);
    font::micro(x - 4, y - 2, '.', 5);
    font::micro(x - 1, y, '-', 3);
    for(int r = 0; r < 3; r++) font::micro(x + int(sinf(t * 3.0f + float(r)) * 2.0f), y + 4 + r * 3, '-', 3 - r / 2);
}

void draw_far_lighthouse(float world_x) {
    int lx = sx_of(world_x, P_SEA), base = int(HORIZON) + 2;
    if(lx < -20 || lx > SCREEN + 8) return;
    for(int i = 0; i < 9; i++) font::micro(lx - 10 + i * 3, base - (i > 2 && i < 7 ? 2 : 0), i > 2 && i < 7 ? '_' : '.', 2);
    static const char *tower[] = {" ^ ", "[.]", "|#|", "|:|", "|#|", "|:|", "/#\\"};
    for(int r = 0; r < 7; r++)
        for(int c = 0; c < 3; c++) {
            char ch = tower[r][c];
            if(ch == ' ') continue;
            int s = ch == '.' ? 2 : 3;
            if(r == 1 && beam > 0.0f) s = 7;
            font::ch(lx - 6 + c * 4, base - 34 + r * 5, ch, s);
        }
}

void draw_sea(float speed_mult) {
    int moon_x = 25 - int(cam * P_SKY);
    bool moonlit = !raining();
    for(int row = 0; row < 6; row++) {
        int y = int(HORIZON) + row * 2 + (row * row) / 3;
        float speed = (4.0f + float(row) * 2.0f) * speed_mult;
        float off = t * speed - cam * (P_SEA + float(row) * 0.04f);
        int step = 6 + row;
        int start = -int(fmodf(off, float(step))) - step;
        for(int x = start; x < SCREEN; x += step) {
            int cell = int(floorf((float(x) + off) / float(step)));
            uint32_t h = hash(uint32_t(cell * 7 + row * 1013));
            if(h % 3 == 0) continue;
            int shade = 2 + (row > 3 ? 1 : 0);
            int dm = abs(x - moon_x);
            if(moonlit && dm < 10 + row * 2) shade += 2 + ((h >> 4) + uint32_t(t * 6.0f)) % 2;
            if(raining() && (h & 3) == 1) shade++;  // whitecaps
            font::micro(x, y, (h & 16) ? '~' : '-', shade);
        }
    }
}

// ---------------------------------------------------------------- grass

void draw_blade_micro(const wind::Blade &b, int rx, int ry, int shade) {
    int pieces = int(b.height / 3.0f);
    float prev = 0.0f;
    for(int i = 1; i <= pieces; i++) {
        float off = wind::bend_offset(b, float(i) / float(pieces));
        float d = off - prev;
        char c = d > 0.5f ? ',' : (d < -0.5f ? '`' : '|');
        font::micro(rx + int(floorf(off)), ry - i * 3, c, shade);
        prev = off;
    }
}

void draw_blade(const wind::Blade &b, int rx, int ry, int shade, bool lightable) {
    constexpr int STEP = 3;
    int pieces = int(b.height / float(STEP));
    if(pieces < 2) pieces = 2;
    float prev = 0.0f;
    for(int i = 1; i <= pieces; i++) {
        float off = wind::bend_offset(b, float(i) / float(pieces));
        char c = wind::stem_char((off - prev) * 1.6f);
        if(i == pieces && c == '|') c = '\'';
        int x = rx + int(floorf(off + 0.5f)) - 1;
        int y = ry - i * STEP - 2;
        int s = shade - (i == pieces ? 1 : 0);
        if(lightable) s += light_at(x + 1, y + 3);
        font::ch(x, y, c, s);
        prev = off;
    }
}

void draw_back_dunes() {
    float off = cam * P_BACK;
    int start = -int(fmodf(off, 4.0f)) - 4;
    for(int x = start; x < SCREEN; x += 4) {
        float wx = float(x) + off;
        int top = int(back_dune_y(wx));
        font::micro(x, top, '_', 2);
        int col = int(floorf(wx / 4.0f));
        for(int y = top + 3; y < SCREEN; y += 3) {
            uint32_t h = hash(uint32_t(col * 131 + y));
            if(h % 3) continue;
            font::micro(x + int(h >> 30), y, '.', 1);
        }
    }
    for(auto &b : back) {
        int rx = int(floorf(b.x - off));
        if(rx < -10 || rx > SCREEN + 10) continue;
        draw_blade_micro(b, rx, int(back_dune_y(b.x)) + 1, 2);
    }
}

void draw_ground() {
    int start = -int(fmodf(cam, 4.0f)) - 4;
    bool wet = raining();
    for(int x = start; x < SCREEN; x += 4) {
        float wx = float(x) + cam;
        int top = int(ground_y(wx));
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(x, top, 4, SCREEN - top));
        int col = int(floorf(wx / 4.0f));
        for(int y = top; y < SCREEN; y += 3) {
            uint32_t h = hash(uint32_t(col * 977 + (y - top) * 31));
            int depth = (y - top) / 3;
            int l = light_at(x + 1, y + 1);
            if(depth > 0 && h % (l > 0 ? 2u : 4u)) continue;
            char c = depth == 0 ? '_' : ((h >> 8) % 4 == 0 ? ':' : (wet && (h >> 12) % 3 == 0 ? ',' : '.'));
            int shade = (depth == 0 ? 2 : 1) + l;
            font::micro(x + int((h >> 20) & 1), y, c, shade);
        }
    }
}

void draw_mid_grass(int parity) {
    for(int i = 0; i < MID_BLADES; i++) {
        if((i & 1) != parity) continue;
        auto &b = mid[i];
        int rx = sx_of(b.x, 1.0f);
        if(rx < -24 || rx > SCREEN + 24) continue;
        draw_blade(b, rx, int(ground_y(b.x)) + 2, parity ? 2 : 1, true);
    }
}

void draw_front_grass() {
    for(auto &b : front) {
        int rx = sx_of(b.x, P_FRONT);
        if(rx < -30 || rx > SCREEN + 30) continue;
        if(sinf(b.x * 0.021f) < 0.15f) continue;
        draw_blade(b, rx, SCREEN + 6, 1, true);
    }
}

// ---------------------------------------------------------------- weather

void draw_rain() {
    if(!raining()) return;
    float w = wind_at(st.x);
    char c = w > 1.2f ? '`' : '|';
    for(auto &d : drops) {
        int x = int(d.x), y = int(d.y);
        int l = light_at(x, y);
        font::micro(x, y, c, 1 + (l > 0 ? l + 1 : 0) + ((x + y) % 5 == 0));
    }
}

void draw_grains() {
    for(auto &g : grains) {
        float w = wind_at(cam + g.x);
        int x = int(g.x), y = int(g.y) - 2;
        font::micro(x, y, w > 0.8f ? '-' : '.', 2 + light_at(x, y));
    }
}

void draw_motes() {
    if(!st.torch) return;
    for(auto &m : motes) {
        int x = int(m.x), y = int(m.y);
        int l = light_at(x, y);
        if(l >= 2) font::micro(x, y, raining() ? '|' : '.', l + 1);
    }
}

// The beam of the relit lighthouse sweeps round: from the side it reaches
// out to one side, shrinks, and reaches out to the other.
void draw_beam(int lx, int ly) {
    if(beam <= 0.0f) return;
    float a = beam * 1.3f;
    float reach = cosf(a);
    int len = 14 + int(fabsf(reach) * 120.0f);
    int dir = reach > 0 ? 1 : -1;
    for(int i = 6; i < len; i += 3) {
        int spread = i / 14;
        for(int s = -spread; s <= spread; s += 2) {
            int shade = 6 - i * 4 / (len + 1) - (s != 0);
            font::micro(lx + dir * i, ly + s - i / 30, '-', shade);
        }
    }
    font::ch(lx - 2, ly - 3, '*', 7);
}

// ---------------------------------------------------------------- people

void draw_szary(int sx, int feet) {
    int top = feet - 33;
    static const Part body[] = {
        {3, 0, SOLID, -3}, {5, 0, SOLID, -3},
        {0, 3, '-', 0}, {3, 3, '-', 0}, {6, 3, '-', 0},
        {2, 12, SOLID, -3}, {5, 12, SOLID, -3},
        {2, 16, SOLID, -3}, {5, 16, SOLID, -3},
        {2, 20, SOLID, -3}, {5, 20, SOLID, -3},
        {0, 11, '/', 0}, {7, 11, '\\', 0},
        {0, 15, '|', -1}, {0, 19, '|', -1},
        {7, 15, '-', 1},
    };
    int base = 5 + (outdoors(st.scene) || st.scene == S_BUNKER || st.scene == S_LIGHTHOUSE ? 0 : 1);
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), sx, top, st.facing, base);
    face(FACE_SZARY, sx, top + 7, st.facing, base, base - 3);
    if(st.torch) {
        static const Part lamp[] = {{10, 14, '=', 0}};
        draw_parts(lamp, 1, sx, top, st.facing, 7);
    }
    float w = outdoors(st.scene) ? wind_at(st.x) : 0.0f;
    int lift = w > 1.0f ? 2 : (w > 0.6f ? 1 : 0);
    char tail = lift == 2 ? '_' : (lift == 1 ? (st.facing > 0 ? '\\' : '/') : '|');
    int tx = st.facing > 0 ? sx - 4 - lift * 2 : sx + 4 + lift * 2;
    font::ch(tx, top + 22 - lift * 2, tail, 3);

    static const char legs[4][2] = {{'/', '\\'}, {'|', '\\'}, {'|', '|'}, {'/', '|'}};
    int frame = st.vx == 0.0f ? 2 : int(st.walk / 5.0f) % 4;
    char l = legs[frame][0], r = legs[frame][1];
    if(st.facing < 0) {
        auto flip = [](char c) { return c == '/' ? '\\' : (c == '\\' ? '/' : c); };
        char tmp = l; l = flip(r); r = flip(tmp);
    }
    font::ch(sx - 3, feet - 7, l, 4);
    font::ch(sx + 1, feet - 7, r, 4);
}

void draw_halina() {
    int hx = sx_of(HALINA_X, 1.0f);
    int feet = int(ground_y(HALINA_X)) + 1;
    if(hx < -20 || hx > SCREEN + 20) return;
    int top = feet - 24;
    int lit = light_at(hx, top + 10);
    static const Part body[] = {
        {3, 9, SOLID, -2}, {5, 9, SOLID, -2}, {1, 9, '/', 0}, {7, 9, '\\', 0},
        {3, 13, SOLID, -2}, {5, 13, SOLID, -2},
        {0, 17, '/', 0}, {3, 17, SOLID, -2}, {5, 17, SOLID, -2}, {8, 17, '\\', 0},
    };
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), hx, top, 1, 2 + lit);
    face(FACE_HALINA, hx, top + 1, 1, 3 + lit, 2 + lit);
    int sway = sinf(t * 1.5f) > 0.6f ? 1 : 0;
    font::ch(hx + 4 + sway, top + 10, '-', 2 + lit);
}

void draw_barrel() {
    int bx = sx_of(BARREL_X, 1.0f);
    int bfeet = int(ground_y(BARREL_X)) + 1;
    if(bx < -20 || bx > SCREEN + 30) return;
    static const char *barrel[] = {"[=]", "[#]"};
    for(int r = 0; r < 2; r++)
        for(int c = 0; c < 3; c++) font::ch(bx - 2 + c * 3, bfeet - 12 + r * 5, barrel[r][c], 3);
    bool out = flag(F_RELIT) && st.chapter >= 5;
    float w = wind_at(BARREL_X);
    if(out) {
        // only smoke now
        for(int i = 0; i < 4; i++) {
            float life = fmodf(t * 0.5f + float(i) * 0.25f, 1.0f);
            font::micro(bx + 2 + int(life * w * 20.0f), bfeet - 16 - int(life * 18.0f), '~', 2);
        }
        return;
    }
    for(int i = 0; i < 5; i++) {
        float fl = sinf(t * (9.0f + float(i) * 3.7f) + float(i) * 1.9f);
        int h = 3 + int(5.0f * (0.5f + 0.5f * fl)) - (i == 0 || i == 4 ? 2 : 0);
        if(st.chapter == 4) h += 3;  // the storm feeds it
        int fx = bx - 2 + i * 2 + int(w * 1.5f * float(1 + (h > 4)));
        font::ch(fx, bfeet - 15 - h, i % 2 ? '^' : (fl > 0 ? '*' : '\''), 5 + (fl > -0.2f ? 2 : 1));
        if(h > 5) font::ch(fx + int(w * 2.0f), bfeet - 19 - h, '\'', 4);
    }
    for(int i = 0; i < 6; i++) {
        float life = fmodf(t * 0.9f + float(i) * 0.37f, 1.6f);
        int x = bx + 2 + int(life * (12.0f + w * 30.0f) + 3.0f * sinf(t * 3.0f + float(i)));
        int y = bfeet - 18 - int(life * 22.0f);
        font::micro(x, y, '.', life < 0.9f ? 6 : 3);
    }
}

// ---------------------------------------------------------------- dunes

void draw_car() {
    int x = sx_of(CAR_X, 1.0f) - 26;
    if(x < -60 || x > SCREEN) return;
    int y = int(ground_y(CAR_X)) - 22;
    art(x, y,
        "   ______\n"
        " _/ |  | \\__\n"
        "|=@@@@@@@@@=|\n"
        "'-(o)---(o)-'", 2);
}

void draw_lighthouse() {
    int x = sx_of(LH_X, 1.0f) - 20;
    if(x < -50 || x > SCREEN + 10) return;
    int base = int(ground_y(LH_X)) + 2;
    const char *tower =
        "   _^_\n"
        "  [@@@]\n"
        " _|###|_\n"
        "  |@@@|\n"
        "  |@:@|\n"
        "  |@@@|\n"
        "  |@.@|\n"
        "  |@@@|\n"
        "  |@:@|\n"
        "  |@@@|\n"
        " /@@@@@\\\n"
        "/@@|=|@@\\\n"
        "@@@| |@@@";
    art(x, base - 13 * 6, tower, 2);
    // the lantern: dark, or burning when relit
    int ly = base - 13 * 6 + 6;
    bool lit = beam > 0.0f;
    for(int c = 0; c < 3; c++) font::ch(x + 12 + c * 4, ly, lit ? SOLID : '.', lit ? 7 : 2);
    if(lit) draw_beam(x + 16, ly + 2);
}

void draw_bike() {
    int x = sx_of(BIKE_X, 1.0f) - 12;
    if(x < -30 || x > SCREEN) return;
    int y = int(ground_y(BIKE_X)) - 12;
    art(x, y, "  _\\_\n(o)-(o)", 3);
}

void draw_bunker() {
    int x = sx_of(BUNKER_X, 1.0f) - 28;
    if(x < -60 || x > SCREEN) return;
    int y = int(ground_y(BUNKER_X)) - 26;
    art(x, y,
        "   _________\n"
        "  /=========\\\n"
        " /@@.-----.@@\\\n"
        "|@@@|@@@@@|@@@|\n"
        "|@@@|@@@@@|@@@|", 2);
    // door: padlock, or open dark mouth
    int dx = x + 20, dy = y + 18;
    if(flag(F_BUNKER_OPEN)) font::ch(dx + 4, dy, ' ', 0);
    else if(st.chapter >= 4) font::ch(dx + 6, dy, 'o', 3 + light_at(dx + 6, dy + 3));
    // sand drifted against it
    for(int i = 0; i < 14; i++) font::micro(x + i * 4, y + 27 + (i % 3), '.', 2);
}

// ---------------------------------------------------------------- pier

void draw_pier() {
    int land_end = sx_of(float(PIER_FROM), 1.0f);
    // the harbour office: a small house with a lit window
    int ox = sx_of(40.0f, 1.0f) - 22;
    if(ox > -60 && ox < SCREEN) {
        art(ox, 104 - 7 * 6,
            "    /\\\n"
            "   /@@\\____\n"
            "  /@@@@@@@@\\\n"
            "  |@[#]@@@@|\n"
            "  |@@@@@|=||\n"
            "  |@@@@@| ||\n"
            "  |@@@@@|_||", 2);
        // the window glows
        font::ch(ox + 16, 104 - 4 * 6, '#', 5);
        // rain on the window
        if(fmodf(t * 3.0f, 1.0f) < 0.5f) font::micro(ox + 17, 104 - 4 * 6 + 2, '|', 3);
    }
    // the shore
    for(int x = -4 - int(fmodf(cam, 4.0f)); x < SCREEN && x < land_end; x += 4) {
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(x, 104, 4, SCREEN - 104));
        for(int y = 104; y < SCREEN; y += 4) font::micro(x + ((y / 4) & 1), y, y == 104 ? '_' : '.', y == 104 ? 2 : 1);
    }
    // planks and posts
    int start = land_end > 0 ? land_end : -int(fmodf(cam - float(PIER_FROM), 8.0f));
    int end = sx_of(410.0f, 1.0f);
    for(int x = start; x < SCREEN && x < end; x += 4) {
        float wx = float(x) + cam;
        font::ch(x, 97, '=', 3 + light_at(x + 1, 100));
        if(int(wx) % 32 < 4) {
            for(int y = 103; y < 126; y += 6) font::ch(x, y, '|', 2);
        }
    }
    // water under the pier
    for(int x = -int(fmodf(t * 6.0f, 6.0f)); x < SCREEN; x += 6) {
        if(x < land_end) continue;
        font::micro(x, 120 + int(sinf(t * 2.0f + float(x) * 0.2f) * 1.5f), '~', 2);
    }
    // Rysiek's boat rocks next to the pier
    int bx = sx_of(292.0f, 1.0f) - 20;
    if(bx > -50 && bx < SCREEN) {
        int bob = int(sinf(t * 1.6f) * 2.0f);
        art(bx, 104 + bob,
            "  _|___\n"
            "\\@@####@/\n"
            " \\@@@@@/", 2);
    }
    // the lamp at the end of the pier
    int lx = sx_of(404.0f, 1.0f);
    if(lx > -10 && lx < SCREEN + 10) {
        for(int y = 72; y < 97; y += 6) font::ch(lx, y, '|', 3);
        font::ch(lx, 66, '*', 6 + (fmodf(t, 2.3f) < 2.0f));
    }
}

// ---------------------------------------------------------------- rooms

void room_walls(int width, int shade) {
    int left = -int(cam);
    int x0 = left < 0 ? 0 : left, x1 = left + width > SCREEN ? SCREEN : left + width;
    if(x1 <= x0) return;
    // a plain back wall: ceiling, a wainscot rail and the floor as clean
    // lines (a dotted brick texture looked like noise on the OLED)
    font::hline(x0, 28, x1 - x0, shade + 1);
    font::hline(x0, 86, x1 - x0, shade);
    for(int x = left + 2; x < left + width; x += 16)
        if(x >= 0 && x < SCREEN) font::micro(x, 88, '|', shade);
    font::hline(x0, ROOM_FLOOR + 2, x1 - x0, 2);
}

void draw_station() {
    room_walls(128, 1);
    int o = -int(cam);
    // window: the dark night, and where the lighthouse should blink
    art(o + 4, 44,
        "+-----+\n"
        "|     |\n"
        "|     |\n"
        "|-----|\n"
        "|     |\n"
        "+-----+", 2);
    font::micro(o + 10, 53, '.', 3);
    font::micro(o + 22, 51, flag(F_RELIT) ? '+' : '.', flag(F_RELIT) ? 7 : 1);
    for(int i = 0; i < 4; i++) font::micro(o + 9 + i * 5, 58, '~', 2);
    // desk with a lamp
    art(o + 28, 66,
        "   _o_\n"
        "  /___\\  =\n"
        "=========\n"
        "||     ||\n"
        "||     ||\n"
        "||     ||", 3);
    // telex
    art(o + 64, 70,
        " .------.\n"
        " |::::::|\n"
        " |[====]|\n"
        " '------'\n"
        "  ||  ||\n"
        "  ||  ||", 3);
    if(!flag(F_TELEX)) {
        // paper creeps out, the keys chatter
        int n = int(t * 2.0f) % 4;
        for(int i = 0; i <= n; i++) font::ch(o + 72 + (i % 2), 64 - i * 4, '|', 6);
        if(fmodf(t, 0.25f) < 0.12f) font::ch(o + 80, 76, '#', 6);
    } else {
        font::ch(o + 72, 64, '|', 5);
    }
    // door with a coat stand
    art(o + 104, 76,
        ".---.\n"
        "|   |\n"
        "|  o|\n"
        "|   |\n"
        "|   |\n"
        "|   |", 3);
    art(o + 94, 74, " Y\n |\n |\n |\n |\n/ \\", 2);
}

void draw_lighthouse_inside() {
    room_walls(128, 1);
    int o = -int(cam);
    // door (moonlight through it)
    art(o + 2, 78, ".--.\n|  |\n|  |\n|  |\n|  |\n|  |", 3);
    for(int y = 82; y < 112; y += 4) font::micro(o + 8, y, '.', 2);
    // table with the log
    art(o + 22, 90, "  [=]\n=======\n||   ||\n||   ||", 3);
    // spiral stairs going up and round
    for(int i = 0; i < 12; i++) {
        int sx = o + 50 + (i % 6) * 6 + (i >= 6 ? 4 : 0);
        int sy = ROOM_FLOOR - 4 - i * 7;
        font::ch(sx, sy, i % 6 == 5 ? '|' : '_', 2 + light_at(sx + 1, sy + 3));
    }
    // cap on the stairs
    font::ch(o + 56, ROOM_FLOOR - 14, SOLID, 2 + light_at(o + 57, ROOM_FLOOR - 11));
    font::ch(o + 60, ROOM_FLOOR - 13, '-', 3 + light_at(o + 61, ROOM_FLOOR - 10));
    // up there, the lens
    art(o + 66, 30, " .-.\n(   )\n '-'", 3);
    font::ch(o + 74, 36, flag(F_RELIT) ? SOLID : '.', flag(F_RELIT) ? 7 : 2 + light_at(o + 75, 39));
    // fuse box with the main switch
    art(o + 98, 70, ".----.\n|[ ][]|\n|  ||  |\n'----'", 3);
    bool fuse_in = flag(F_RELIT);
    font::ch(o + 106, 76, fuse_in ? '#' : ' ', 6);
}

void draw_bar() {
    room_walls(170, 1);
    int o = -int(cam);
    // two bulbs on swinging cords
    for(int k = 0; k < 2; k++) {
        int bx = o + 36 + k * 74 + int(sinf(t * 0.8f + float(k)) * 1.5f);
        for(int y = 30; y < 44; y += 4) font::micro(bx + 1, y, '|', 2);
        font::ch(bx, 44, 'o', 7);
    }
    // door
    art(o + 0, 78, ".---.\n|   |\n|  o|\n|   |\n|   |\n|   |", 3);
    // shelves with bottles behind the counter
    art(o + 18, 50, "=========\n i!ii!i\n=========", 2);
    // radio on its shelf
    art(o + 48, 62, ".----.\n|o::o|\n'----'", 3);
    // the note the radio plays
    if(fmodf(t, 1.5f) < 0.75f) font::ch(o + 64, 56, '~', 3);
    // photo on the wall
    art(o + 74, 54, "+---+\n|^~^|\n+---+", 3);
    // window with the sea; a boat with no lights
    art(o + 140, 52, "+------+\n|      |\n|      |\n+------+", 2);
    for(int i = 0; i < 5; i++) font::micro(o + 145 + i * 4, 62 + (i & 1), '~', 2);
    if(fmodf(t, 3.0f) < 2.2f) font::micro(o + 156 + int(sinf(t * 0.3f) * 3.0f), 59, '.', 6);
    // table and a stool for Kuba
    art(o + 96, 98, "=======\n||   ||", 3);
    // the boots under the table
    art(o + 120, 108, "_)_)", 3);
}

void draw_counter() {
    int o = -int(cam);
    // solid front, so Rysiek stands right behind it with no gap
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(o + 14, 92, 48, ROOM_FLOOR + 2 - 92));
    font::hline(o + 13, 92, 50, 4);
    art(o + 14, 94, "|          |\n|          |\n|__________|", 3);
}

void draw_office() {
    room_walls(128, 1);
    int o = -int(cam);
    art(o + 2, 78, ".--.\n|  |\n| o|\n|  |\n|  |\n|  |", 3);
    // rain on the window
    art(o + 18, 40, "+----+\n|    |\n|    |\n+----+", 2);
    for(int i = 0; i < 3; i++) {
        int ry = 44 + int(fmodf(t * 18.0f + float(i) * 5.0f, 14.0f));
        font::micro(o + 22 + i * 4, ry, '|', 3);
    }
    // desk, lamp, kettle with steam
    art(o + 30, 92, "=========\n||     ||\n||     ||", 3);
    art(o + 52, 80, " _\n(_)", 3);
    for(int i = 0; i < 3; i++) font::micro(o + 54 + int(sinf(t * 2.0f + float(i)) * 2.0f), 74 - i * 4, '~', 2);
    // timetable
    art(o + 62, 46, "+------+\n|AURORA|\n| 05:40|\n+------+", 2);
    // sea chart with the sandbank
    art(o + 86, 46, "+-----+\n|~ ~ ~|\n| ::  |\n|~ ~ ~|\n+-----+", 2);
    // archive cabinet
    art(o + 108, 80, "+---+\n|[-]|\n|[-]|\n|[-]|\n|[-]|", 3);
}

void draw_bunker_inside() {
    int o = -int(cam);
    // concrete vault, almost black; the torch shows it
    for(int y = 30; y < ROOM_FLOOR; y += 5)
        for(int x = o; x < o + 128; x += 6) {
            int l = light_at(x + 1, y + 1);
            if(l == 0 && (hash(uint32_t(x - o) * 13u + uint32_t(y)) % 4)) continue;
            font::micro(x + ((y / 5) & 1) * 3, y, ':', 1 + l);
        }
    for(int x = o; x < o + 128; x += 4) font::micro(x, ROOM_FLOOR, '_', 1 + light_at(x, ROOM_FLOOR));
    // the open door and the storm outside
    art(o + 2, 78, ".--.\n|  |\n|  |\n|  |\n|  |\n|  |", 2);
    for(int i = 0; i < 4; i++) font::micro(o + 6, 82 + int(fmodf(t * 40.0f + float(i) * 7.0f, 28.0f)), '`', 2);
    // a drip
    float d = fmodf(t, 1.7f);
    font::micro(o + 88, 32 + int(d * 48.0f), '.', d < 1.6f ? 2 + light_at(o + 88, 32 + int(d * 48.0f)) : 0);
    // shelf
    art(o + 96, 80, "=======\n||   ||", 2);
    if(!has(clue::fuse)) font::ch(o + 104, 76, '#', 2 + light_at(o + 105, 79) + 1);
}

// Kuba sits at the table, hood up, one leg jiggling.
void draw_kuba() {
    int x = -int(cam) + 108;
    int top = ROOM_FLOOR - 28;
    static const Part body[] = {
        {2, 9, SOLID, -2}, {5, 9, SOLID, -2}, {0, 9, '/', 0},
        {2, 13, SOLID, -2}, {5, 13, SOLID, -2},
    };
    int kl = light_at(x, top + 8);
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), x, top, -1, 4 + kl);
    face(FACE_KUBA, x, top + 1, -1, 5 + kl, 2 + kl);
    int jig = fmodf(t, 0.4f) < 0.2f ? 1 : 0;
    font::ch(x - 4, top + 18 - jig, '/', 3);
    font::ch(x, top + 18, '|', 3);
    // a glass on the table
    font::ch(x - 12, top + 14, 'u', 4);
}

// Rysiek behind the counter, wiping a glass.
void draw_rysiek() {
    int x = -int(cam) + 30;
    int top = ROOM_FLOOR - 40;
    static const Part body[] = {
        {0, 12, '/', 0}, {2, 12, SOLID, -2}, {5, 12, SOLID, -2}, {8, 12, '\\', 0},
        {2, 16, SOLID, -2}, {5, 16, SOLID, -2},
    };
    int rl = light_at(x, top + 8);
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), x, top, 1, 4 + rl);
    face(FACE_RYSIEK, x, top + 4, 1, 5 + rl, 2 + rl);
    int wipe = int(sinf(t * 4.0f) * 2.0f);
    font::ch(x + 8 + wipe, top + 15, 'u', 5);
}

// Captain Jasiński at his desk, cap and beard.
void draw_jasinski() {
    int x = -int(cam) + 44;
    int top = ROOM_FLOOR - 34;
    static const Part body[] = {
        {2, 13, SOLID, -2}, {5, 13, SOLID, -2}, {0, 13, '/', 0}, {8, 13, '\\', 0},
    };
    int jl = light_at(x, top + 8);
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), x, top, 1, 4 + jl);
    face(FACE_JASINSKI, x, top + 4, 1, 5 + jl, 3 + jl);
}

// Bogdan lying on the floor of the bunker, under a blanket once covered.
void draw_bogdan() {
    int x = -int(cam) + 46;
    int y = ROOM_FLOOR - 8;
    bool covered = flag(F_BOGDAN1);
    const char *s = covered ? " @~~~~~" : " =====<";
    for(int i = 1; s[i]; i++) {
        char c = s[i] == '@' ? SOLID : s[i];
        int l = light_at(x + i * 4 + 1, y + 3);
        font::ch(x + i * 4, y, c, 2 + l);
    }
    int hl = light_at(x + 2, y + 3);
    face(FACE_BOGDAN, x + 2, y + 2, 1, 3 + hl, 1 + hl);
    // breathing
    if(fmodf(t, 3.0f) < 1.5f) font::micro(x - 4, y - 2, '.', 1 + light_at(x - 4, y));
}

// ---------------------------------------------------------------- update

void ambience(float dt) {
    // sand grains (screen space)
    for(int i = 0; i < GRAINS; i++) {
        auto &g = grains[i];
        float w = wind_at(cam + g.x);
        g.x += (w * 95.0f - 10.0f) * dt;
        g.vy += (hashf(uint32_t(t * 50.0f) + uint32_t(i) * 31u) - 0.5f) * 60.0f * dt;
        g.vy *= 0.97f;
        g.y += g.vy * dt;
        float gy = ground_y(cam + g.x);
        if(g.y > gy) { g.y = gy; g.vy = -fabsf(g.vy); }
        if(g.x > SCREEN + 4 || g.x < -8 || g.y < gy - 40.0f) {
            g.x = -4.0f - hashf(uint32_t(t * 100.0f) + uint32_t(i)) * 30.0f;
            g.y = ground_y(cam) - hashf(uint32_t(i) * 17u + uint32_t(t)) * 24.0f;
            g.vy = 0;
        }
    }
    for(int i = 0; i < MOTES; i++) {
        auto &m = motes[i];
        float w = wind_at(cam + m.x);
        m.x += (w * 40.0f) * dt - st.vx * dt;
        m.y += sinf(t * 1.3f + float(i)) * 6.0f * dt;
        if(m.x > SCREEN + 2) m.x -= SCREEN + 8;
        if(m.x < -6) m.x += SCREEN + 8;
    }
    if(raining()) {
        float w = wind_at(st.x);
        for(int i = 0; i < DROPS; i++) {
            auto &d = drops[i];
            d.y += 170.0f * dt;
            d.x += (w * 60.0f) * dt - st.vx * dt;
            if(d.y > SCREEN || d.x > SCREEN + 4 || d.x < -6) {
                d.y = -4.0f - hashf(uint32_t(i) * 7u + uint32_t(t * 30.0f)) * 30.0f;
                d.x = hashf(uint32_t(i) * 13u + uint32_t(t * 50.0f)) * (SCREEN + 40) - 30.0f;
            }
        }
    }
}

}  // namespace

// ================================================================ API

void init() {
    field.reset(-60.0f);
    place_blades(mid, MID_BLADES, 0.0f, DUNES_W, 9.0f, 19.0f, 1.0f, 100);
    place_blades(back, BACK_BLADES, 0.0f, DUNES_W * P_BACK + SCREEN, 4.0f, 8.0f, 1.2f, 900);
    place_blades(front, FRONT_BLADES, 0.0f, DUNES_W * P_FRONT + SCREEN, 16.0f, 30.0f, 0.8f, 1700);
    // keep the grass clear of the lighthouse, car and bunker walls
    for(auto &b : mid) {
        if(fabsf(b.x - LH_X) < 18.0f || fabsf(b.x - CAR_X) < 24.0f || fabsf(b.x - BUNKER_X) < 26.0f) b.height = 0.0f;
    }
    lay_trail();
    for(int i = 0; i < GRAINS; i++) {
        grains[i].x = hashf(5000u + uint32_t(i)) * SCREEN;
        grains[i].y = 80.0f + hashf(5100u + uint32_t(i)) * 40.0f;
        grains[i].vy = 0.0f;
    }
    for(int i = 0; i < MOTES; i++) motes[i] = {hashf(6000u + uint32_t(i)) * SCREEN, 60.0f + hashf(6100u + uint32_t(i)) * 60.0f};
    for(int i = 0; i < STARS; i++) {
        stars[i].x = uint8_t(hash(7000u + uint32_t(i)) % 200u);
        stars[i].y = uint8_t(2u + hash(7100u + uint32_t(i)) % 60u);
        stars[i].kind = uint8_t(hash(7200u + uint32_t(i)) % 7u);
        stars[i].phase = hashf(7300u + uint32_t(i)) * 6.28f;
    }
    for(int i = 0; i < DROPS; i++) drops[i] = {hashf(8000u + uint32_t(i)) * SCREEN, hashf(8100u + uint32_t(i)) * SCREEN};
}

void enter(Scene s) {
    (void)s;
    if(pressed_for != st.chapter) lay_trail();
    if(st.chapter < 5) { aurora = 0.0f; }
    if(!flag(F_RELIT)) beam = 0.0f;
    set_cam_instant();
}

float width(Scene s) {
    switch(s) {
    case S_DUNES: return DUNES_W;
    case S_PIER: return 430.0f;
    case S_BAR: return 170.0f;
    default: return 128.0f;
    }
}

bool outdoors(Scene s) { return s == S_DUNES || s == S_PIER; }

float floor_y(Scene s, float x) {
    switch(s) {
    case S_DUNES: return ground_y(x) + 1.0f;
    case S_PIER: return x < float(PIER_FROM) ? 105.0f : 98.0f;
    default: return float(ROOM_FLOOR) + 1.0f;
    }
}

float wind_at(float x) { return field.at(x) * wind_mult(); }

int screen_x(float x) { return sx_of(x, 1.0f); }

namespace {
float cam_target() {
    float w = width(st.scene);
    float target = st.x - 56.0f + float(st.facing) * 10.0f;
    if(target > w - SCREEN) target = w - SCREEN;
    if(target < 0) target = 0;
    return target;
}
}  // namespace

void set_cam_instant() { cam = cam_target(); }

void update(float dt) {
    t += dt;
    if(pressed_for != st.chapter) lay_trail();
    field.step(dt, cam - 80.0f, cam * P_FRONT + SCREEN + 80.0f);
    cam += (cam_target() - cam) * 0.05f;
    flicker = 0.92f + 0.08f * sinf(t * 23.0f) * sinf(t * 3.1f);
    if(beam > 0.0f) beam += dt;
    if(st.chapter >= 5 && aurora < 1.0f) aurora += dt / 300.0f;

    if(st.scene == S_DUNES) {
        for(auto &b : mid) step_blade(b, wind_at(b.x), t, dt);
        for(auto &b : back) step_blade(b, wind_at(b.x / P_BACK), t, dt);
        for(auto &b : front) step_blade(b, wind_at(b.x / P_FRONT), t, dt);
    }
    if(outdoors(st.scene)) ambience(dt);
}

void draw() {
    px = sx_of(st.x, 1.0f);
    int feet = int(floor_y(st.scene, st.x));
    py = feet;
    nlamps = lamps_of(st.scene, lamps);

    switch(st.scene) {
    case S_DUNES:
        draw_sky(raining());
        draw_aurora();
        draw_sea(wind_mult());
        draw_back_dunes();
        draw_ground();
        draw_mid_grass(0);
        draw_car();
        draw_lighthouse();
        draw_bike();
        draw_bunker();
        draw_halina();
        draw_barrel();
        if(show_player) draw_szary(px, feet);
        draw_mid_grass(1);
        draw_grains();
        draw_motes();
        draw_rain();
        draw_front_grass();
        break;
    case S_PIER:
        draw_sky(true);
        draw_far_lighthouse(380.0f);
        draw_sea(wind_mult());
        draw_pier();
        if(show_player) draw_szary(px, feet);
        draw_motes();
        draw_rain();
        break;
    case S_STATION:
        draw_station();
        if(show_player) draw_szary(px, feet);
        break;
    case S_LIGHTHOUSE:
        draw_lighthouse_inside();
        if(show_player) draw_szary(px, feet);
        break;
    case S_BAR:
        draw_bar();
        draw_rysiek();
        draw_counter();
        draw_kuba();
        if(show_player) draw_szary(px, feet);
        break;
    case S_OFFICE:
        draw_office();
        draw_jasinski();
        if(show_player) draw_szary(px, feet);
        break;
    case S_BUNKER:
        draw_bunker_inside();
        draw_bogdan();
        if(show_player) draw_szary(px, feet);
        draw_motes();
        break;
    default: break;
    }
}

}  // namespace world
}  // namespace sz
