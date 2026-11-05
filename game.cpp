// Szary: a grey ASCII noir for the ChiliChip VGC Zero.
//
// This is the first technical test (M0): one night scene on the dunes with
// grass in the wind, the detective with a torch, one witness and one clue.
// See README.md for the plan.

#include "game.hpp"

#include <cmath>
#include <cstring>

#include "font.hpp"
#include "texts.hpp"
#include "wind.hpp"

using namespace blit;

namespace {

// ---------------------------------------------------------------- world

constexpr int SCREEN = 128;
constexpr float WORLD_W = 720.0f;
constexpr float DT = 0.01f;  // update() runs at 100 Hz

constexpr float HALINA_X = 494.0f;
constexpr float BARREL_X = 514.0f;
constexpr float TALK_RANGE = 30.0f;
constexpr float TRAIL_FROM = 250.0f, TRAIL_TO = 330.0f, TRAIL_LOOK = 292.0f;

// parallax factors
constexpr float P_SKY = 0.04f, P_SEA = 0.22f, P_BACK = 0.5f, P_FRONT = 1.35f;

inline float ground_y(float x) { return 108.0f + 4.0f * sinf(x * 0.013f) + 2.0f * sinf(x * 0.037f + 2.0f); }
inline float back_dune_y(float x) { return 93.0f + 5.0f * sinf(x * 0.02f) + 3.0f * sinf(x * 0.051f + 1.0f); }
constexpr float HORIZON = 72.0f;

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
Mote motes[MOTES];  // dust in the torch beam, screen space

struct Star { uint8_t x, y, kind; float phase; };
constexpr int STARS = 46;
Star stars[STARS];

float player_x = 40.0f, player_vx = 0.0f, walk_dist = 0.0f;
int facing = 1;
bool torch = false;
float torch_flicker = 1.0f;
float cam = 0.0f;
float time_s = 0.0f;
int lang = texts::PL;
float lang_toast = 0.0f;
bool show_fps = false;
uint32_t fps_frames = 0, fps_last = 0, fps_value = 0, last_render_us = 0;
uint32_t last_update_ms = 0;

// dialog
const texts::Line *dlg_lines = nullptr;
int dlg_count = 0, dlg_index = 0;
float dlg_shown = 0.0f;
bool saw_trail = false, talked = false;

// ---------------------------------------------------------------- dialog

constexpr int BOX_COLS = 30, BOX_LINES = 3;

struct Wrapped {
    std::string_view lines[8];
    int count = 0;
};

// Word wrap a UTF-8 line to `cols` characters.
Wrapped wrap(std::string_view s, int cols) {
    Wrapped w;
    size_t line_start = 0, last_space = std::string_view::npos;
    int chars = 0, chars_at_space = 0;
    for(size_t i = 0; i < s.size();) {
        size_t at = i;
        uint32_t cp = font::next_cp(s, i);
        if(cp == ' ') { last_space = at; chars_at_space = chars; }
        chars++;
        if(chars > cols && w.count < 7) {
            if(last_space != std::string_view::npos && last_space > line_start) {
                w.lines[w.count++] = s.substr(line_start, last_space - line_start);
                line_start = last_space + 1;
                chars -= chars_at_space + 1;
            } else {
                w.lines[w.count++] = s.substr(line_start, at - line_start);
                line_start = at;
                chars = 1;
            }
            last_space = std::string_view::npos;
        }
    }
    if(line_start < s.size()) w.lines[w.count++] = s.substr(line_start);
    return w;
}

bool dialog_open() { return dlg_lines != nullptr; }
const texts::Line &dlg_line() { return dlg_lines[dlg_index]; }
int dlg_len() { return font::length(dlg_line().text[lang]); }

void open_dialog(const texts::Line *lines, int count) {
    dlg_lines = lines;
    dlg_count = count;
    dlg_index = 0;
    dlg_shown = 0.0f;
}

void dialog_press() {
    if(dlg_shown < float(dlg_len())) { dlg_shown = float(dlg_len()); return; }
    dlg_index++;
    dlg_shown = 0.0f;
    if(dlg_index >= dlg_count) dlg_lines = nullptr;
}

// ---------------------------------------------------------------- audio

void start_wind_sound() {
    auto &c = channels[0];
    c.waveforms = Waveform::NOISE;
    c.frequency = 1800;
    c.volume = 0;
    c.attack_ms = 400;
    c.decay_ms = 1;
    c.sustain = 0xffff;
    c.release_ms = 400;
    c.trigger_attack();
}

void click_sound() {
    auto &c = channels[1];
    c.waveforms = Waveform::SQUARE;
    c.frequency = 1400;
    c.volume = 0x2400;
    c.attack_ms = 1;
    c.decay_ms = 25;
    c.sustain = 0;
    c.release_ms = 1;
    c.trigger_attack();
}

void blip_sound() {
    auto &c = channels[2];
    c.waveforms = Waveform::TRIANGLE;
    c.frequency = 520 + (hash(uint32_t(time_s * 1000.0f)) & 120);
    c.volume = 0x1400;
    c.attack_ms = 1;
    c.decay_ms = 18;
    c.sustain = 0;
    c.release_ms = 1;
    c.trigger_attack();
}

// ---------------------------------------------------------------- setup

// Grass grows in tufts: a few blades from one spot, fanned out, with bare
// sand between tufts.
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
            float f = in_tuft > 1 ? float(j) / float(in_tuft - 1) - 0.5f : 0.0f;  // -0.5 .. 0.5
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

}  // namespace

void init() {
    set_screen_mode(ScreenMode::hires);

    field.reset(-60.0f);
    place_blades(mid, MID_BLADES, 0.0f, WORLD_W, 9.0f, 19.0f, 1.0f, 100);
    place_blades(back, BACK_BLADES, 0.0f, WORLD_W * P_BACK + SCREEN, 4.0f, 8.0f, 1.2f, 900);
    place_blades(front, FRONT_BLADES, 0.0f, WORLD_W * P_FRONT + SCREEN, 16.0f, 30.0f, 0.8f, 1700);
    for(auto &b : mid)
        if(b.x > TRAIL_FROM && b.x < TRAIL_TO) b.pressed = true;

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

    start_wind_sound();
    open_dialog(texts::intro, 1);
}

// ---------------------------------------------------------------- update

namespace {

void tick() {
    time_s += DT;
    field.step(DT, cam - 80.0f, cam * P_FRONT + SCREEN + 80.0f);

    bool busy = dialog_open();

    // input
    if(buttons.pressed & Button::X) { lang = 1 - lang; lang_toast = 1.4f; }
    if(buttons.pressed & Button::B) show_fps = !show_fps;
    if(buttons.pressed & Button::Y) { torch = !torch; click_sound(); }

    float want = 0.0f;
    if(!busy) {
        if(buttons & Button::DPAD_LEFT) want = -30.0f;
        if(buttons & Button::DPAD_RIGHT) want = 30.0f;
    }
    player_vx += (want - player_vx) * (want == 0.0f ? 0.18f : 0.08f);
    if(fabsf(player_vx) < 0.3f && want == 0.0f) player_vx = 0.0f;
    if(want > 0) facing = 1;
    if(want < 0) facing = -1;
    player_x += player_vx * DT;
    if(player_x < 12.0f) { player_x = 12.0f; player_vx = 0; }
    if(player_x > WORLD_W - 16.0f) { player_x = WORLD_W - 16.0f; player_vx = 0; }
    walk_dist += fabsf(player_vx) * DT;

    if(buttons.pressed & Button::A) {
        if(busy) dialog_press();
        else if(fabsf(player_x - HALINA_X) < TALK_RANGE) { open_dialog(texts::halina, 4); talked = true; }
        else if(fabsf(player_x - TRAIL_LOOK) < 22.0f) {
            if(torch) { open_dialog(texts::trail_lit, 2); saw_trail = true; }
            else open_dialog(texts::trail_dark, 1);
        }
    }

    // typewriter
    if(dialog_open() && dlg_shown < float(dlg_len())) {
        float before = dlg_shown;
        dlg_shown += 40.0f * DT;
        if(int(dlg_shown) / 3 != int(before) / 3) blip_sound();
    }

    // camera follows with a little look-ahead
    float target = player_x - 56.0f + float(facing) * 10.0f;
    if(target < 0) target = 0;
    if(target > WORLD_W - SCREEN) target = WORLD_W - SCREEN;
    cam += (target - cam) * 0.05f;

    // grass
    for(auto &b : mid) step_blade(b, field.at(b.x), time_s, DT);
    for(auto &b : back) step_blade(b, field.at(b.x / P_BACK), time_s, DT);
    for(auto &b : front) step_blade(b, field.at(b.x / P_FRONT), time_s, DT);

    // sand grains (screen space) blown by the wind
    for(int i = 0; i < GRAINS; i++) {
        auto &g = grains[i];
        float w = field.at(cam + g.x);
        g.x += (w * 95.0f - 10.0f) * DT;
        g.vy += (hashf(uint32_t(time_s * 50.0f) + uint32_t(i) * 31u) - 0.5f) * 60.0f * DT;
        g.vy *= 0.97f;
        g.y += g.vy * DT;
        float gy = ground_y(cam + g.x);
        if(g.y > gy) { g.y = gy; g.vy = -fabsf(g.vy); }
        if(g.x > SCREEN + 4 || g.x < -8 || g.y < gy - 40.0f) {
            g.x = -4.0f - hashf(uint32_t(time_s * 100.0f) + uint32_t(i)) * 30.0f;
            g.y = ground_y(cam) - hashf(uint32_t(i) * 17u + uint32_t(time_s)) * 24.0f;
            g.vy = 0;
        }
    }
    // dust in the beam
    for(int i = 0; i < MOTES; i++) {
        auto &m = motes[i];
        float w = field.at(cam + m.x);
        m.x += (w * 40.0f) * DT - player_vx * DT;
        m.y += sinf(time_s * 1.3f + float(i)) * 6.0f * DT;
        if(m.x > SCREEN + 2) m.x -= SCREEN + 8;
        if(m.x < -6) m.x += SCREEN + 8;
    }

    torch_flicker = 0.92f + 0.08f * sinf(time_s * 23.0f) * sinf(time_s * 3.1f);
    if(lang_toast > 0) lang_toast -= DT;

    // wind noise follows the wind at the player
    float wp = field.at(player_x);
    channels[0].volume = uint16_t(0x500 + int(fminf(wp, 1.8f) * 0x1600));
}

}  // namespace

void update(uint32_t time) {
    // fixed steps; after a long stall (system menu) do not race to catch up
    if(last_update_ms == 0 || time - last_update_ms > 250) last_update_ms = time - 10;
    while(time - last_update_ms >= 10) {
        tick();
        buttons.pressed = 0;  // only the first step of a frame sees a press
        last_update_ms += 10;
    }
}

// ---------------------------------------------------------------- render

namespace {

int sx_of(float world_x, float parallax) { return int(floorf(world_x - cam * parallax)); }

// Light on the screen at (x, y) from the torch and the fire, 0..4.
int light_at(int x, int y, int px, int py) {
    float v = 0.0f;
    if(torch) {
        float ox = float(px + facing * 7), oy = float(py - 15);
        float dx = (float(x) - ox) * float(facing);
        if(dx > 0.0f) {
            float dy = float(y) - oy - dx * 0.22f;  // aimed a bit down
            float spread = 3.0f + dx * 0.4f;
            float ady = fabsf(dy);
            if(ady < spread && dx < 92.0f) v += (1.0f - dx / 92.0f) * (1.0f - 0.5f * ady / spread) * 5.4f * torch_flicker;
        }
    }
    float fx = float(sx_of(BARREL_X, 1.0f)) + 4.0f, fy = ground_y(BARREL_X) - 14.0f;
    float d = sqrtf((float(x) - fx) * (float(x) - fx) + (float(y) - fy) * (float(y) - fy) * 1.6f);
    if(d < 38.0f) v += (1.0f - d / 38.0f) * 3.0f * (0.85f + 0.15f * sinf(time_s * 17.0f + float(x)));
    int iv = int(v + 0.5f);
    return iv > 4 ? 4 : iv;
}

void draw_sky() {
    for(auto &s : stars) {
        int x = int(s.x) - int(cam * P_SKY);
        if(x < -2 || x >= SCREEN) continue;
        float tw = 0.5f + 0.5f * sinf(time_s * (0.7f + float(s.kind) * 0.3f) + s.phase);
        int shade = 1 + int(tw * (s.kind < 2 ? 3.0f : 1.6f));
        font::micro(x, s.y, s.kind == 0 ? '+' : (s.kind == 1 ? ':' : '.'), shade);
    }
    // moon: a disc of single dots on a 2 px grid, dimmer "seas" and rim
    int mx = 24 - int(cam * P_SKY), my = 18;
    for(int y = -9; y <= 9; y += 2)
        for(int x = -9; x <= 9; x += 2) {
            float d = sqrtf(float(x * x + y * y));
            if(d > 9.2f) continue;
            bool sea = (x > -6 && x < 0 && y > -6 && y < 0) || (x > 1 && x < 6 && y > 1 && y < 6);
            font::micro(mx + x, my + y - 2, '.', sea ? 4 : (d > 7.5f ? 5 : 7));
        }
    // halo
    for(int a = 0; a < 16; a++) {
        float ang = float(a) * 0.3927f + time_s * 0.02f;
        font::micro(mx + int(cosf(ang) * 13.0f), my + int(sinf(ang) * 12.0f), '.', 2 + (a % 3 == 0));
    }
    // clouds drifting with the wind
    for(int k = 0; k < 4; k++) {
        float cx = fmodf(float(k) * 61.0f + time_s * (3.0f + float(k)) - cam * 0.08f, 220.0f) - 50.0f;
        int cy = 8 + k * 9;
        int len = 6 + int(hash(uint32_t(k) * 7u) % 6u);
        for(int i = 0; i < len; i++) {
            char c = (i == 0 || i == len - 1) ? '.' : ((i + k) % 3 ? '-' : '~');
            font::micro(int(cx) + i * 3, cy + ((i * 5 + k) % 3 == 0 ? 1 : 0), c, k == 1 ? 2 : 1);
        }
    }
    // a bird now and then
    float bx = fmodf(time_s * 14.0f, 260.0f) - 40.0f;
    font::ch(int(bx), 30 + int(3.0f * sinf(time_s * 1.7f)), 'v', 2);
}

void draw_sea() {
    // the lighthouse on its headland, dark
    int lx = sx_of(196.0f, P_SEA), base = int(HORIZON) + 2;
    if(lx > -20 && lx < SCREEN + 8) {
        for(int i = 0; i < 9; i++) font::micro(lx - 10 + i * 3, base - (i > 2 && i < 7 ? 2 : 0), i > 2 && i < 7 ? '_' : '.', 2);
        static const char *tower[] = {" ^ ", "[.]", "|#|", "|:|", "|#|", "|:|", "/#\\"};
        for(int r = 0; r < 7; r++)
            for(int c = 0; c < 3; c++) {
                char ch = tower[r][c];
                if(ch == ' ') continue;
                font::ch(lx - 6 + c * 4, base - 34 + r * 5, ch, ch == '.' ? 2 : 3);
            }
    }
    // waves: short marks scrolling, moon light on the water
    int moon_x = 25 - int(cam * P_SKY);
    for(int row = 0; row < 6; row++) {
        int y = int(HORIZON) + row * 2 + (row * row) / 3;
        float speed = 4.0f + float(row) * 2.0f;
        float off = time_s * speed - cam * (P_SEA + float(row) * 0.04f);
        int step = 6 + row;
        int start = -int(fmodf(off, float(step))) - step;
        for(int x = start; x < SCREEN; x += step) {
            int cell = int(floorf((float(x) + off) / float(step)));
            uint32_t h = hash(uint32_t(cell * 7 + row * 1013));
            if(h % 3 == 0) continue;
            int shade = 2 + (row > 3 ? 1 : 0);
            int dm = abs(x - moon_x);
            if(dm < 10 + row * 2) shade += 2 + ((h >> 4) + uint32_t(time_s * 6.0f)) % 2;
            font::micro(x, y, (h & 16) ? '~' : '-', shade);
        }
    }
}

void draw_blade_micro(const wind::Blade &b, int rx, int ry, int shade) {
    // back grass: small marks, 3 px per piece
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

void draw_back_dunes() {
    // dune body: dots in a grid that scrolls with the dune
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

void draw_ground(int px, int py) {
    int start = -int(fmodf(cam, 4.0f)) - 4;
    for(int x = start; x < SCREEN; x += 4) {
        float wx = float(x) + cam;
        int top = int(ground_y(wx));
        // black out the back dune behind the front one
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(x, top, 4, SCREEN - top));
        int col = int(floorf(wx / 4.0f));
        for(int y = top; y < SCREEN; y += 3) {
            uint32_t h = hash(uint32_t(col * 977 + (y - top) * 31));
            int depth = (y - top) / 3;
            int l = light_at(x + 1, y + 1, px, py);
            // the surface line, then sparse sand that the torch brings out
            if(depth > 0 && h % (l > 0 ? 2u : 4u)) continue;
            char c = depth == 0 ? '_' : ((h >> 8) % 4 == 0 ? ':' : '.');
            int shade = (depth == 0 ? 2 : 1) + l;
            font::micro(x + int((h >> 20) & 1), y, c, shade);
        }
    }
}

// Stem of a grass blade with text-font characters, one per 4 px of height.
void draw_blade(const wind::Blade &b, int rx, int ry, int shade, int px, int py, bool lightable) {
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
        if(lightable) s += light_at(x + 1, y + 3, px, py);
        font::ch(x, y, c, s);
        prev = off;
    }
}

void draw_mid_grass(int px, int py, int parity) {
    for(int i = 0; i < MID_BLADES; i++) {
        if((i & 1) != parity) continue;
        auto &b = mid[i];
        int rx = sx_of(b.x, 1.0f);
        if(rx < -24 || rx > SCREEN + 24) continue;
        int shade = parity ? 2 : 1;  // flat grass catches less moonlight
        draw_blade(b, rx, int(ground_y(b.x)) + 2, shade, px, py, true);
    }
}

void draw_front_grass(int px, int py) {
    for(auto &b : front) {
        int rx = sx_of(b.x, P_FRONT);
        if(rx < -30 || rx > SCREEN + 30) continue;
        // clumps: only some stretches have tall grass in front
        if(sinf(b.x * 0.021f) < 0.15f) continue;
        draw_blade(b, rx, SCREEN + 6, 1, px, py, true);
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
        font::ch(x + dx - 4, y + p.dy, c, base_shade + p.shade);
    }
}

void draw_szary(int sx, int feet) {
    int top = feet - 33;
    const char SOLID = 127;
    // Noir silhouette: a dark solid coat and hat, edges picked out in light.
    static const Part body[] = {
        {3, 0, SOLID, -3}, {5, 0, SOLID, -3},                 // hat crown
        {0, 3, '-', 0}, {3, 3, '-', 0}, {6, 3, '-', 0},      // brim
        {4, 6, 'o', 1}, {7, 6, '.', 0},                      // face, nose
        {2, 12, SOLID, -3}, {5, 12, SOLID, -3},              // coat
        {2, 16, SOLID, -3}, {5, 16, SOLID, -3},
        {2, 20, SOLID, -3}, {5, 20, SOLID, -3},
        {0, 11, '/', 0}, {7, 11, '\\', 0},                   // shoulders
        {0, 15, '|', -1}, {0, 19, '|', -1},                  // back edge of the coat
        {7, 15, '-', 1},                                     // arm with the torch
    };
    draw_parts(body, int(sizeof(body) / sizeof(body[0])), sx, top, facing, 5);
    if(torch) {
        static const Part lamp[] = {{10, 14, '=', 0}};
        draw_parts(lamp, 1, sx, top, facing, 7);
    }

    // coat tail flaps behind him in the wind
    float w = field.at(player_x);
    int lift = w > 1.0f ? 2 : (w > 0.6f ? 1 : 0);
    char tail = lift == 2 ? '_' : (lift == 1 ? (facing > 0 ? '\\' : '/') : '|');
    int tx = facing > 0 ? sx - 4 - lift * 2 : sx + 4 + lift * 2;
    font::ch(tx, top + 22 - lift * 2, tail, 3);

    // legs
    static const char legs[4][2] = {{'/', '\\'}, {'|', '\\'}, {'|', '|'}, {'/', '|'}};
    int frame = player_vx == 0.0f ? 2 : int(walk_dist / 5.0f) % 4;
    char l = legs[frame][0], r = legs[frame][1];
    if(facing < 0) {
        auto flip = [](char c) { return c == '/' ? '\\' : (c == '\\' ? '/' : c); };
        char t = l; l = flip(r); r = flip(t);
    }
    font::ch(sx - 3, feet - 7, l, 4);
    font::ch(sx + 1, feet - 7, r, 4);
}

void draw_halina_and_fire(int px, int py) {
    int hx = sx_of(HALINA_X, 1.0f);
    int feet = int(ground_y(HALINA_X)) + 1;
    if(hx > -20 && hx < SCREEN + 20) {
        int top = feet - 24;
        int lit = light_at(hx, top + 10, px, py);
        const char SOLID = 127;
        static const Part body[] = {
            {3, 0, '^', 0},                                  // knotted scarf
            {1, 3, '(', 0}, {4, 4, 'o', 1}, {7, 3, ')', 0},  // scarf round the face
            {3, 9, SOLID, -2}, {5, 9, SOLID, -2}, {1, 9, '/', 0}, {7, 9, '\\', 0},
            {3, 13, SOLID, -2}, {5, 13, SOLID, -2},
            {0, 17, '/', 0}, {3, 17, SOLID, -2}, {5, 17, SOLID, -2}, {8, 17, '\\', 0},  // skirt
        };
        // she faces the fire (right)
        draw_parts(body, int(sizeof(body) / sizeof(body[0])), hx, top, 1, 2 + lit);
        // a little sway as she warms her hands
        int sway = sinf(time_s * 1.5f) > 0.6f ? 1 : 0;
        font::ch(hx + 4 + sway, top + 10, '-', 2 + lit);
    }
    int bx = sx_of(BARREL_X, 1.0f);
    int bfeet = int(ground_y(BARREL_X)) + 1;
    if(bx > -20 && bx < SCREEN + 20) {
        static const char *barrel[] = {"[=]", "[#]"};
        for(int r = 0; r < 2; r++)
            for(int c = 0; c < 3; c++) font::ch(bx - 2 + c * 3, bfeet - 12 + r * 5, barrel[r][c], 3);
        // flames lean with the wind
        float w = field.at(BARREL_X);
        for(int i = 0; i < 5; i++) {
            float fl = sinf(time_s * (9.0f + float(i) * 3.7f) + float(i) * 1.9f);
            int h = 3 + int(5.0f * (0.5f + 0.5f * fl)) - (i == 0 || i == 4 ? 2 : 0);
            int fx = bx - 2 + i * 2 + int(w * 1.5f * float(1 + (h > 4)));
            font::ch(fx, bfeet - 15 - h, i % 2 ? '^' : (fl > 0 ? '*' : '\''), 5 + (fl > -0.2f ? 2 : 1));
            if(h > 5) font::ch(fx + int(w * 2.0f), bfeet - 19 - h, '\'', 4);
        }
        // sparks blown downwind
        for(int i = 0; i < 6; i++) {
            float life = fmodf(time_s * 0.9f + float(i) * 0.37f, 1.6f);
            int x = bx + 2 + int(life * (12.0f + w * 30.0f) + 3.0f * sinf(time_s * 3.0f + float(i)));
            int y = bfeet - 18 - int(life * 22.0f);
            font::micro(x, y, '.', life < 0.9f ? 6 : 3);
        }
    }
}

void draw_grains(int px, int py) {
    for(auto &g : grains) {
        float w = field.at(cam + g.x);
        int x = int(g.x), y = int(g.y) - 2;
        int s = 2 + light_at(x, y, px, py);
        font::micro(x, y, w > 0.8f ? '-' : '.', s);
    }
}

void draw_motes(int px, int py) {
    if(!torch) return;
    for(auto &m : motes) {
        int x = int(m.x), y = int(m.y);
        int l = light_at(x, y, px, py);
        if(l >= 2) font::micro(x, y, '.', l + 1);
    }
}

void draw_prompt(float world_x, const char *label) {
    int w = font::length(label) * font::CELL_W;
    int x = sx_of(world_x, 1.0f) - w / 2;
    if(x < 1) x = 1;
    if(x + w > SCREEN - 1) x = SCREEN - 1 - w;
    int y = int(ground_y(world_x)) - 40;
    int shade = 4 + int(1.5f + 1.5f * sinf(time_s * 4.0f));
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(x - 1, y, w + 1, font::CELL_H));
    font::text(x, y, label, shade);
}

void draw_dialog() {
    if(!dialog_open()) return;
    const texts::Line &line = dlg_line();
    std::string_view text = line.text[lang];
    int shown = int(dlg_shown);
    bool thought = line.speaker[lang] == nullptr;
    Wrapped w = wrap(text, thought ? 31 : BOX_COLS);

    if(thought) {
        // Szary's thoughts: grey words across the top of the night, no box
        int h = w.count * font::CELL_H + 3;
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(0, 0, SCREEN, h));
        int left = shown;
        for(int i = 0; i < w.count && left > 0; i++) {
            int n = font::text(2, 1 + i * font::CELL_H, w.lines[i], 5, left);
            left -= n + 1;  // the space we broke at
        }
        if(shown >= font::length(text) && fmodf(time_s, 1.0f) < 0.6f) font::ch(SCREEN - 6, h - 6, '>', 4);
        return;
    }

    // a box across the top (the sky is empty, the people stand below)
    int y0 = 0;
    int box_h = BOX_LINES * font::CELL_H + 4;
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(0, 0, SCREEN, box_h + 4));
    for(int x = 0; x < SCREEN; x += 4) font::ch(x, box_h - 2, '-', 3);
    font::ch(0, box_h - 2, '+', 4);
    font::ch(SCREEN - 4, box_h - 2, '+', 4);
    // speaker name in the bottom border
    const char *name = line.speaker[lang];
    int nl = font::length(name);
    screen.rectangle(Rect(5, box_h - 1, (nl + 2) * 4, 6));
    font::ch(6, box_h - 2, '[', 4);
    font::text(10, box_h - 2, name, 7);
    font::ch(10 + nl * 4, box_h - 2, ']', 4);

    // the lines; if the text is long, show the last three as it types
    int left = shown, first = 0, total_lines = w.count;
    int used = 0, typed_lines = 0;
    for(int i = 0; i < total_lines; i++) {
        used += font::length(w.lines[i]) + 1;
        typed_lines = i + 1;
        if(used > shown) break;
    }
    if(typed_lines > BOX_LINES) first = typed_lines - BOX_LINES;
    for(int i = 0; i < first; i++) left -= font::length(w.lines[i]) + 1;
    for(int i = first; i < total_lines && i < first + BOX_LINES && left > 0; i++) {
        int n = font::text(3, y0 + 1 + (i - first) * font::CELL_H, w.lines[i], 6, left);
        left -= n + 1;
    }
    if(shown >= font::length(text) && fmodf(time_s, 1.0f) < 0.6f) font::ch(SCREEN - 8, box_h - 2, '>', 6);
}

void draw_overlay() {
    if(lang_toast > 0.0f) {
        const char *name = texts::LANG_NAME[lang];
        int w = font::length(name) * 4;
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(SCREEN - w - 4, 1, w + 3, 8));
        font::text(SCREEN - w - 2, 1, name, 6);
    }
    if(show_fps) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu FPS %lu.%luMS", (unsigned long)fps_value, (unsigned long)(last_render_us / 1000),
                 (unsigned long)(last_render_us / 100 % 10));
        int w = font::length(buf) * 4;
        screen.pen = Pen(0, 0, 0);
        screen.rectangle(Rect(0, 0, w + 2, 8));
        font::text(1, 1, buf, 7);
    }
}

}  // namespace

void render(uint32_t time) {
    uint32_t start = now_us();

    screen.pen = Pen(0, 0, 0);
    screen.clear();

    int px = sx_of(player_x, 1.0f);
    int feet = int(ground_y(player_x)) + 1;
    int py = feet;

    draw_sky();
    draw_sea();
    draw_back_dunes();
    draw_ground(px, py);
    draw_mid_grass(px, py, 0);
    draw_halina_and_fire(px, py);
    draw_szary(px, feet);
    draw_mid_grass(px, py, 1);
    draw_grains(px, py);
    draw_motes(px, py);
    draw_front_grass(px, py);

    if(!dialog_open()) {
        if(fabsf(player_x - HALINA_X) < TALK_RANGE) draw_prompt(HALINA_X, texts::TALK[lang]);
        else if(fabsf(player_x - TRAIL_LOOK) < 22.0f) draw_prompt(TRAIL_LOOK, texts::LOOK[lang]);
        else if(time_s < 12.0f && !torch) {
            const char *hint = texts::TITLE_HINT[lang];
            font::text((SCREEN - font::length(hint) * 4) / 2, 2, hint, 3);
        }
    }
    draw_dialog();

    last_render_us = us_diff(start, now_us());
    fps_frames++;
    if(time - fps_last >= 1000) {
        fps_value = fps_frames;
        fps_frames = 0;
        fps_last = time;
    }
    draw_overlay();
}

// ---------------------------------------------------------------- debug view

namespace dbg {
float player_x() { return ::player_x; }
bool dialog_open() { return ::dialog_open(); }
bool dialog_typing() { return ::dialog_open() && dlg_shown < float(dlg_len()); }
bool torch_on() { return torch; }
int lang() { return ::lang; }
float wind_at_player() { return field.at(::player_x); }
uint32_t render_us() { return last_render_us; }
}
