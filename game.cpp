// Szary: a grey ASCII noir for the ChiliChip VGC Zero.
//
// The game loop: modes (title, play, notebook, board, choice, menu,
// ending), input, the script that runs story actions one by one, and the
// save. See szary.hpp for how the files fit together.

#include <cmath>
#include <cstring>

#include "32blit.hpp"
#include "font.hpp"
#include "szary.hpp"

using namespace blit;

namespace sz {

State st;
float fps_value_f = 0.0f;
uint32_t render_us = 0;

namespace {

Mode cur = M_TITLE;

// ---------------------------------------------------------------- script

constexpr int QMAX = 96, PMAX = 48;
Act queue[QMAX];
int qn = 0;
Act pending[PMAX];
int pn = 0;
float wait_s = 0.0f;
bool save_dirty = false;
float glide_to = -1.0f;  // Szary steps up to the person he talks to

// Actions pushed while one runs go right after it, before the rest.
void merge() {
    if(pn == 0) return;
    if(qn + pn > QMAX) pn = QMAX - qn;
    memmove(queue + pn, queue, sizeof(Act) * size_t(qn));
    memcpy(queue, pending, sizeof(Act) * size_t(pn));
    qn += pn;
    pn = 0;
}

bool blocking() { return ui::dialog_open() || ui::card_open() || wait_s > 0.0f || cur != M_PLAY; }

void progress() {
    st.idle_s = 0.0f;
    st.hint_level = 0;
    save_dirty = true;
}

void run(const Act &a) {
    switch(a.kind) {
    case A_SAY: ui::say(txt::Group(a.a)); break;
    case A_LINE: ui::say_line(txt::Id(a.a)); break;
    case A_CLUE: {
        clue::Id c = clue::Id(a.a);
        if(has(c)) break;
        st.clues |= uint64_t(1) << c;
        if(st.nclues < 64) st.order[st.nclues++] = uint8_t(c);
        ui::toast(c >= clue::w_rider ? txt::ui_conclusion : txt::ui_newclue, clue::name[c][st.lang]);
        audio::page();
        story::after_clue(c);
        progress();
        break;
    }
    case A_FLAG:
        set_flag(Flag(a.a));
        story::after_clue(clue::COUNT);
        progress();
        break;
    case A_CARD: ui::card(txt::Id(a.a)); break;
    case A_SCENE:
        st.scene = Scene(a.a);
        st.x = fabsf(a.f);
        if(a.f < 0.0f) st.facing = -1;  // a negative x means: face left
        glide_to = -1.0f;
        st.vx = 0.0f;
        if(st.scene != S_DUNES && st.scene != S_BUNKER && st.scene != S_LIGHTHOUSE) st.torch = false;
        world::enter(st.scene);
        audio::step();
        break;
    case A_CHAPTER:
        st.chapter = uint8_t(a.a);
        progress();
        break;
    case A_WAIT: wait_s = a.f; break;
    case A_EVENT:
        if(a.a == 0xffff) { ui::notebook_open_map(); cur = M_NOTEBOOK; }
        else story::event(a.a);
        break;
    case A_CHOICE:
        ui::choice_open(a.a);
        cur = M_CHOICE;
        break;
    case A_ENDING:
        st.endings |= uint8_t(1u << a.a);
        save_settings();
        clear_script();
        ui::ending_open(Ending(a.a));
        cur = M_ENDING;
        break;
    case A_SAVE: save_dirty = true; break;
    case A_BEAM:
        world::beam = 0.01f;
        audio::hum(true);
        break;
    }
}

void run_script() {
    for(int guard = 0; guard < 64; guard++) {
        merge();
        if(blocking() || qn == 0) break;
        Act a = queue[0];
        qn--;
        memmove(queue, queue + 1, sizeof(Act) * size_t(qn));
        run(a);
    }
    merge();
    if(save_dirty && qn == 0 && !blocking() && cur == M_PLAY) {
        save_game();
        save_dirty = false;
    }
}

// ---------------------------------------------------------------- save

constexpr uint32_t SAVE_MAGIC = 0x53415a31;  // "SZA1"
constexpr uint32_t SETTINGS_MAGIC = 0x53415a53;

struct SaveData {
    uint32_t magic;
    uint8_t chapter, scene;
    int8_t facing;
    uint8_t nclues;
    float x;
    uint64_t flags, clues;
    uint8_t order[64];
};

struct Settings {
    uint32_t magic;
    uint8_t lang, speed, show_fps, endings;
};

// ---------------------------------------------------------------- play

float step_dist = 0.0f;

void play_input() {
    bool busy = script_busy();

    if(buttons.pressed & Button::MENU) {
        ui::menu_open();
        cur = M_MENU;
        return;
    }
    if(ui::card_open()) {
        if(buttons.pressed & (Button::A | Button::B)) ui::card_press();
        return;
    }
    if(ui::dialog_open()) {
        if(buttons.pressed & (Button::A | Button::B)) ui::dialog_press();
        return;
    }
    if(buttons.pressed & Button::Y) { st.torch = !st.torch; audio::click(); }

    float want = 0.0f;
    if(!busy) {
        float speed = (buttons & Button::B) ? 58.0f : 34.0f;
        if(buttons & Button::DPAD_LEFT) want = -speed;
        if(buttons & Button::DPAD_RIGHT) want = speed;
    }
    st.vx += (want - st.vx) * (want == 0.0f ? 0.18f : 0.08f);
    if(fabsf(st.vx) < 0.3f && want == 0.0f) st.vx = 0.0f;
    if(want > 0) st.facing = 1;
    if(want < 0) st.facing = -1;
    st.x += st.vx * DT;
    float w = world::width(st.scene);
    if(st.x < 8.0f) { st.x = 8.0f; st.vx = 0; }
    if(st.x > w - 8.0f) { st.x = w - 8.0f; st.vx = 0; }
    st.walk += fabsf(st.vx) * DT;
    step_dist += fabsf(st.vx) * DT;
    if(step_dist > 11.0f) { step_dist = 0.0f; audio::step(); }

    if(glide_to >= 0.0f) {
        float d = glide_to - st.x, v = 40.0f * DT;
        if(fabsf(d) <= v) {
            st.x = glide_to;
            glide_to = -1.0f;
        } else {
            st.x += d > 0 ? v : -v;
            st.walk += v;
        }
    }

    if(busy) return;

    if(buttons.pressed & Button::A) {
        int h = near_hotspot();
        if(h >= 0) {
            st.idle_s = 0.0f;
            const Hotspot &hs = story::hotspot(h);
            if(hs.kind == HK_TALK) {
                // stand beside the person, facing them, not on top of them
                int side = st.x <= float(hs.x) ? 1 : -1;
                float to = float(hs.x) - float(side) * 18.0f;
                if(to < 8.0f) { side = -1; to = float(hs.x) + 18.0f; }
                st.facing = int8_t(side);
                glide_to = to;
            }
            story::use_hotspot(h);
            return;
        }
    }
    if(buttons.pressed & Button::X) {
        int h = near_hotspot();
        Person p = h >= 0 ? story::hotspot_person(h) : P_NONE;
        ui::notebook_open(p);
        cur = M_NOTEBOOK;
        return;
    }

    // a thought nudges a player who is stuck
    st.idle_s += DT;
    if(st.idle_s > 120.0f) {
        st.idle_s = 0.0f;
        push(A_LINE, story::hint());
    }
}

void enter_title() {
    uint8_t lang = st.lang, speed = st.speed, endings = st.endings;
    bool fps = st.show_fps;
    st = State();
    st.lang = lang; st.speed = speed; st.endings = endings; st.show_fps = fps;
    st.chapter = 1;
    st.scene = S_DUNES;
    st.x = 300.0f;
    world::show_player = false;
    world::enter(S_DUNES);
    ui::title_open();
    cur = M_TITLE;
}

void tick() {
    st.time_s += DT;
    world::update(DT);
    ui::dialog_update(DT);
    ui::card_update(DT);
    if(wait_s > 0.0f) wait_s -= DT;
    if(wait_s < 0.0f) wait_s = 0.0f;

    switch(cur) {
    case M_TITLE:
        st.x += 9.0f * DT;
        if(st.x > 640.0f) { st.x = 140.0f; world::set_cam_instant(); }
        if(!ui::title_update()) {
            world::show_player = true;
            cur = M_PLAY;
        }
        break;
    case M_PLAY:
        play_input();
        break;
    case M_NOTEBOOK:
        if(!ui::notebook_update()) {
            cur = M_PLAY;
            if(ui::board_request >= 0) {
                ui::board_open(Board(ui::board_request));
                ui::board_request = -1;
                cur = M_BOARD;
            }
        }
        break;
    case M_BOARD:
        if(!ui::board_update()) cur = M_PLAY;
        break;
    case M_CHOICE:
        if(!ui::choice_update()) cur = M_PLAY;
        break;
    case M_MENU:
        if(!ui::menu_update()) {
            cur = M_PLAY;
            if(ui::quit_to_title) back_to_title();
        }
        break;
    case M_ENDING:
        if(!ui::ending_update()) back_to_title();
        break;
    }
    if(cur == M_PLAY) run_script();
}

uint32_t last_update_ms = 0;
uint32_t fps_frames = 0, fps_last = 0;

}  // namespace

// ================================================================ script API

void push(ActKind k, uint16_t a, float f) {
    if(pn < PMAX) pending[pn++] = {k, a, f};
}

bool script_busy() { return qn > 0 || pn > 0 || ui::dialog_open() || ui::card_open() || wait_s > 0.0f; }

void clear_script() {
    qn = 0;
    pn = 0;
    wait_s = 0.0f;
    save_dirty = false;
}

Mode mode() { return cur; }

int near_hotspot() {
    int best = -1;
    float best_d = 1e9f;
    for(int i = 0; i < story::hotspot_count(); i++) {
        const Hotspot &h = story::hotspot(i);
        if(h.scene != st.scene || !story::hotspot_active(i)) continue;
        float d = fabsf(float(h.x) - st.x);
        float range = h.kind == HK_TALK ? 18.0f : 14.0f;
        if(d < range && d < best_d) { best = i; best_d = d; }
    }
    return best;
}

// ================================================================ save

void save_game() {
    SaveData d{};
    d.magic = SAVE_MAGIC;
    d.chapter = st.chapter;
    d.scene = st.scene;
    d.facing = st.facing;
    d.nclues = st.nclues;
    d.x = st.x;
    d.flags = st.flags;
    d.clues = st.clues;
    memcpy(d.order, st.order, sizeof(d.order));
    write_save(d, 0);
}

bool has_save() {
    SaveData d{};
    return read_save(d, 0) && d.magic == SAVE_MAGIC;
}

bool load_game() {
    SaveData d{};
    if(!read_save(d, 0) || d.magic != SAVE_MAGIC || d.scene >= S_COUNT) return false;
    clear_script();
    st.chapter = d.chapter;
    st.scene = Scene(d.scene);
    st.facing = d.facing < 0 ? -1 : 1;
    st.nclues = d.nclues > 64 ? 64 : d.nclues;
    st.x = d.x;
    st.flags = d.flags;
    st.clues = d.clues;
    memcpy(st.order, d.order, sizeof(st.order));
    st.vx = 0.0f;
    st.torch = false;
    st.idle_s = 0.0f;
    world::beam = flag(F_RELIT) ? 10.0f : 0.0f;
    world::show_player = true;
    world::enter(st.scene);
    return true;
}

void save_settings() {
    Settings s{SETTINGS_MAGIC, st.lang, st.speed, uint8_t(st.show_fps), st.endings};
    write_save(s, 1);
}

void back_to_title() {
    clear_script();
    enter_title();
}

}  // namespace sz

// ================================================================ 32blit

using namespace sz;

void init() {
    set_screen_mode(ScreenMode::hires);
    Settings s{};
    if(read_save(s, 1) && s.magic == SETTINGS_MAGIC) {
        st.lang = s.lang > 1 ? 0 : s.lang;
        st.speed = s.speed > 2 ? 1 : s.speed;
        st.show_fps = s.show_fps != 0;
        st.endings = s.endings;
    }
    world::init();
    audio::init();
    enter_title();
}

void update(uint32_t time) {
    // fixed steps; after a long stall (system menu) do not race to catch up
    if(last_update_ms == 0 || time - last_update_ms > 250) last_update_ms = time - 10;
    while(time - last_update_ms >= 10) {
        tick();
        buttons.pressed = 0;  // only the first step of a frame sees a press
        last_update_ms += 10;
    }
    audio::update();
}

void render(uint32_t time) {
    uint32_t start = now_us();
    screen.pen = Pen(0, 0, 0);
    screen.clear();

    switch(cur) {
    case M_NOTEBOOK: ui::notebook_draw(); break;
    case M_BOARD: ui::board_draw(); break;
    case M_ENDING: ui::ending_draw(); break;
    default:
        world::draw();
        if(cur == M_TITLE) ui::title_draw();
        if(cur == M_PLAY || cur == M_MENU || cur == M_CHOICE) {
            if(cur == M_PLAY && !script_busy()) {
                int h = near_hotspot();
                if(h >= 0) {
                    static const txt::Id labels[4] = {txt::ui_look, txt::ui_talk, txt::ui_enter, txt::ui_exit};
                    const Hotspot &hs = story::hotspot(h);
                    int y = int(world::floor_y(st.scene, float(hs.x))) - 44;
                    ui::prompt(world::screen_x(float(hs.x)), y, labels[hs.kind]);
                }
            }
            ui::dialog_draw();
        }
        if(cur == M_CHOICE) ui::choice_draw();
        if(cur == M_MENU) ui::menu_draw();
        break;
    }
    ui::overlay_draw();

    render_us = us_diff(start, now_us());
    fps_frames++;
    if(time - fps_last >= 1000) {
        fps_value_f = float(fps_frames) * 1000.0f / float(time - fps_last);
        fps_frames = 0;
        fps_last = time;
    }
}
