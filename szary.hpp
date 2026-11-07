#pragma once
// Shared state and interfaces of Szary. See README.md for the overview.
//
//   game.cpp   init/update/render, input, modes, save
//   story.cpp  what happens: hotspots, clues, combining, showing, boards,
//              chapters, endings, hints (the content logic)
//   world.cpp  drawing the places: dunes, pier, rooms, people, weather
//   ui.cpp     dialog box, notebook, boards, menus, title, cards
//   audio.cpp  wind, rain, sea and small sounds from the SDK synth
//   font.cpp   the ASCII glyphs; wind.hpp the wind and grass

#include <cstdint>
#include <string_view>

#include "texts_gen.hpp"

namespace sz {

constexpr int SCREEN = 128;
constexpr float DT = 0.01f;  // one update step

enum Lang : uint8_t { PL = 0, EN = 1 };

enum Scene : uint8_t { S_STATION, S_DUNES, S_LIGHTHOUSE, S_BAR, S_PIER, S_OFFICE, S_BUNKER, S_COUNT };

enum Person : uint8_t { P_NONE, P_HALINA, P_KUBA, P_RYSIEK, P_JASINSKI, P_BOGDAN, P_COUNT };

// Things that happened. Stored in the save, so only append.
enum Flag : uint8_t {
    F_TELEX, F_CH1_STARTED, F_LH_ENTERED, F_SWITCH, F_HALINA1, F_TRAIL_END, F_CH1_DONE,
    F_CH2_STARTED, F_RYSIEK1, F_KUBA1, F_PHOTO, F_BOARD1, F_CH2_DONE,
    F_CH3_STARTED, F_JAS1, F_ARCHIVE, F_CH3_DONE,
    F_CH4_STARTED, F_TRAIL2, F_TRAIL3, F_TRAIL4, F_BUNKER_OPEN, F_BOGDAN1, F_FIRE_THOUGHT, F_BOARD2, F_CH4_DONE,
    F_CH5_STARTED, F_BOARD3, F_ACCUSE_OK, F_RELIT, F_LEGEND_EARLY, F_WINDOW, F_RADIO_BAR, F_SHOW_THOUGHT,
    F_COUNT
};
static_assert(F_COUNT <= 64, "flags live in a uint64_t");
static_assert(clue::COUNT <= 64, "clues live in a uint64_t");

enum Ending : uint8_t { E_DAWN, E_ASH, E_WIND, E_BANK, E_COUNT };

enum Board : uint8_t { B_LIE, B_FIRE, B_FINAL, B_COUNT };

enum Place : uint8_t { PL_DUNES, PL_LIGHTHOUSE, PL_BAR, PL_PIER, PL_OFFICE, PL_BUNKER, PL_COUNT };

enum HotKind : uint8_t { HK_LOOK, HK_TALK, HK_ENTER, HK_EXIT };

struct Hotspot {
    Scene scene;
    int16_t x;
    HotKind kind;
    uint8_t id;
};

// ------------------------------------------------------------------ state

struct State {
    // saved
    uint8_t chapter = 0;
    Scene scene = S_STATION;
    float x = 60.0f;
    int8_t facing = 1;
    uint64_t flags = 0;
    uint64_t clues = 0;
    uint8_t order[64] = {};  // clues in the order Szary noted them
    uint8_t nclues = 0;

    // settings (saved separately)
    uint8_t lang = PL;
    uint8_t speed = 1;  // 0 slow, 1 normal, 2 fast
    bool show_fps = false;
    uint8_t endings = 0;  // bit per Ending seen

    // not saved
    bool torch = false;
    float vx = 0.0f, walk = 0.0f;
    float time_s = 0.0f;       // since start of the program
    float idle_s = 0.0f;       // since the last progress (for hints)
    int hint_level = 0;
};
extern State st;

inline bool flag(Flag f) { return (st.flags >> f) & 1u; }
inline void set_flag(Flag f) { st.flags |= uint64_t(1) << f; }
inline bool has(clue::Id c) { return (st.clues >> c) & 1u; }

inline const char *T(txt::Id id) { return txt::text[id][st.lang]; }

// ------------------------------------------------------------------ script
// Story code queues actions; the game runs them one by one, waiting for
// each dialog, card or choice to finish.

enum ActKind : uint8_t {
    A_SAY,      // a = dialog group
    A_CLUE,     // a = clue (notes it, shows "new clue")
    A_FLAG,     // a = flag
    A_CARD,     // a = text id, full-screen card
    A_SCENE,    // a = scene, f = x
    A_CHAPTER,  // a = chapter number
    A_WAIT,     // f = seconds
    A_EVENT,    // a = story event id (story::event)
    A_CHOICE,   // a = choice id (story::choice)
    A_ENDING,   // a = ending
    A_SAVE,
    A_BEAM,     // the lighthouse lights up
    A_LINE,     // a = one text id (a thought, a hint)
};
struct Act {
    ActKind kind;
    uint16_t a;
    float f;
};
// Queue actions at the end, or (inside story callbacks) right after the
// action that is running now.
void push(ActKind k, uint16_t a = 0, float f = 0.0f);
bool script_busy();
void clear_script();

// ------------------------------------------------------------------ story

namespace story {
void new_game();
void start_chapter(int ch, bool with_card);
void grant_until(int ch);  // clues and flags of a normal play before chapter ch

int hotspot_count();
const Hotspot &hotspot(int i);
bool hotspot_active(int i);
bool hotspot_dark(int i);  // needs the torch, and it is off
void use_hotspot(int i);
Person hotspot_person(int i);

void combine(clue::Id a, clue::Id b);
void show(Person p, clue::Id c);
void event(uint16_t id);
void choice(uint16_t id, int option);
void after_clue(clue::Id c);  // checks chapter progress

// Notebook
bool met(Person p);
bool clue_spent(clue::Id c);  // every combination and useful show with it is done
int person_lines(Person p, txt::Id out[3]);
int questions(txt::Id out[8], bool closed[8], int8_t board[8]);
int places(Place out[PL_COUNT]);
void travel(Place p);
txt::Id place_name(Place p);

// Boards
struct BoardDef {
    uint8_t blanks;
    txt::Id prompt[3];
    uint8_t options[3];
    txt::Id words[3][4];
    uint8_t answer[3];
    bool final;  // no feedback, commits
};
const BoardDef &board(Board b);
bool board_available(Board b);
bool board_done(Board b);
// Returns true when right (for the final board it commits either way).
bool board_submit(Board b, const uint8_t picks[3]);

txt::Id hint();
float hint_delay_s();  // seconds without progress before a hint
}  // namespace story

// ------------------------------------------------------------------ world

namespace world {
void init();
void enter(Scene s);
void update(float dt);
void draw();                 // the place, people, weather (not the player UI)
float width(Scene s);
float floor_y(Scene s, float x);
bool outdoors(Scene s);
float wind_at(float x);
void set_cam_instant();
int screen_x(float x);         // world x to screen x in the current scene
extern bool show_player;     // false on the title screen
extern float beam;           // 0 = lighthouse dark; >0 seconds since it lit
extern float aurora;         // 0..1 how far the Aurora's lights came
}  // namespace world

// ------------------------------------------------------------------ ui

namespace ui {
// dialog
void say(txt::Group g);
void say_line(txt::Id id);
bool dialog_open();
bool line_said(txt::Id id);  // the last single line (thought or hint) was this one
bool dialog_typing();
void dialog_press();
void dialog_update(float dt);
void dialog_draw();

void toast(txt::Id label, const char *what);
void card(txt::Id id);
bool card_open();
void card_press();
void card_update(float dt);

void prompt(int x, int y, txt::Id label);

// full-screen modes; each returns false when it closes
void notebook_open(Person show_to);
void notebook_open_map();
extern int8_t board_request;  // set by the notebook: open this board next
bool notebook_update();
void notebook_draw();

void board_open(Board b);
bool board_update();
void board_draw();

void choice_open(uint16_t id);
bool choice_update();
void choice_draw();

void menu_open();
bool menu_update();  // returns false to resume; sets quit_to_title
void menu_draw();
extern bool quit_to_title;

void title_open();
bool title_update();  // returns false when a game starts
void title_draw();

void ending_open(Ending e);
bool ending_update();
void ending_draw();

void overlay_draw();  // toasts, fps

int wrap_lines(std::string_view s, int cols);
}  // namespace ui

// ------------------------------------------------------------------ audio

namespace audio {
void init();
void update();
void click();
void type_blip();
void telex();
void page();
void thud();
void step();
void hum(bool on);
void motif(int which);  // 0 title, 1 ending
}  // namespace audio

// ------------------------------------------------------------------ game

enum Mode : uint8_t { M_TITLE, M_PLAY, M_NOTEBOOK, M_BOARD, M_CHOICE, M_MENU, M_ENDING };
Mode mode();
int near_hotspot();  // index of the hotspot Szary stands at, or -1

void save_game();
bool load_game();
bool has_save();
void save_settings();
void back_to_title();
extern float fps_value_f;
extern uint32_t render_us;

}  // namespace sz
