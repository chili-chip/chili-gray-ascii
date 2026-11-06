// What sits on top of the world: the dialog box, thoughts, cards, the
// notebook, boards, choices, the pause menu, the title and the endings.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "32blit.hpp"
#include "font.hpp"
#include "szary.hpp"

using namespace blit;

namespace sz {
namespace ui {

bool quit_to_title = false;
int8_t board_request = -1;

namespace {

constexpr int BOX_COLS = 30, BOX_LINES = 3;
constexpr int THOUGHT_COLS = 31, THOUGHT_LINES = 4;
const char SOLID = 127;

float now_s() { return st.time_s; }

struct Wrapped {
    std::string_view lines[10];
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
        if(chars > cols && w.count < 9) {
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

void black(int x, int y, int w, int h) {
    screen.pen = Pen(0, 0, 0);
    screen.rectangle(Rect(x, y, w, h));
}

int text_w(std::string_view s) { return font::length(s) * font::CELL_W; }

void centered(int y, std::string_view s, int shade, int max_chars = 1 << 30) {
    font::text((SCREEN - text_w(s)) / 2, y, s, shade, max_chars);
}

// Wrapped text, at most `max_lines` lines from (x, y), with typing.
int block(int x, int y, std::string_view s, int cols, int max_lines, int shade, int shown = 1 << 30, bool centre = false) {
    Wrapped w = wrap(s, cols);
    int left = shown;
    int n = w.count < max_lines ? w.count : max_lines;
    for(int i = 0; i < n && left > 0; i++) {
        int lx = centre ? (SCREEN - text_w(w.lines[i])) / 2 : x;
        int d = font::text(lx, y + i * font::CELL_H, w.lines[i], shade, left);
        left -= d + 1;
    }
    return n;
}

bool blink() { return fmodf(now_s(), 1.0f) < 0.6f; }

float type_speed() {
    static const float s[3] = {22.0f, 40.0f, 90.0f};
    return s[st.speed > 2 ? 1 : st.speed];
}

// ---------------------------------------------------------------- dialog

uint16_t dlg_first = 0, dlg_count = 0, dlg_index = 0;
bool dlg_on = false;
float dlg_shown = 0.0f;

txt::Id dlg_id() { return txt::Id(dlg_first + dlg_index); }
int dlg_len() { return font::length(T(dlg_id())); }

void dlg_start(uint16_t first, uint16_t count) {
    dlg_first = first;
    dlg_count = count;
    dlg_index = 0;
    dlg_shown = 0.0f;
    dlg_on = count > 0;
}

const char *speaker_name(uint8_t who) {
    switch(who) {
    case txt::W_SZARY: return "SZARY";
    case txt::W_HALINA: return "HALINA";
    case txt::W_KUBA: return "KUBA";
    case txt::W_RYSIEK: return "RYSIEK";
    case txt::W_JASINSKI: return "JASIŃSKI";
    case txt::W_BOGDAN: return "BOGDAN";
    case txt::W_RADIO: return "RADIO";
    case txt::W_NONE: return st.lang == PL ? "DALEKOPIS" : "TELEX";
    default: return nullptr;
    }
}

// ---------------------------------------------------------------- toasts

struct Toast { txt::Id label; const char *what; float until; };
Toast toasts[3];
int ntoasts = 0;

// ---------------------------------------------------------------- card

bool card_on = false;
txt::Id card_id = txt::COUNT;
float card_shown = 0.0f, card_t = 0.0f;

// ---------------------------------------------------------------- notebook

enum Tab { T_CLUES, T_PEOPLE, T_QUESTIONS, T_MAP, T_COUNT };
int tab = T_CLUES;
int sel[T_COUNT] = {};
int first_pick = -1;
Person show_to = P_NONE;

const Person people_order[] = {P_HALINA, P_KUBA, P_RYSIEK, P_JASINSKI, P_BOGDAN};

int people_list(Person out[5]) {
    int n = 0;
    for(Person p : people_order)
        if(story::met(p)) out[n++] = p;
    return n;
}

int clue_count() { return st.nclues; }
clue::Id clue_at(int i) { return clue::Id(st.order[st.nclues - 1 - i]); }  // newest first

int tab_size(int tb) {
    switch(tb) {
    case T_CLUES: return clue_count();
    case T_PEOPLE: { Person p[5]; return people_list(p); }
    case T_QUESTIONS: { txt::Id q[8]; bool c[8]; int8_t b[8]; return story::questions(q, c, b); }
    case T_MAP: { Place p[PL_COUNT]; return story::places(p); }
    default: return 0;
    }
}

// ---------------------------------------------------------------- board

Board brd = B_LIE;
uint8_t picks[3] = {};
int brow = 0;
enum BPhase { BP_EDIT, BP_RIGHT, BP_WRONG, BP_SURE };
BPhase bphase = BP_EDIT;
float bphase_t = 0.0f;
int sure_sel = 1;

// ---------------------------------------------------------------- choice

uint16_t ch_id = 0;
int ch_sel = 0;

// ---------------------------------------------------------------- menu

int menu_sel = 0;

// ---------------------------------------------------------------- title

int title_sel = 0;
bool title_chapters = false;
int title_chapter = 1;
float title_t = 0.0f;

enum TitleItem { TI_CONTINUE, TI_NEW, TI_CHAPTERS, TI_LANG };
int title_items(TitleItem out[4]) {
    int n = 0;
    if(has_save()) out[n++] = TI_CONTINUE;
    out[n++] = TI_NEW;
    if(st.endings) out[n++] = TI_CHAPTERS;
    out[n++] = TI_LANG;
    return n;
}

// ---------------------------------------------------------------- ending

Ending end_e = E_DAWN;
int end_line = 0;
float end_shown = 0.0f;
bool end_final = false;

txt::Group end_group(Ending e) {
    static const txt::Group g[E_COUNT] = {txt::G_end_dawn, txt::G_end_ash, txt::G_end_wind, txt::G_end_bank};
    return g[e];
}

int endings_seen() {
    int n = 0;
    for(int i = 0; i < E_COUNT; i++) n += (st.endings >> i) & 1;
    return n;
}

bool pr(uint32_t b) { return buttons.pressed & b; }

void draw_tabs(const char *title, int y) {
    black(0, y, SCREEN, 9);
    centered(y + 1, title, 7);
    for(int x = 0; x < SCREEN; x += 4) font::micro(x, y + 8, '-', 2);
}

}  // namespace

int wrap_lines(std::string_view s, int cols) { return wrap(s, cols).count; }

// ================================================================ dialog

void say(txt::Group g) { dlg_start(txt::groups[g].first, txt::groups[g].count); }
void say_line(txt::Id id) { dlg_start(id, 1); }
bool dialog_open() { return dlg_on; }
bool dialog_typing() { return dlg_on && dlg_shown < float(dlg_len()); }

void dialog_press() {
    if(!dlg_on) return;
    if(dialog_typing()) { dlg_shown = float(dlg_len()); return; }
    dlg_index++;
    dlg_shown = 0.0f;
    audio::click();
    if(dlg_index >= dlg_count) dlg_on = false;
}

void dialog_update(float dt) {
    if(!dlg_on || !dialog_typing()) return;
    float before = dlg_shown;
    dlg_shown += type_speed() * dt;
    if(int(dlg_shown) / 3 != int(before) / 3) {
        if(txt::who[dlg_id()] == txt::W_NONE) audio::telex();
        else if(txt::who[dlg_id()] != txt::W_MYSL) audio::type_blip();
    }
}

void dialog_draw() {
    if(!dlg_on) return;
    txt::Id id = dlg_id();
    std::string_view text = T(id);
    int shown = int(dlg_shown);
    uint8_t who = txt::who[id];
    const char *name = speaker_name(who);

    if(!name) {
        // Szary's thoughts: grey words across the top of the night, no box
        Wrapped w = wrap(text, THOUGHT_COLS);
        int h = w.count * font::CELL_H + 3;
        black(0, 0, SCREEN, h);
        block(2, 1, text, THOUGHT_COLS, THOUGHT_LINES, 5, shown);
        if(!dialog_typing() && blink()) font::ch(SCREEN - 6, h - 6, '>', 4);
        return;
    }

    bool telex = who == txt::W_NONE;
    Wrapped w = wrap(text, BOX_COLS);
    int box_h = BOX_LINES * font::CELL_H + 4;
    black(0, 0, SCREEN, box_h + 4);
    char edge = telex ? '=' : '-';
    for(int x = 0; x < SCREEN; x += 4) font::ch(x, box_h - 2, edge, 3);
    font::ch(0, box_h - 2, '+', 4);
    font::ch(SCREEN - 4, box_h - 2, '+', 4);
    int nl = font::length(name);
    black(5, box_h - 1, (nl + 2) * 4, 6);
    font::ch(6, box_h - 2, '[', 4);
    font::text(10, box_h - 2, name, 7);
    font::ch(10 + nl * 4, box_h - 2, ']', 4);

    // long text scrolls: show the last three lines as it types
    int left = shown, first = 0, used = 0, typed_lines = 0;
    for(int i = 0; i < w.count; i++) {
        used += font::length(w.lines[i]) + 1;
        typed_lines = i + 1;
        if(used > shown) break;
    }
    if(typed_lines > BOX_LINES) first = typed_lines - BOX_LINES;
    for(int i = 0; i < first; i++) left -= font::length(w.lines[i]) + 1;
    for(int i = first; i < w.count && i < first + BOX_LINES && left > 0; i++) {
        int n = font::text(3, 1 + (i - first) * font::CELL_H, w.lines[i], telex ? 7 : 6, left);
        left -= n + 1;
    }
    if(!dialog_typing() && blink()) font::ch(SCREEN - 8, box_h - 2, '>', 6);
}

// ================================================================ toasts

void toast(txt::Id label, const char *what) {
    if(ntoasts == 3) {
        toasts[0] = toasts[1];
        toasts[1] = toasts[2];
        ntoasts = 2;
    }
    float start = now_s();
    if(ntoasts > 0 && toasts[ntoasts - 1].until - 2.2f > start) start = toasts[ntoasts - 1].until - 2.2f + 1.2f;
    toasts[ntoasts++] = {label, what, start + 2.4f};
}

// ================================================================ card

void card(txt::Id id) {
    card_on = true;
    card_id = id;
    card_shown = 0.0f;
    card_t = 0.0f;
    audio::telex();
}
bool card_open() { return card_on; }

void card_press() {
    if(!card_on) return;
    int len = font::length(T(card_id));
    if(card_shown < float(len)) { card_shown = float(len); return; }
    card_on = false;
    audio::click();
}

// Cards and endings also type themselves; game.cpp calls this through
// dialog_update's timing.
void card_update(float dt) {
    if(!card_on) return;
    card_t += dt;
    if(card_t > 0.5f) {
        float before = card_shown;
        card_shown += 18.0f * dt;
        if(int(card_shown) != int(before) && int(card_shown) <= font::length(T(card_id))) audio::type_blip();
    }
}

void card_draw() {
    if(!card_on) return;
    black(0, 0, SCREEN, SCREEN);
    std::string_view s = T(card_id);
    int lines = wrap_lines(s, 28);
    int y = 60 - lines * font::CELL_H / 2;
    block(0, y, s, 28, 4, 7, int(card_shown), true);
    // a thin rule of wind-blown dashes
    for(int i = 0; i < 9; i++) {
        int x = 28 + i * 8 + int(sinf(now_s() * 2.0f + float(i)) * 2.0f);
        font::micro(x, y + lines * font::CELL_H + 6, '-', 2);
    }
    if(card_shown >= float(font::length(s)) && blink()) font::ch(SCREEN - 8, SCREEN - 10, '>', 4);
}

// ================================================================ prompt

void prompt(int x, int y, txt::Id label) {
    std::string_view s = T(label);
    int w = text_w(s);
    int lx = x - w / 2;
    if(lx < 1) lx = 1;
    if(lx + w > SCREEN - 1) lx = SCREEN - 1 - w;
    int shade = 4 + int(1.5f + 1.5f * sinf(now_s() * 4.0f));
    black(lx - 1, y, w + 1, font::CELL_H);
    font::text(lx, y, s, shade);
}

// ================================================================ notebook

void notebook_open(Person p) {
    show_to = p;
    tab = T_CLUES;
    first_pick = -1;
    board_request = -1;
    for(int &s : sel) s = 0;
    audio::page();
}

void notebook_open_map() {
    notebook_open(P_NONE);
    tab = T_MAP;
}

bool notebook_update() {
    int n = tab_size(tab);
    if(pr(Button::B) || pr(Button::X)) {
        if(first_pick >= 0) { first_pick = -1; audio::click(); return true; }
        audio::page();
        return false;
    }
    if(pr(Button::DPAD_LEFT)) { tab = (tab + T_COUNT - 1) % T_COUNT; first_pick = -1; audio::page(); return true; }
    if(pr(Button::DPAD_RIGHT)) { tab = (tab + 1) % T_COUNT; first_pick = -1; audio::page(); return true; }
    if(pr(Button::DPAD_UP) && sel[tab] > 0) { sel[tab]--; audio::click(); }
    if(pr(Button::DPAD_DOWN) && sel[tab] < n - 1) { sel[tab]++; audio::click(); }
    if(sel[tab] >= n) sel[tab] = n > 0 ? n - 1 : 0;
    bool y = tab == T_CLUES && pr(Button::Y);
    if(!(pr(Button::A) || y) || n == 0) return true;

    int i = sel[tab];
    switch(tab) {
    case T_CLUES:
        // near a person A shows the clue; Y (or A with a clue picked) combines
        if(show_to != P_NONE && !y && first_pick < 0) {
            story::show(show_to, clue_at(i));
            return false;
        }
        if(first_pick < 0) { first_pick = i; audio::click(); return true; }
        if(first_pick == i) { first_pick = -1; return true; }
        story::combine(clue_at(first_pick), clue_at(i));
        first_pick = -1;
        return false;
    case T_QUESTIONS: {
        txt::Id q[8]; bool c[8]; int8_t b[8];
        story::questions(q, c, b);
        if(b[i] >= 0 && story::board_available(Board(b[i]))) {
            board_request = b[i];
            return false;
        }
        return true;
    }
    case T_MAP: {
        Place p[PL_COUNT];
        story::places(p);
        story::travel(p[i]);
        return false;
    }
    default: return true;
    }
}

void notebook_draw() {
    black(0, 0, SCREEN, SCREEN);
    // ruled paper
    for(int y = 18; y < 72; y += 7)
        for(int x = 0; x < SCREEN; x += 6) font::micro(x, y + 6, '_', 1);

    static const txt::Id names[T_COUNT] = {txt::ui_tab_clues, txt::ui_tab_people, txt::ui_tab_questions,
                                           txt::ui_tab_map};
    // the tab name between arrows, and four marks for the four tabs
    centered(1, T(names[tab]), 7);
    font::ch(2, 1, '<', 4);
    font::ch(SCREEN - 6, 1, '>', 4);
    for(int k = 0; k < T_COUNT; k++) font::micro(56 + k * 5, 9, k == tab ? '-' : '.', k == tab ? 6 : 2);
    for(int x = 0; x < SCREEN; x += 4) font::micro(x, 13, '-', 2);

    int n = tab_size(tab);
    int s = sel[tab];
    constexpr int ROWS = 8, Y0 = 18;
    int first = s - ROWS + 1 > 0 ? s - ROWS + 1 : 0;
    std::string_view detail;
    txt::Id action = txt::COUNT;

    if(n == 0) {
        centered(40, T(txt::ui_nb_empty), 3);
    }
    for(int r = 0; r < ROWS && first + r < n; r++) {
        int i = first + r;
        int y = Y0 + r * 7;
        bool on = i == s;
        int shade = on ? 7 : 4;
        std::string_view label;
        char mark = on ? '>' : ' ';
        switch(tab) {
        case T_CLUES: {
            clue::Id c = clue_at(i);
            label = clue::name[c][st.lang];
            if(c >= clue::w_rider) { font::ch(8, y, '*', on ? 6 : 3); }
            if(i == first_pick) mark = '+';
            if(on) detail = clue::note[c][st.lang];
            break;
        }
        case T_PEOPLE: {
            Person p[5];
            people_list(p);
            txt::Id l[3];
            int k = story::person_lines(p[i], l);
            label = T(l[0]);
            if(on && k > 1) detail = T(l[1]);
            if(on && k > 2) {
                // two notes on one person: show the newest
                detail = T(l[2]);
            }
            break;
        }
        case T_QUESTIONS: {
            txt::Id q[8]; bool c[8]; int8_t b[8];
            story::questions(q, c, b);
            label = T(q[i]);
            if(c[i]) { shade = on ? 4 : 2; font::ch(8, y, 'X', shade); }
            else if(b[i] >= 0 && story::board_available(Board(b[i]))) font::ch(8, y, '#', on ? 7 : 5);
            if(on && b[i] >= 0 && story::board_available(Board(b[i]))) action = txt::ui_nb_board;
            break;
        }
        case T_MAP: {
            Place p[PL_COUNT];
            story::places(p);
            label = T(story::place_name(p[i]));
            static const Scene of[PL_COUNT] = {S_DUNES, S_LIGHTHOUSE, S_BAR, S_PIER, S_OFFICE, S_BUNKER};
            if(of[p[i]] == st.scene) font::ch(8, y, 'o', on ? 7 : 4);
            if(on) action = txt::ui_nb_travel;
            break;
        }
        }
        if(mark != ' ') font::ch(2, y, mark, 7);
        font::text(14, y, label, shade, 28);
    }
    if(first > 0) font::ch(SCREEN - 6, Y0, '^', 3);
    if(first + ROWS < n) font::ch(SCREEN - 6, Y0 + (ROWS - 1) * 7, 'v', 3);

    // details at the bottom
    for(int x = 0; x < SCREEN; x += 4) font::micro(x, 75, '-', 2);
    if(tab == T_QUESTIONS) {
        // the next step, in Szary's words
        detail = T(story::hint());
    }
    if(!detail.empty()) block(2, 80, detail, 31, 4, tab == T_QUESTIONS ? 4 : 6);

    if(tab == T_CLUES && n > 0) {
        if(show_to != P_NONE && first_pick < 0) action = txt::ui_nb_show;
        else action = first_pick >= 0 ? txt::ui_nb_second : txt::ui_nb_combine;
    }
    if(action != txt::COUNT) {
        black(0, 120, SCREEN, 8);
        font::text(2, 121, T(action), 5 + (blink() ? 1 : 0));
    }
}

// ================================================================ board

void board_open(Board b) {
    brd = b;
    for(auto &p : picks) p = 0;
    brow = 0;
    bphase = BP_EDIT;
    bphase_t = 0.0f;
    audio::page();
}

bool board_update() {
    const story::BoardDef &d = story::board(brd);
    if(bphase == BP_RIGHT || bphase == BP_WRONG) {
        bphase_t += DT;
        if(bphase_t > 1.6f || pr(Button::A)) {
            if(bphase == BP_RIGHT) return false;
            bphase = BP_EDIT;
        }
        return true;
    }
    if(bphase == BP_SURE) {
        if(pr(Button::DPAD_UP) || pr(Button::DPAD_DOWN)) { sure_sel = 1 - sure_sel; audio::click(); }
        if(pr(Button::B)) bphase = BP_EDIT;
        if(pr(Button::A)) {
            if(sure_sel == 1) { bphase = BP_EDIT; return true; }
            story::board_submit(brd, picks);
            audio::thud();
            return false;
        }
        return true;
    }
    if(pr(Button::B) || pr(Button::X)) return false;
    int rows = d.blanks + 1;
    if(pr(Button::DPAD_UP)) { brow = (brow + rows - 1) % rows; audio::click(); }
    if(pr(Button::DPAD_DOWN)) { brow = (brow + 1) % rows; audio::click(); }
    if(brow < d.blanks) {
        int n = d.options[brow];
        if(pr(Button::DPAD_LEFT)) { picks[brow] = uint8_t((picks[brow] + n - 1) % n); audio::click(); }
        if(pr(Button::DPAD_RIGHT) || pr(Button::A)) { picks[brow] = uint8_t((picks[brow] + 1) % n); audio::click(); }
    } else if(pr(Button::A)) {
        if(d.final) { bphase = BP_SURE; sure_sel = 1; return true; }
        bool right = story::board_submit(brd, picks);
        bphase = right ? BP_RIGHT : BP_WRONG;
        bphase_t = 0.0f;
        if(right) audio::thud(); else audio::click();
    }
    return true;
}

void board_draw() {
    black(0, 0, SCREEN, SCREEN);
    const story::BoardDef &d = story::board(brd);
    draw_tabs(T(txt::ui_board_title), 0);
    int y = 13;
    for(int i = 0; i < d.blanks; i++) {
        int n = block(2, y, T(d.prompt[i]), 31, 2, 4);
        y += n * font::CELL_H + 1;
        bool on = brow == i && bphase == BP_EDIT;
        std::string_view w = T(d.words[i][picks[i]]);
        int wx = (SCREEN - text_w(w)) / 2;
        if(on) {
            font::ch(wx - 8, y, '<', blink() ? 7 : 4);
            font::ch(wx + text_w(w) + 4, y, '>', blink() ? 7 : 4);
        }
        // the gap is drawn as an underline under the word
        for(int x = wx - 2; x < wx + text_w(w) + 2; x += 2) font::micro(x, y + 7, '_', on ? 4 : 2);
        font::text(wx, y, w, on ? 7 : 5);
        y += font::CELL_H + 6;
    }
    bool on = brow == d.blanks && bphase == BP_EDIT;
    txt::Id btn = d.final ? txt::ui_board_commit : txt::ui_board_check;
    int by = SCREEN - 12;
    if(on) font::ch((SCREEN - text_w(T(btn))) / 2 - 8, by, '>', 7);
    centered(by, T(btn), on ? 7 : 4);

    if(bphase == BP_RIGHT || bphase == BP_WRONG || bphase == BP_SURE) {
        black(8, 50, SCREEN - 16, 30);
        for(int x = 8; x < SCREEN - 8; x += 4) { font::ch(x, 48, '-', 3); font::ch(x, 78, '-', 3); }
        if(bphase == BP_SURE) {
            centered(52, T(txt::ui_board_sure), 7);
            static const txt::Id yn[2] = {txt::ui_board_yes, txt::ui_board_no};
            for(int k = 0; k < 2; k++) {
                int ly = 61 + k * 8;
                centered(ly, T(yn[k]), sure_sel == k ? 7 : 3);
                if(sure_sel == k) font::ch((SCREEN - text_w(T(yn[k]))) / 2 - 8, ly, '>', 7);
            }
        } else {
            centered(60, T(bphase == BP_RIGHT ? txt::ui_board_right : txt::ui_board_wrong), 7);
        }
    }
}

// ================================================================ choice

void choice_open(uint16_t id) {
    ch_id = id;
    ch_sel = 0;
    audio::thud();
}

bool choice_update() {
    if(pr(Button::DPAD_UP) || pr(Button::DPAD_DOWN)) { ch_sel = 1 - ch_sel; audio::click(); }
    if(pr(Button::A)) {
        story::choice(ch_id, ch_sel);
        audio::thud();
        return false;
    }
    return true;
}

void choice_draw() {
    static const txt::Id q[2][3] = {{txt::choice_final_q, txt::choice_final_a, txt::choice_final_b},
                                    {txt::choice_page_q, txt::choice_page_a, txt::choice_page_b}};
    const txt::Id *c = q[ch_id > 1 ? 0 : ch_id];
    black(0, 0, SCREEN, 44);
    for(int x = 0; x < SCREEN; x += 4) font::ch(x, 40, '-', 3);
    centered(3, T(c[0]), 7);
    for(int k = 0; k < 2; k++) {
        int y = 14 + k * 12;
        bool on = ch_sel == k;
        std::string_view s = T(c[1 + k]);
        int n = block(10, y, s, 28, 2, on ? 7 : 3);
        (void)n;
        if(on) font::ch(3, y, '>', blink() ? 7 : 5);
    }
}

// ================================================================ pause menu

void menu_open() {
    menu_sel = 0;
    quit_to_title = false;
    audio::click();
}

bool menu_update() {
    constexpr int N = 5;
    if(pr(Button::MENU) || pr(Button::B)) return false;
    if(pr(Button::DPAD_UP)) { menu_sel = (menu_sel + N - 1) % N; audio::click(); }
    if(pr(Button::DPAD_DOWN)) { menu_sel = (menu_sel + 1) % N; audio::click(); }
    bool change = pr(Button::A) || pr(Button::DPAD_LEFT) || pr(Button::DPAD_RIGHT);
    if(!change) return true;
    audio::click();
    switch(menu_sel) {
    case 0: if(pr(Button::A)) return false; break;
    case 1: st.lang = uint8_t(1 - st.lang); save_settings(); break;
    case 2:
        st.speed = uint8_t((st.speed + (pr(Button::DPAD_LEFT) ? 2 : 1)) % 3);
        save_settings();
        break;
    case 3: st.show_fps = !st.show_fps; save_settings(); break;
    case 4:
        if(pr(Button::A)) { quit_to_title = true; return false; }
        break;
    }
    return true;
}

void menu_draw() {
    black(16, 24, SCREEN - 32, 74);
    for(int x = 16; x < SCREEN - 16; x += 4) { font::ch(x, 22, '-', 3); font::ch(x, 96, '-', 3); }
    centered(28, T(txt::ui_pause), 7);
    char buf[64];
    static const txt::Id speeds[3] = {txt::ui_speed0, txt::ui_speed1, txt::ui_speed2};
    for(int i = 0; i < 5; i++) {
        int y = 42 + i * 10;
        bool on = menu_sel == i;
        const char *s = "";
        switch(i) {
        case 0: s = T(txt::ui_resume); break;
        case 1: s = T(txt::ui_language); break;
        case 2: snprintf(buf, sizeof(buf), "%s %s", T(txt::ui_textspeed), T(speeds[st.speed % 3])); s = buf; break;
        case 3: snprintf(buf, sizeof(buf), "%s %s", T(txt::ui_fps), T(st.show_fps ? txt::ui_on : txt::ui_off)); s = buf; break;
        case 4: s = T(txt::ui_quit); break;
        }
        centered(y, s, on ? 7 : 4);
        if(on) font::ch((SCREEN - text_w(s)) / 2 - 8, y, '>', 7);
    }
}

// ================================================================ title

void title_open() {
    title_sel = 0;
    title_chapters = false;
    title_t = 0.0f;
    audio::motif(0);
}

bool title_update() {
    title_t += DT;
    TitleItem items[4];
    int n = title_items(items);
    if(title_sel >= n) title_sel = 0;
    if(title_chapters) {
        if(pr(Button::DPAD_LEFT) && title_chapter > 1) { title_chapter--; audio::click(); }
        if(pr(Button::DPAD_RIGHT) && title_chapter < 5) { title_chapter++; audio::click(); }
        if(pr(Button::B)) { title_chapters = false; audio::click(); }
        if(pr(Button::A)) {
            story::new_game();
            // replace the prologue with the chosen chapter
            clear_script();
            story::grant_until(title_chapter);
            story::start_chapter(title_chapter, true);
            return false;
        }
        return true;
    }
    if(pr(Button::DPAD_UP)) { title_sel = (title_sel + n - 1) % n; audio::click(); }
    if(pr(Button::DPAD_DOWN)) { title_sel = (title_sel + 1) % n; audio::click(); }
    bool a = pr(Button::A);
    switch(items[title_sel]) {
    case TI_CONTINUE: if(a && load_game()) return false; break;
    case TI_NEW: if(a) { story::new_game(); return false; } break;
    case TI_CHAPTERS: if(a) { title_chapters = true; title_chapter = 1; audio::click(); } break;
    case TI_LANG:
        if(a || pr(Button::DPAD_LEFT) || pr(Button::DPAD_RIGHT)) {
            st.lang = uint8_t(1 - st.lang);
            save_settings();
            audio::click();
        }
        break;
    }
    return true;
}

void title_draw() {
    // SZARY in big letters made of blocks, a little shaken by the wind
    static const char *letters[5][5] = {
        {"###", "#..", "###", "..#", "###"}, {"###", "..#", ".#.", "#..", "###"},
        {".#.", "#.#", "###", "#.#", "#.#"}, {"##.", "#.#", "##.", "#.#", "#.#"},
        {"#.#", "#.#", ".#.", ".#.", ".#."},
    };
    int x0 = 17, y0 = 14;
    for(int l = 0; l < 5; l++)
        for(int r = 0; r < 5; r++)
            for(int c = 0; c < 3; c++) {
                if(letters[l][r][c] != '#') continue;
                float w = world::wind_at(float(l * 20 + c * 5) + 300.0f);
                int jx = w > 0.9f && r < 2 ? 1 : 0;
                int x = x0 + l * 20 + c * 5 + jx, y = y0 + r * 6;
                font::ch(x, y, SOLID, 6 + (r == 0));
            }
    // a line of grass-like marks under it
    for(int i = 0; i < 24; i++) font::micro(18 + i * 4, 46, i % 3 ? '-' : '~', 3);

    TitleItem items[4];
    int n = title_items(items);
    int y = 56;
    char buf[48];
    for(int i = 0; i < n; i++) {
        bool on = i == title_sel;
        const char *s = "";
        switch(items[i]) {
        case TI_CONTINUE: s = T(txt::ui_continue); break;
        case TI_NEW: s = T(txt::ui_newgame); break;
        case TI_CHAPTERS:
            if(title_chapters) { snprintf(buf, sizeof(buf), "< %s %d >", T(txt::ui_chapters), title_chapter); s = buf; }
            else s = T(txt::ui_chapters);
            break;
        case TI_LANG: s = T(txt::ui_language); break;
        }
        int w = text_w(s);
        black((SCREEN - w) / 2 - 2, y - 1, w + 4, 8);
        centered(y, s, on ? 7 : 4);
        if(on && !title_chapters) font::ch((SCREEN - w) / 2 - 8, y, '>', blink() ? 7 : 5);
        y += 9;
    }
    if(st.endings) {
        snprintf(buf, sizeof(buf), "%s %d/4", T(txt::ui_endings), endings_seen());
        black(0, 112, SCREEN, 8);
        centered(113, buf, 3);
    }
    black(0, 121, SCREEN, 7);
    centered(121, T(txt::ui_copyright), 2);
}

// ================================================================ endings

void ending_open(Ending e) {
    end_e = e;
    end_line = 0;
    end_shown = 0.0f;
    end_final = false;
    audio::motif(1);
}

bool ending_update() {
    txt::Group g = end_group(end_e);
    int count = txt::groups[g].count;
    txt::Id id = txt::Id(txt::groups[g].first + end_line);
    if(!end_final) {
        float before = end_shown;
        end_shown += type_speed() * 0.8f * DT;
        if(int(before) / 3 != int(end_shown) / 3 && int(end_shown) <= font::length(T(id))) audio::type_blip();
    }
    if(!pr(Button::A)) return true;
    if(end_final) return false;
    if(end_shown < float(font::length(T(id)))) { end_shown = 1e6f; return true; }
    audio::click();
    end_line++;
    end_shown = 0.0f;
    if(end_line >= count) end_final = true;
    return true;
}

void ending_draw() {
    black(0, 0, SCREEN, SCREEN);
    // a dark sea line across the middle, slow
    for(int x = -int(fmodf(now_s() * 3.0f, 8.0f)); x < SCREEN; x += 8) font::micro(x, 100, '~', 2);
    if(end_e == E_DAWN) for(int x = 0; x < SCREEN; x += 5) font::micro(x, 96, '.', 3);
    if(end_final) {
        static const txt::Id names[E_COUNT] = {txt::title_dawn, txt::title_ash, txt::title_wind, txt::title_bank};
        centered(40, T(txt::end_fin), 4);
        centered(52, T(names[end_e]), 7);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %d/4", T(txt::ui_endings), endings_seen());
        centered(70, buf, 3);
        if(blink()) font::ch(SCREEN - 8, SCREEN - 10, '>', 4);
        return;
    }
    txt::Group g = end_group(end_e);
    txt::Id id = txt::Id(txt::groups[g].first + end_line);
    std::string_view s = T(id);
    int lines = wrap_lines(s, 28);
    block(0, 52 - lines * font::CELL_H / 2, s, 28, 5, 6, int(end_shown), true);
    if(end_shown >= float(font::length(s)) && blink()) font::ch(SCREEN - 8, SCREEN - 10, '>', 4);
}

// ================================================================ overlay

void overlay_draw() {
    card_draw();
    float now = now_s();
    while(ntoasts > 0 && toasts[0].until < now) {
        for(int i = 1; i < ntoasts; i++) toasts[i - 1] = toasts[i];
        ntoasts--;
    }
    if(ntoasts > 0 && toasts[0].until - 2.4f <= now && (mode() == M_PLAY || mode() == M_CHOICE)) {
        const Toast &tt = toasts[0];
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %s", T(tt.label), tt.what);
        int w = text_w(buf);
        if(w > SCREEN - 4) w = SCREEN - 4;
        int x = (SCREEN - w) / 2;
        black(x - 3, SCREEN - 11, w + 6, 11);
        for(int k = x - 3; k < x + w + 3; k += 4) font::micro(k, SCREEN - 12, '-', 4);
        font::text(x, SCREEN - 9, buf, 7, 31);
    }
    if(st.show_fps) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d FPS %lu.%luMS", int(fps_value_f + 0.5f), (unsigned long)(render_us / 1000),
                 (unsigned long)(render_us / 100 % 10));
        int w = text_w(buf);
        black(SCREEN - w - 2, SCREEN - 8, w + 2, 8);
        font::text(SCREEN - w - 1, SCREEN - 7, buf, 7);
    }
}


}  // namespace ui
}  // namespace sz
