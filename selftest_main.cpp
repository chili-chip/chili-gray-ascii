// Headless self-test and clip recorder (cmake -DGRAY_SELFTEST=ON).
//
// Stands in for the SDL/console firmware: a fake clock and an RGB565 frame
// buffer like the VGC Zero's. No window and no sound.
//
//   gray-ascii-selftest              run the checks, exit 1 on a failure
//   gray-ascii-selftest --clip DIR   play a scripted walk and write PPM frames
//   gray-ascii-selftest --bench      time render() on this machine

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "32blit.hpp"
#include "engine/api_private.hpp"
#include "engine/api_version.h"
#include "font.hpp"
#include "game.hpp"
#include "texts.hpp"
#include "wind.hpp"

using namespace blit;

void init();
void update(uint32_t time);
void render(uint32_t time);

namespace {

uint32_t clock_ms = 1000;
uint16_t framebuffer[128 * 128];
std::map<std::string, std::vector<char>> files;

uint32_t host_now() { return clock_ms; }
uint32_t host_random() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
void host_debug(const char *message) { fputs(message, stderr); }
uint32_t host_us() { return clock_ms * 1000u; }
uint32_t host_max_us() { return 0xffffffffu; }
const char *host_save_path() { return ""; }
bool host_storage() { return true; }
bool host_set_mode(ScreenMode, SurfaceTemplate &t) {
    t.data = reinterpret_cast<uint8_t *>(framebuffer);
    t.format = PixelFormat::RGB565;
    t.bounds = Size(128, 128);
    return true;
}
void *host_open(const std::string &, int) { return nullptr; }
int32_t host_read(void *, uint32_t, uint32_t, char *) { return 0; }
int32_t host_write(void *, uint32_t, uint32_t length, const char *) { return int32_t(length); }
int32_t host_close(void *) { return 0; }
uint32_t host_length(void *) { return 0; }
bool host_exists(const std::string &) { return false; }
bool host_dir_exists(const std::string &) { return true; }
bool host_mkdir(const std::string &) { return true; }
bool host_remove(const std::string &) { return false; }

AudioChannel host_channels[CHANNEL_COUNT];

APIConst make_api() {
    APIConst a{};
    a.channels = host_channels;
    a.version_major = BLIT_API_VERSION_MAJOR;
    a.version_minor = BLIT_API_VERSION_MINOR;
    a.now = host_now;
    a.random = host_random;
    a.debug = host_debug;
    a.open_file = host_open;
    a.read_file = host_read;
    a.write_file = host_write;
    a.close_file = host_close;
    a.get_file_length = host_length;
    a.file_exists = host_exists;
    a.directory_exists = host_dir_exists;
    a.create_directory = host_mkdir;
    a.remove_file = host_remove;
    a.get_save_path = host_save_path;
    a.is_storage_available = host_storage;
    a.get_us_timer = host_us;
    a.get_max_us_timer = host_max_us;
    a.set_screen_mode_format = host_set_mode;
    return a;
}

APIConst host_api_const = make_api();
APIData host_api_data;

}  // namespace

namespace blit {
const APIConst &api = host_api_const;
APIData &api_data = host_api_data;
}

namespace {

int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while(0)

// One 10 ms step with these buttons held; `press` = newly pressed.
void step(uint32_t held, uint32_t press = 0) {
    clock_ms += 10;
    buttons.state = held | press;
    buttons.pressed = press;
    buttons.released = 0;
    ::update(clock_ms);
    buttons.pressed = 0;
}

void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if(!f) return;
    fprintf(f, "P6 128 128 255\n");
    for(auto px : framebuffer) {
        uint8_t rgb[3] = {uint8_t((px & 31) << 3), uint8_t(((px >> 5) & 63) << 2), uint8_t((px >> 11) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

bool frame_is_grey() {
    for(auto px : framebuffer) {
        int a = (px & 31) << 3, g = ((px >> 5) & 63) << 2, b = (px >> 11) << 3;
        if(abs(a - b) > 8 || abs(a - g) > 8) return false;
    }
    return true;
}

template <size_t N>
void check_lines(const texts::Line (&lines)[N], const char *what) {
    for(size_t i = 0; i < N; i++)
        for(int l = 0; l < texts::LANGS; l++) {
            const char *t = lines[i].text[l];
            CHECK(font::has_all_glyphs(t), "%s line %zu lang %d has a character with no glyph: %s", what, i, l, t);
            // 3 lines of 30 in the box, or 4 lines of 31 as a thought
            int limit = lines[i].speaker[l] ? 3 * 30 : 4 * 31;
            CHECK(font::length(t) <= limit, "%s line %zu lang %d is too long (%d > %d)", what, i, l, font::length(t), limit);
            if(lines[i].speaker[l]) CHECK(font::has_all_glyphs(lines[i].speaker[l]), "%s speaker", what);
        }
}

void test_texts() {
    check_lines(texts::intro, "intro");
    check_lines(texts::trail_dark, "trail_dark");
    check_lines(texts::trail_lit, "trail_lit");
    check_lines(texts::halina, "halina");
    for(int l = 0; l < texts::LANGS; l++) {
        CHECK(font::length(texts::TITLE_HINT[l]) <= 32, "hint too long");
        CHECK(font::has_all_glyphs(texts::TALK[l]) && font::has_all_glyphs(texts::LOOK[l]), "labels");
    }
    CHECK(font::length("ŻÓŁĆ") == 4, "UTF-8 length");
    CHECK(font::glyph_index(0x142) == font::glyph_index(0x141), "ł maps to Ł");
    CHECK(font::glyph_index(0x20AC) < 0, "no glyph for the euro sign");
}

void test_wind() {
    wind::Field f;
    f.reset(0);
    wind::Blade free_b, pressed_b;
    free_b.height = pressed_b.height = 16;
    pressed_b.pressed = true;
    float min_w = 9, max_w = -9, sum_free = 0, sum_pressed = 0;
    int n = 0;
    for(int i = 0; i < 6000; i++) {  // a minute
        f.step(0.01f, -100, 400);
        float w = f.at(100);
        min_w = fminf(min_w, w);
        max_w = fmaxf(max_w, w);
        wind::step_blade(free_b, w, f.t, 0.01f);
        wind::step_blade(pressed_b, w, f.t, 0.01f);
        CHECK(std::isfinite(free_b.angle) && std::isfinite(pressed_b.angle), "blade angle is not a number");
        if(i > 500) { sum_free += free_b.angle; sum_pressed += pressed_b.angle; n++; }
    }
    CHECK(min_w > 0.0f, "wind should never blow backwards (%f)", double(min_w));
    CHECK(max_w > 0.8f, "a minute should bring at least one gust (max %f)", double(max_w));
    CHECK(sum_free / float(n) > 0.1f, "free grass leans with the wind");
    CHECK(sum_pressed / float(n) < -0.3f, "pressed grass leans against the wind");
    CHECK(wind::stem_char(0) == '|' && wind::stem_char(3) == '/' && wind::stem_char(-3) == '\\', "stem characters");
}

void test_game() {
    ::init();
    // intro thought is open; it types, then A closes it
    CHECK(dbg::dialog_open(), "intro opens");
    for(int i = 0; i < 400; i++) step(0);
    CHECK(!dbg::dialog_typing(), "intro finished typing");
    step(0, Button::A);
    CHECK(!dbg::dialog_open(), "A closes the intro");

    // walking
    float x0 = dbg::player_x();
    for(int i = 0; i < 100; i++) step(Button::DPAD_RIGHT);
    CHECK(dbg::player_x() > x0 + 20, "walks right");

    // the trail: dark first, then with the torch
    while(dbg::player_x() < 292) step(Button::DPAD_RIGHT);
    for(int i = 0; i < 60; i++) step(0);
    step(0, Button::A);
    CHECK(dbg::dialog_open(), "look at the trail");
    for(int i = 0; i < 6 && dbg::dialog_open(); i++) { step(0, Button::A); step(0, Button::A); }
    CHECK(!dbg::dialog_open(), "trail thought closes");
    step(0, Button::Y);
    CHECK(dbg::torch_on(), "Y lights the torch");

    // a frame with the torch is grey only
    ::render(clock_ms);
    CHECK(frame_is_grey(), "the picture has only greys");

    // language
    step(0, Button::X);
    CHECK(dbg::lang() == texts::EN, "X switches to English");
    step(0, Button::X);

    // Halina
    while(dbg::player_x() < 470) step(Button::DPAD_RIGHT);
    for(int i = 0; i < 60; i++) step(0);
    step(0, Button::A);
    CHECK(dbg::dialog_open(), "talk to Halina");
    int presses = 0;
    while(dbg::dialog_open() && presses < 20) { step(0, Button::A); presses++; }
    CHECK(!dbg::dialog_open(), "the talk ends");
    // cannot walk while talking
    step(0, Button::A);
    float xh = dbg::player_x();
    for(int i = 0; i < 50; i++) step(Button::DPAD_RIGHT);
    CHECK(fabsf(dbg::player_x() - xh) < 0.5f, "no walking while the box is open");

    // a long stall (system menu) does not run hundreds of steps
    clock_ms += 60000;
    ::update(clock_ms);
}

// Scripted walk for the website GIF and the plan: intro, torch, trail, Halina.
void clip(const char *dir, int every) {
    ::init();
    int frame = 0, n = 0;
    auto shoot = [&]() {
        if(n++ % every) return;
        ::render(clock_ms);
        char path[512];
        snprintf(path, sizeof(path), "%s/f%05d.ppm", dir, frame++);
        write_ppm(path);
    };
    auto wait = [&](int steps, uint32_t held = 0) { for(int i = 0; i < steps; i++) { step(held); shoot(); } };
    auto press = [&](uint32_t b) { step(0, b); shoot(); };

    wait(330);              // intro types
    press(Button::A);
    wait(60);
    press(Button::Y);       // torch
    wait(40);
    while(dbg::player_x() < 200) { step(Button::DPAD_RIGHT); shoot(); }
    wait(90);               // stand in the wind
    while(dbg::player_x() < 292) { step(Button::DPAD_RIGHT); shoot(); }
    wait(80);
    press(Button::A);       // look at the grass
    wait(260);
    press(Button::A);
    wait(220);
    press(Button::A);
    wait(40);
    while(dbg::player_x() < 470) { step(Button::DPAD_RIGHT); shoot(); }
    wait(80);
    press(Button::A);       // talk
    for(int k = 0; k < 4; k++) { wait(k == 2 ? 300 : 230); press(Button::A); }
    wait(120);
    printf("%d frames\n", frame);
}

void bench() {
    ::init();
    step(0, Button::A);
    step(0, Button::Y);
    double total_us = 0, worst_us = 0;
    int frames = 0;
    for(int i = 0; i < 4000; i++) {
        step(i % 400 < 300 ? uint32_t(Button::DPAD_RIGHT) : 0u);
        if(i % 2) continue;
        auto t0 = std::chrono::steady_clock::now();
        ::render(clock_ms);
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        total_us += us;
        if(us > worst_us) worst_us = us;
        frames++;
    }
    printf("render on this machine: %.0f us average, %.0f us worst, %d frames\n", total_us / frames, worst_us, frames);
}

}  // namespace

int main(int argc, char **argv) {
    if(argc >= 3 && !strcmp(argv[1], "--clip")) {
        clip(argv[2], argc >= 4 ? atoi(argv[3]) : 4);
        return 0;
    }
    if(argc >= 2 && !strcmp(argv[1], "--bench")) {
        bench();
        return 0;
    }
    test_texts();
    test_wind();
    test_game();
    if(failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
