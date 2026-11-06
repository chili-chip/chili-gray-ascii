// Headless self-test and clip recorder (cmake -DGRAY_SELFTEST=ON).
//
// Stands in for the SDL/console firmware: a fake clock and an RGB565 frame
// buffer like the VGC Zero's. No window and no sound.
//
//   gray-ascii-selftest              run the checks and play the whole game to
//                                    all four endings; exit 1 on a failure
//   gray-ascii-selftest --clip DIR   the same play, writing PPM frames of
//                                    chosen moments (for GIFs)
//   gray-ascii-selftest --bench      time render() on this machine

#include <algorithm>
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
#include "szary.hpp"
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
// Files live in memory, so save slots work like on the console.
struct Handle { std::string name; };
void *host_open(const std::string &name, int mode) {
    if(mode & OpenMode::write) files[name].clear();
    else if(!files.count(name)) return nullptr;
    return new Handle{name};
}
int32_t host_read(void *h, uint32_t offset, uint32_t length, char *out) {
    auto &f = files[static_cast<Handle *>(h)->name];
    if(offset >= f.size()) return 0;
    uint32_t n = std::min<uint32_t>(length, uint32_t(f.size()) - offset);
    memcpy(out, f.data() + offset, n);
    return int32_t(n);
}
int32_t host_write(void *h, uint32_t offset, uint32_t length, const char *data) {
    auto &f = files[static_cast<Handle *>(h)->name];
    if(f.size() < offset + length) f.resize(offset + length);
    memcpy(f.data() + offset, data, length);
    return int32_t(length);
}
int32_t host_close(void *h) { delete static_cast<Handle *>(h); return 0; }
uint32_t host_length(void *h) { return uint32_t(files[static_cast<Handle *>(h)->name].size()); }
bool host_exists(const std::string &name) { return files.count(name) > 0; }
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

using namespace sz;

namespace {

int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while(0)

// ---------------------------------------------------------------- frames

const char *clip_dir = nullptr;
const char *shots_dir = nullptr;
bool recording = false;
int clip_frame = 0, clip_every = 4, clip_n = 0;

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

int grey_frames = 0, frames_checked = 0;

// One 10 ms step with these buttons held; `press` = newly pressed.
void step(uint32_t held, uint32_t press = 0) {
    clock_ms += 10;
    buttons.state = held | press;
    buttons.pressed = press;
    buttons.released = 0;
    ::update(clock_ms);
    buttons.pressed = 0;
    // render now and then: every scene must draw without trouble and grey
    static int n = 0;
    bool shoot = recording && clip_dir && (clip_n++ % clip_every) == 0;
    if(shoot || (++n % 97) == 0) {
        ::render(clock_ms);
        frames_checked++;
        grey_frames += frame_is_grey();
        if(shoot) {
            char path[512];
            snprintf(path, sizeof(path), "%s/f%05d.ppm", clip_dir, clip_frame++);
            write_ppm(path);
        }
    }
}

void shot(const char *name) {
    if(!shots_dir) return;
    ::render(clock_ms);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", shots_dir, name);
    write_ppm(path);
}

void wait(int steps, uint32_t held = 0) { for(int i = 0; i < steps; i++) step(held); }
void press(uint32_t b) { step(0, b); step(0); }

// Read every line: press A when the text has typed, until the script is
// done and Szary can walk again.
bool settle(int limit = 60000) {
    for(int i = 0; i < limit; i++) {
        if(mode() == M_PLAY && !script_busy()) return true;
        if(mode() != M_PLAY) return mode() == M_CHOICE || mode() == M_ENDING;  // the player decides
        bool typing = ui::dialog_typing();
        if((ui::dialog_open() && !typing) || ui::card_open()) {
            wait(recording ? 90 : 2);
            step(0, Button::A);
        } else {
            step(0);
        }
    }
    return false;
}

void walk_to(float x) {
    for(int i = 0; i < 6000 && fabsf(st.x - x) > 2.0f; i++) {
        settle();
        step(st.x < x ? Button::DPAD_RIGHT : Button::DPAD_LEFT);
    }
    for(int i = 0; i < 40; i++) step(0);  // stop
}

void torch(bool on) {
    if(st.torch != on) press(Button::Y);
}

// Walk to a hotspot and press A there.
void use(float x, const char *what) {
    walk_to(x);
    int h = near_hotspot();
    CHECK(h >= 0, "nothing to use at %s (x %.0f, scene %d)", what, double(st.x), int(st.scene));
    press(Button::A);
    CHECK(settle(), "stuck after %s", what);
}

int clue_row(clue::Id c) {
    for(int i = 0; i < st.nclues; i++)
        if(st.order[st.nclues - 1 - i] == c) return i;
    return -1;
}

void down(int n) { for(int i = 0; i < n; i++) press(Button::DPAD_DOWN); }

void combine(clue::Id a, clue::Id b, clue::Id expect) {
    CHECK(has(a) && has(b), "combine: missing %s or %s", clue::name[a][1], clue::name[b][1]);
    press(Button::X);
    CHECK(mode() == M_NOTEBOOK, "X opens the notebook");
    down(clue_row(a));
    press(Button::Y);  // Y combines everywhere, also next to a person
    int from = clue_row(a), to = clue_row(b);
    for(int i = 0; i < abs(to - from); i++) press(to > from ? Button::DPAD_DOWN : Button::DPAD_UP);
    press(Button::A);
    CHECK(mode() == M_PLAY, "combining closes the notebook");
    CHECK(settle(), "stuck after combining");
    CHECK(has(expect), "%s + %s should give %s", clue::name[a][1], clue::name[b][1], clue::name[expect][1]);
}

void show(float x, clue::Id c, clue::Id expect) {
    walk_to(x);
    press(Button::X);
    CHECK(mode() == M_NOTEBOOK, "X near a person opens the notebook to show");
    down(clue_row(c));
    press(Button::A);
    CHECK(settle(), "stuck after showing");
    if(expect != clue::COUNT) CHECK(has(expect), "showing %s should give %s", clue::name[c][1], clue::name[expect][1]);
}

void travel(Place p) {
    press(Button::X);
    press(Button::DPAD_LEFT);  // clues -> map
    Place list[PL_COUNT];
    int n = story::places(list);
    int at = -1;
    for(int i = 0; i < n; i++)
        if(list[i] == p) at = i;
    CHECK(at >= 0, "place %d is not on the map", int(p));
    down(at);
    press(Button::A);
    CHECK(settle(), "stuck after travelling");
}

void board(Board b, std::initializer_list<int> picks, bool expect_right) {
    press(Button::X);
    press(Button::DPAD_RIGHT);
    press(Button::DPAD_RIGHT);  // clues -> people -> questions
    txt::Id q[8]; bool closed[8]; int8_t brd[8];
    int n = story::questions(q, closed, brd);
    int at = -1;
    for(int i = 0; i < n; i++)
        if(brd[i] == b) at = i;
    CHECK(at >= 0, "no question opens board %d", int(b));
    down(at);
    press(Button::A);
    CHECK(mode() == M_BOARD, "A on the question opens board %d", int(b));
    for(int p : picks) {
        for(int i = 0; i < p; i++) press(Button::DPAD_RIGHT);
        press(Button::DPAD_DOWN);
    }
    press(Button::A);
    if(story::board(b).final) {
        press(Button::DPAD_UP);  // "not yet" -> "yes"
        press(Button::A);
    } else {
        wait(200);
        if(!expect_right) {
            CHECK(mode() == M_BOARD, "a wrong board stays open to fix");
            press(Button::B);
        }
    }
    CHECK(mode() != M_BOARD, "board %d closes", int(b));
    if(expect_right) CHECK(story::board_done(b), "board %d should be right", int(b));
    for(int i = 0; i < 6000 && mode() == M_PLAY; i++) {
        if(settle(1)) break;
    }
}

// ---------------------------------------------------------------- texts

void test_texts() {
    using namespace txt;
    for(int i = 0; i < txt::COUNT; i++)
        for(int l = 0; l < 2; l++) {
            const char *s = text[i][l];
            CHECK(font::has_all_glyphs(s), "text %d lang %d has a character with no glyph: %s", i, l, s);
            switch(who[i]) {
            case W_MYSL: CHECK(ui::wrap_lines(s, 31) <= 4, "thought too long: %s", s); break;
            case W_NARR: CHECK(ui::wrap_lines(s, 28) <= 4, "card too long: %s", s); break;
            case W_NONE: CHECK(ui::wrap_lines(s, 30) <= 3, "label too long: %s", s); break;
            default: CHECK(ui::wrap_lines(s, 30) <= 3, "line does not fit the box (3x30): %s", s); break;
            }
        }
    // stricter places
    for(int l = 0; l < 2; l++) {
        for(int i = word_kuba; i <= word_shame; i++) CHECK(font::length(text[i][l]) <= 26, "board word too long: %s", text[i][l]);
        for(int i = q_light; i <= q_final; i++) CHECK(font::length(text[i][l]) <= 28, "question too long: %s", text[i][l]);
        for(int i = board1_s1; i <= board3_s3; i++) CHECK(ui::wrap_lines(text[i][l], 31) <= 2, "board line too long: %s", text[i][l]);
        for(int i = choice_final_q; i <= choice_page_b; i++) CHECK(ui::wrap_lines(text[i][l], 28) <= 2, "choice too long: %s", text[i][l]);
        for(int i = hint_prolog; i <= hint_ch5b; i++) CHECK(ui::wrap_lines(text[i][l], 31) <= 4, "hint too long: %s", text[i][l]);
        // the notebook must not name people Szary has not met yet
        for(int i = hint_prolog; i <= hint_ch1c; i++)
            for(const char *w : {"HALIN", "KOBIET", "WOMAN", "KUBA"})
                CHECK(!strstr(text[i][l], w), "hint gives away %s: %s", w, text[i][l]);
        for(int i = place_dunes; i <= place_station; i++) CHECK(font::length(text[i][l]) <= 26, "place too long: %s", text[i][l]);
        for(int i = 0; i < clue::COUNT; i++) {
            CHECK(font::has_all_glyphs(clue::name[i][l]) && font::has_all_glyphs(clue::note[i][l]), "clue %d glyphs", i);
            CHECK(font::length(clue::name[i][l]) <= 20, "clue name too long: %s", clue::name[i][l]);
            CHECK(ui::wrap_lines(clue::note[i][l], 31) <= 4, "clue note too long: %s", clue::note[i][l]);
        }
    }
    CHECK(font::length("ŻÓŁĆ") == 4, "UTF-8 length");
    CHECK(font::glyph_index(0x142) == font::glyph_index(0x141), "ł maps to Ł");
    CHECK(font::glyph_index(0x20AC) < 0, "no glyph for the euro sign");
}

// ---------------------------------------------------------------- wind

void test_wind() {
    wind::Field f;
    f.reset(0);
    wind::Blade free_b, pressed_b;
    free_b.height = pressed_b.height = 16;
    pressed_b.pressed = true;
    float min_w = 9, max_w = -9, sum_free = 0, sum_pressed = 0;
    int n = 0;
    for(int i = 0; i < 6000; i++) {
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

// ---------------------------------------------------------------- play

// Title to the start of chapter 5, the way a player would.
void play_to_chapter5() {
    ::init();
    CHECK(mode() == M_TITLE, "starts on the title");
    CHECK(!has_save(), "no save on a fresh console");
    recording = true;
    wait(300);
    shot("01-title");
    press(Button::A);  // NEW GAME
    CHECK(mode() == M_PLAY, "new game starts");
    CHECK(settle(), "prologue card and first thought");
    recording = false;
    CHECK(st.scene == S_STATION && st.chapter == 0, "prologue at the station");
    shot("02-station");

    // prologue: the door does nothing before the telex
    use(114, "door");
    CHECK(st.chapter == 0, "the telex first");
    recording = true;
    use(78, "telex");
    recording = false;
    CHECK(flag(F_TELEX), "telex read");
    use(114, "door");
    CHECK(st.chapter == 1 && st.scene == S_DUNES, "chapter 1 on the dunes");
    CHECK(story::hint() == txt::hint_ch1a, "the first hint points at the lighthouse");
    {
        Place p[PL_COUNT];
        CHECK(story::places(p) == 1 && p[0] == PL_DUNES, "the map has only the dunes before the lighthouse");
    }

    // chapter 1
    recording = true;
    torch(true);
    walk_to(150);
    recording = false;
    use(150, "lighthouse door");
    CHECK(st.scene == S_LIGHTHOUSE && has(clue::door), "inside the lighthouse");
    {
        Place p[PL_COUNT];
        CHECK(story::places(p) == 2 && p[1] == PL_LIGHTHOUSE, "the lighthouse is on the map once visited");
    }
    use(32, "log");
    shot("03-lighthouse-inside");
    use(58, "cap");
    use(78, "lens");
    use(106, "switch");
    CHECK(has(clue::lamp) && has(clue::fuse_gone), "the fuse is gone");
    use(10, "lighthouse exit");
    CHECK(st.scene == S_DUNES, "back on the dunes");
    use(176, "bike");
    torch(false);
    use(272, "trail in the dark");
    CHECK(!has(clue::trail), "no trail without the torch");
    torch(true);
    recording = true;
    use(272, "trail");
    recording = false;
    CHECK(has(clue::trail), "the torch shows the trail");
    walk_to(290);
    wait(100);
    shot("04-trail-torch");
    use(334, "trail end");
    CHECK(story::hint() == txt::hint_ch1c && !flag(F_HALINA1), "before Halina the hint only mentions a fire");
    recording = true;
    use(494, "Halina");
    recording = false;
    CHECK(flag(F_CH1_DONE), "chapter 1 done");
    use(518, "barrel");
    walk_to(470);
    wait(60);
    shot("05-halina-fire");
    CHECK(has(clue::barrel), "the barrel was dragged");
    combine(clue::bike, clue::nosand, clue::w_rider);
    CHECK(story::clue_spent(clue::bike) && story::clue_spent(clue::nosand), "combined clues are spent");
    CHECK(!story::clue_spent(clue::lamp) && !story::clue_spent(clue::door), "clues never combined stay bright");
    combine(clue::h_evening, clue::h_bike, clue::w_saw);
    combine(clue::cap, clue::trail, clue::w_led);
    press(Button::X);
    shot("06-notebook-clues");
    press(Button::DPAD_RIGHT);
    press(Button::DPAD_RIGHT);
    shot("07-notebook-questions");
    press(Button::DPAD_RIGHT);
    shot("08-notebook-map");
    press(Button::B);
    show(494, clue::w_saw, clue::h_930);
    // the language can change any time
    press(Button::MENU);
    CHECK(mode() == M_MENU, "MENU pauses");
    down(1);
    press(Button::A);
    CHECK(st.lang == EN, "the menu switches to English");
    ::render(clock_ms);
    press(Button::A);
    CHECK(st.lang == PL, "and back");
    press(Button::MENU);
    CHECK(mode() == M_PLAY, "MENU resumes");

    // chapter 2
    travel(PL_BAR);
    CHECK(st.chapter == 2 && st.scene == S_BAR, "chapter 2 in the bar");
    recording = true;
    use(30, "Rysiek");
    recording = false;
    use(108, "Kuba");
    use(126, "boots");
    use(150, "window");
    use(82, "photo");
    use(56, "radio");
    walk_to(70);
    wait(60);
    shot("09-bar");
    show(108, clue::r_time, clue::k_time);
    show(108, clue::log, clue::woman);
    combine(clue::r_time, clue::k_time, clue::w_lie);
    combine(clue::boots, clue::boatlight, clue::w_kboat);
    board(B_LIE, {0, 0}, false);
    CHECK(!story::board_done(B_LIE), "a wrong board stays open");
    board(B_LIE, {1, 1}, true);
    CHECK(flag(F_CH2_DONE) && has(clue::smuggle), "Rysiek confesses");

    // chapter 3
    travel(PL_PIER);
    CHECK(st.chapter == 3 && st.scene == S_PIER, "chapter 3 on the pier");
    recording = true;
    use(292, "boat");
    recording = false;
    use(404, "pier end");
    walk_to(300);
    wait(60);
    shot("10-pier-rain");
    use(40, "office door");
    CHECK(st.scene == S_OFFICE, "in the harbour office");
    use(44, "Jasinski");
    use(70, "timetable");
    use(94, "chart");
    use(116, "archive");
    walk_to(60);
    shot("11-office");
    CHECK(flag(F_CH3_DONE) && has(clue::report), "chapter 3 done");
    show(44, clue::paper, clue::captain);
    combine(clue::aurora, clue::h_asked, clue::w_waits);
    combine(clue::paper, clue::photo, clue::w_mewa);
    combine(clue::photo, clue::woman, clue::w_kowal);

    // chapter 4
    travel(PL_DUNES);
    CHECK(st.chapter == 4 && st.scene == S_DUNES, "chapter 4: the storm");
    torch(true);
    recording = true;
    use(392, "tuft 1");
    recording = false;
    use(450, "tuft 2");
    wait(60);
    shot("12-storm-trail");
    use(604, "tuft 3");
    CHECK(flag(F_TRAIL4), "the trail leads to the bunker");
    use(662, "bunker");
    CHECK(has(clue::padlock) && !flag(F_BUNKER_OPEN), "padlocked");
    use(662, "bunker without crowbar");
    use(44, "car");
    CHECK(has(clue::crowbar), "crowbar from the car");
    use(662, "bunker with crowbar");
    CHECK(st.scene == S_BUNKER, "inside the bunker");
    recording = true;
    use(56, "Bogdan");
    recording = false;
    use(74, "pocket");
    shot("13-bunker");
    use(104, "shelf");
    CHECK(has(clue::b_fire) && has(clue::page) && has(clue::fuse), "bunker clues");
    if(shots_dir) {
        press(Button::X); press(Button::DPAD_RIGHT); press(Button::DPAD_RIGHT); press(Button::A);
        press(Button::DPAD_RIGHT); press(Button::DPAD_RIGHT);
        shot("14-board");
        press(Button::B);
    }
    board(B_FIRE, {2, 1, 2}, true);
    CHECK(st.chapter == 5 && st.scene == S_DUNES, "chapter 5 at dawn");
    CHECK(has_save(), "the game saved itself");
    wait(3000);
    shot("15-dawn-aurora");
}

void continue_game() {
    back_to_title();
    wait(50);
    CHECK(mode() == M_TITLE, "on the title");
    press(Button::A);  // CONTINUE is first when there is a save
    CHECK(mode() == M_PLAY && st.chapter == 5, "continue loads chapter 5");
    CHECK(settle(), "continue settles");
}

void finish(Ending e) {
    CHECK(mode() == M_ENDING, "an ending plays (%d)", int(e));
    CHECK(st.endings & (1u << e), "ending %d recorded", int(e));
    for(int i = 0; i < 3000 && mode() == M_ENDING; i++) {
        wait(recording ? 120 : 30);
        step(0, Button::A);
    }
    CHECK(mode() == M_TITLE, "ending returns to the title");
}

void test_play() {
    play_to_chapter5();

    // talking to Halina before the last board does not end the game
    use(494, "Halina too early");
    CHECK(mode() == M_PLAY, "the board comes first");

    // 1. the wrong accusation: the Sandbank
    board(B_FINAL, {0, 0, 0}, false);
    finish(E_BANK);

    // 2. the right board, then sit by the fire: Wind
    continue_game();
    CHECK(!flag(F_BOARD3), "a lost ending was not saved over the game");
    board(B_FINAL, {2, 2, 2}, true);
    CHECK(flag(F_ACCUSE_OK), "Halina named");
    txt::Id q[8]; bool closed[8]; int8_t b[8];
    int n = story::questions(q, closed, b);
    for(int i = 0; i < n; i++) CHECK(closed[i], "question %d still open at the end", i);
    use(494, "Halina final");
    CHECK(mode() == M_CHOICE, "the last choice");
    press(Button::DPAD_DOWN);
    press(Button::A);
    settle(100);
    finish(E_WIND);

    // 3. relight, the page to the report: Dawn
    continue_game();
    CHECK(flag(F_BOARD3), "the right board was saved");
    recording = true;
    use(494, "Halina final");
    press(Button::A);  // relight
    for(int i = 0; i < 6000 && mode() == M_PLAY; i++) settle(1);
    CHECK(flag(F_RELIT) && world::beam > 0.0f, "the lighthouse burns");
    shot("16-beam");
    {
        // the beam sweeps the sky: bright pixels above the dunes over a few seconds
        int bright = 0;
        for(int k = 0; k < 10; k++) {
            wait(50);
            ::render(clock_ms);
            for(int y = 45; y < 70; y++)
                for(int x = 0; x < 128; x++) bright += ((framebuffer[y * 128 + x] >> 5) & 63) > 40;
        }
        CHECK(bright > 200, "the lighthouse beam lights the sky (%d)", bright);
    }
    CHECK(mode() == M_CHOICE, "the page");
    wait(120);
    press(Button::A);
    finish(E_DAWN);
    recording = false;

    // 4. relight, the page in the fire: Ash
    continue_game();
    use(494, "Halina final");
    press(Button::A);
    for(int i = 0; i < 6000 && mode() == M_PLAY; i++) settle(1);
    press(Button::DPAD_DOWN);
    press(Button::A);
    finish(E_ASH);
    CHECK(st.endings == 15, "all four endings seen");

    // chapter select after the endings
    wait(20);
    press(Button::DPAD_DOWN);
    press(Button::DPAD_DOWN);  // continue, new game, chapters
    press(Button::A);
    for(int i = 0; i < 3; i++) press(Button::DPAD_RIGHT);
    press(Button::A);
    CHECK(settle(), "chapter select settles");
    CHECK(st.chapter == 4 && st.scene == S_DUNES && has(clue::report), "chapter 4 from the title with its clues");

    // a stuck player hears a hint
    CHECK(!ui::dialog_open(), "quiet");
    wait(12100);
    CHECK(ui::dialog_open(), "a hint after two idle minutes");
    settle();

    // a long stall (system menu) does not run hundreds of steps
    clock_ms += 60000;
    ::update(clock_ms);

    CHECK(frames_checked > 50 && grey_frames == frames_checked, "every frame is grey (%d of %d)", grey_frames,
          frames_checked);
}

void bench() {
    ::init();
    press(Button::A);
    settle();
    story::grant_until(4);
    clear_script();
    story::start_chapter(4, false);
    settle();
    torch(true);
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
    printf("render on this machine (storm, torch): %.0f us average, %.0f us worst, %d frames\n", total_us / frames,
           worst_us, frames);
}

}  // namespace

int main(int argc, char **argv) {
    if(argc >= 2 && !strcmp(argv[1], "--bench")) {
        bench();
        return 0;
    }
    if(argc >= 3 && !strcmp(argv[1], "--shots")) shots_dir = argv[2];
    if(argc >= 3 && !strcmp(argv[1], "--clip")) {
        clip_dir = argv[2];
        if(argc >= 4) clip_every = atoi(argv[3]);
    }
    test_texts();
    test_wind();
    test_play();
    if(clip_dir) printf("%d frames\n", clip_frame);
    if(failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
