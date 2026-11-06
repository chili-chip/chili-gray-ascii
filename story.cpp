// What happens in Szary: places to look at, people, clues, combining,
// showing, boards, chapters and endings. Words are in story/teksty.csv.

#include "szary.hpp"

namespace sz {
namespace story {

using namespace txt;

namespace {

// ------------------------------------------------------------ hotspots

enum H : uint8_t {
    H_ST_WINDOW, H_ST_DESK, H_ST_TELEX, H_ST_DOOR,
    H_DU_CAR, H_DU_LHDOOR, H_DU_BIKE, H_DU_TRAIL, H_DU_TRAILEND, H_DU_TRAIL2, H_DU_TRAIL3, H_DU_TRAIL4,
    H_DU_HALINA, H_DU_BARREL, H_DU_BUNKER,
    H_LH_DOOR, H_LH_LOG, H_LH_CAP, H_LH_LENS, H_LH_SWITCH,
    H_BAR_DOOR, H_BAR_RYSIEK, H_BAR_RADIO, H_BAR_PHOTO, H_BAR_KUBA, H_BAR_BOOTS, H_BAR_WINDOW,
    H_PI_OFFICE, H_PI_BOAT, H_PI_END,
    H_OF_DOOR, H_OF_JAS, H_OF_TIMETABLE, H_OF_CHART, H_OF_ARCHIVE,
    H_BU_DOOR, H_BU_BOGDAN, H_BU_POCKET, H_BU_SHELF,
    H_COUNT
};

const Hotspot spots[H_COUNT] = {
    {S_STATION, 18, HK_LOOK, H_ST_WINDOW},  {S_STATION, 44, HK_LOOK, H_ST_DESK},
    {S_STATION, 78, HK_LOOK, H_ST_TELEX},   {S_STATION, 114, HK_EXIT, H_ST_DOOR},
    {S_DUNES, 44, HK_LOOK, H_DU_CAR},       {S_DUNES, 150, HK_ENTER, H_DU_LHDOOR},
    {S_DUNES, 176, HK_LOOK, H_DU_BIKE},     {S_DUNES, 272, HK_LOOK, H_DU_TRAIL},
    {S_DUNES, 334, HK_LOOK, H_DU_TRAILEND}, {S_DUNES, 392, HK_LOOK, H_DU_TRAIL2},
    {S_DUNES, 450, HK_LOOK, H_DU_TRAIL3},   {S_DUNES, 604, HK_LOOK, H_DU_TRAIL4},
    {S_DUNES, 494, HK_TALK, H_DU_HALINA},   {S_DUNES, 518, HK_LOOK, H_DU_BARREL},
    {S_DUNES, 662, HK_LOOK, H_DU_BUNKER},
    {S_LIGHTHOUSE, 10, HK_EXIT, H_LH_DOOR}, {S_LIGHTHOUSE, 32, HK_LOOK, H_LH_LOG},
    {S_LIGHTHOUSE, 58, HK_LOOK, H_LH_CAP},  {S_LIGHTHOUSE, 78, HK_LOOK, H_LH_LENS},
    {S_LIGHTHOUSE, 106, HK_LOOK, H_LH_SWITCH},
    {S_BAR, 8, HK_EXIT, H_BAR_DOOR},        {S_BAR, 30, HK_TALK, H_BAR_RYSIEK},
    {S_BAR, 56, HK_LOOK, H_BAR_RADIO},      {S_BAR, 82, HK_LOOK, H_BAR_PHOTO},
    {S_BAR, 108, HK_TALK, H_BAR_KUBA},      {S_BAR, 126, HK_LOOK, H_BAR_BOOTS},
    {S_BAR, 150, HK_LOOK, H_BAR_WINDOW},
    {S_PIER, 40, HK_ENTER, H_PI_OFFICE},    {S_PIER, 292, HK_LOOK, H_PI_BOAT},
    {S_PIER, 404, HK_LOOK, H_PI_END},
    {S_OFFICE, 10, HK_EXIT, H_OF_DOOR},     {S_OFFICE, 44, HK_TALK, H_OF_JAS},
    {S_OFFICE, 70, HK_LOOK, H_OF_TIMETABLE},{S_OFFICE, 94, HK_LOOK, H_OF_CHART},
    {S_OFFICE, 116, HK_LOOK, H_OF_ARCHIVE},
    {S_BUNKER, 10, HK_EXIT, H_BU_DOOR},     {S_BUNKER, 56, HK_TALK, H_BU_BOGDAN},
    {S_BUNKER, 74, HK_LOOK, H_BU_POCKET},   {S_BUNKER, 104, HK_LOOK, H_BU_SHELF},
};

// ------------------------------------------------------------ events

enum Ev : uint16_t {
    EV_TELEX_DONE, EV_CH1_DONE, EV_BOARD1_OK, EV_CH3_DONE, EV_BOARD2_OK,
    EV_FINAL_BAD, EV_START_CH2, EV_START_CH3, EV_START_CH4, EV_START_CH5, EV_RELIGHT_DONE,
};
enum Choice : uint16_t { CH_FINAL, CH_PAGE };

void say(Group g) { push(A_SAY, g); }
void line(Id id) { push(A_LINE, id); }
void give(clue::Id c) { push(A_CLUE, c); }
void mark(Flag f) { push(A_FLAG, f); }

// ------------------------------------------------------------ combining

struct Combo {
    clue::Id a, b, result;
    Group said;  // spoken when the combination works (or GROUP_COUNT)
};
const Combo combos[] = {
    {clue::bike, clue::nosand, clue::w_rider, GROUP_COUNT},
    {clue::h_evening, clue::h_bike, clue::w_saw, GROUP_COUNT},
    {clue::cap, clue::trail, clue::w_led, GROUP_COUNT},
    {clue::r_time, clue::k_time, clue::w_lie, G_ch2_lie},
    {clue::boots, clue::boatlight, clue::w_kboat, GROUP_COUNT},
    {clue::photo, clue::woman, clue::w_kowal, GROUP_COUNT},
    {clue::aurora, clue::h_asked, clue::w_waits, GROUP_COUNT},
    {clue::paper, clue::photo, clue::w_mewa, GROUP_COUNT},
};

// A thought when the second clue of a pair arrives: plain in chapter 1,
// vaguer later, none from chapter 4 on.
struct Nudge {
    clue::Id a, b;
    txt::Id said;
    int8_t last_chapter;
};
const Nudge nudges[] = {
    {clue::bike, clue::nosand, think_bike, 3},
    {clue::h_evening, clue::h_bike, think_saw, 3},
    {clue::cap, clue::trail, think_cap, 3},
    {clue::r_time, clue::k_time, think_lie, 3},
    {clue::boots, clue::boatlight, think_boat, 3},
    {clue::photo, clue::woman, think_kowal, 3},
    {clue::aurora, clue::h_asked, think_waits, 3},
    {clue::paper, clue::photo, think_mewa, 3},
};

// ------------------------------------------------------------ showing

struct Show {
    Person p;
    clue::Id c;
    Group said;
    clue::Id gives;  // clue::COUNT = nothing
};
const Show shows[] = {
    {P_HALINA, clue::w_saw, G_halina_show_saw, clue::h_930},
    {P_HALINA, clue::w_kowal, G_halina_show_k, clue::anniv},
    {P_HALINA, clue::woman, G_halina_show_k, clue::anniv},
    {P_HALINA, clue::aurora, G_halina_show_aurora, clue::COUNT},
    {P_HALINA, clue::w_waits, G_halina_show_aurora, clue::COUNT},
    {P_KUBA, clue::boots, G_kuba_show_boots, clue::COUNT},
    {P_KUBA, clue::r_time, G_kuba_show_time, clue::k_time},
    {P_KUBA, clue::log, G_kuba_show_log, clue::woman},
    {P_RYSIEK, clue::boatlight, G_rysiek_show_boat, clue::COUNT},
    {P_RYSIEK, clue::photo, G_rysiek_show_photo, clue::COUNT},
    {P_JASINSKI, clue::paper, G_jas_show_paper, clue::captain},
    {P_JASINSKI, clue::report, G_jas_show_report, clue::COUNT},
    {P_JASINSKI, clue::barrel, G_jas_show_barrel, clue::COUNT},
};

// ------------------------------------------------------------ boards

const BoardDef boards[B_COUNT] = {
    {2, {board1_s1, board1_s2, board1_s1}, {4, 3, 0},
     {{word_kuba, word_rysiek, word_halina, word_jasinski}, {word_debt, word_boat, word_bike, word_bike}, {}},
     {1, 1, 0}, false},
    {3, {board2_s1, board2_s2, board2_s3}, {3, 3, 3},
     {{word_boatlight, word_star, word_lighthouse, word_lighthouse},
      {word_harbour, word_sandbank, word_fairway, word_fairway},
      {word_warmth, word_signal, word_false, word_false}},
     {2, 1, 2}, false},
    {3, {board3_s1, board3_s2, board3_s3}, {4, 4, 4},
     {{word_kuba, word_rysiek, word_halina, word_bogdan},
      {word_kuba, word_rysiek, word_halina, word_bogdan},
      {word_money, word_smuggling, word_mewa, word_shame}},
     {2, 2, 2}, true},
};

bool board_flag(Board b) {
    return b == B_LIE ? flag(F_BOARD1) : b == B_FIRE ? flag(F_BOARD2) : flag(F_BOARD3);
}

}  // namespace

// ================================================================ chapters

void new_game() {
    uint8_t lang = st.lang, speed = st.speed, endings = st.endings;
    bool fps = st.show_fps;
    st = State();
    st.lang = lang; st.speed = speed; st.endings = endings; st.show_fps = fps;
    start_chapter(0, true);
}

void start_chapter(int ch, bool with_card) {
    static const Id cards[6] = {card_prolog, card_ch1, card_ch2, card_ch3, card_ch4, card_ch5};
    if(with_card) push(A_CARD, cards[ch]);
    push(A_CHAPTER, uint16_t(ch));
    switch(ch) {
    case 0: push(A_SCENE, S_STATION, 64); say(G_prolog_wake); break;
    case 1: push(A_SCENE, S_DUNES, 74); mark(F_CH1_STARTED); say(G_ch1_arrive); break;
    case 2: push(A_SCENE, S_BAR, 22); mark(F_CH2_STARTED); say(G_ch2_arrive); break;
    case 3: push(A_SCENE, S_PIER, 80); mark(F_CH3_STARTED); say(G_ch3_arrive); break;
    case 4: push(A_SCENE, S_DUNES, 346); mark(F_CH4_STARTED); say(G_ch4_arrive); break;
    case 5: push(A_SCENE, S_DUNES, 462); mark(F_CH5_STARTED); say(G_ch5_arrive); break;
    }
    push(A_SAVE);
}

void grant_until(int ch) {
    auto c = [](std::initializer_list<clue::Id> l) {
        for(auto x : l)
            if(!has(x)) { st.clues |= uint64_t(1) << x; st.order[st.nclues++] = x; }
    };
    auto f = [](std::initializer_list<Flag> l) { for(auto x : l) set_flag(x); };
    if(ch >= 1) f({F_TELEX});
    if(ch >= 2) {
        c({clue::bike, clue::lamp, clue::fuse_gone, clue::door, clue::log, clue::cap, clue::trail,
           clue::h_evening, clue::h_bike, clue::nosand, clue::barrel, clue::w_rider});
        f({F_CH1_STARTED, F_LH_ENTERED, F_SWITCH, F_HALINA1, F_TRAIL_END, F_CH1_DONE});
    }
    if(ch >= 3) {
        c({clue::r_time, clue::debt, clue::k_1030, clue::key, clue::k_bike, clue::boots, clue::k_time,
           clue::photo, clue::boatlight, clue::w_lie, clue::smuggle});
        f({F_CH2_STARTED, F_RYSIEK1, F_KUBA1, F_BOARD1, F_CH2_DONE});
    }
    if(ch >= 4) {
        c({clue::aurora, clue::h_asked, clue::legend, clue::sandbank, clue::paper, clue::report});
        f({F_CH3_STARTED, F_JAS1, F_ARCHIVE, F_CH3_DONE});
    }
    if(ch >= 5) {
        c({clue::padlock, clue::crowbar, clue::b_fire, clue::b_she, clue::page, clue::fuse});
        f({F_CH4_STARTED, F_TRAIL2, F_TRAIL3, F_TRAIL4, F_BUNKER_OPEN, F_BOGDAN1, F_FIRE_THOUGHT, F_BOARD2,
           F_CH4_DONE});
    }
}

// Called after every new clue or flag: is the chapter finished?
// is there a pair in the notebook that is waiting to be combined?
bool combo_waiting() {
    for(const auto &k : combos)
        if(has(k.a) && has(k.b) && !has(k.result)) return true;
    return false;
}

void after_clue(clue::Id c) {
    if(c != clue::COUNT) {
        for(const auto &n : nudges) {
            if(st.chapter > n.last_chapter) continue;
            clue::Id other = n.a == c ? n.b : n.b == c ? n.a : clue::COUNT;
            if(other == clue::COUNT || !has(other)) continue;
            bool done = false;
            for(const auto &k : combos)
                if(((k.a == n.a && k.b == n.b) || (k.a == n.b && k.b == n.a)) && has(k.result)) done = true;
            if(!done) { line(n.said); break; }
        }
    }
    // showing is the least obvious thing in the game: the first time it is
    // needed, Szary says so
    if(st.chapter == 2 && !flag(F_SHOW_THOUGHT) && has(clue::r_time) && flag(F_KUBA1) && !has(clue::k_time)) {
        set_flag(F_SHOW_THOUGHT);
        line(think_show);
    }
    switch(st.chapter) {
    case 1:
        if(!flag(F_CH1_DONE) && has(clue::lamp) && has(clue::log) && has(clue::bike) && flag(F_HALINA1)) {
            set_flag(F_CH1_DONE);
            push(A_EVENT, EV_CH1_DONE);
        }
        break;
    case 3:
        if(!flag(F_CH3_DONE) && flag(F_JAS1) && has(clue::paper) && has(clue::sandbank) && has(clue::aurora)) {
            set_flag(F_CH3_DONE);
            push(A_EVENT, EV_CH3_DONE);
        }
        break;
    case 4:
        if(!flag(F_FIRE_THOUGHT) && has(clue::b_fire)) {
            set_flag(F_FIRE_THOUGHT);
            say(G_ch4_fire);
        }
        break;
    }
}

void event(uint16_t id) {
    switch(id) {
    case EV_TELEX_DONE:
        say(G_prolog_read);
        break;
    case EV_CH1_DONE:
        say(G_ch1_done);
        push(A_SAVE);
        break;
    case EV_BOARD1_OK:
        say(G_rysiek_confess);
        give(clue::smuggle);
        say(G_ch2_done);
        mark(F_CH2_DONE);
        push(A_SAVE);
        break;
    case EV_CH3_DONE:
        say(G_ch3_done);
        push(A_SAVE);
        break;
    case EV_BOARD2_OK:
        say(G_ch4_done);
        mark(F_CH4_DONE);
        start_chapter(5, true);
        break;
    case EV_FINAL_BAD:
        push(A_ENDING, E_BANK);
        break;
    case EV_START_CH2: start_chapter(2, true); break;
    case EV_START_CH3: start_chapter(3, true); break;
    case EV_START_CH4: start_chapter(4, true); break;
    case EV_START_CH5: start_chapter(5, true); break;
    case EV_RELIGHT_DONE:
        push(A_CHOICE, CH_PAGE);
        break;
    }
}

void choice(uint16_t id, int option) {
    if(id == CH_FINAL) {
        if(option == 0) {
            push(A_SCENE, S_DUNES, -170.0f);  // by the lighthouse, looking at it
            push(A_BEAM, 0, 0.0f);
            say(G_relight);
            push(A_WAIT, 0, 3.0f);
            mark(F_RELIT);
            push(A_EVENT, EV_RELIGHT_DONE);
        } else {
            push(A_ENDING, E_WIND);
        }
    } else if(id == CH_PAGE) {
        push(A_ENDING, option == 0 ? E_DAWN : E_ASH);
    }
}

// ================================================================ hotspots

int hotspot_count() { return H_COUNT; }
const Hotspot &hotspot(int i) { return spots[i]; }

Person hotspot_person(int i) {
    switch(i) {
    case H_DU_HALINA: return P_HALINA;
    case H_BAR_RYSIEK: return P_RYSIEK;
    case H_BAR_KUBA: return P_KUBA;
    case H_OF_JAS: return P_JASINSKI;
    case H_BU_BOGDAN: return P_BOGDAN;
    default: return P_NONE;
    }
}

bool hotspot_active(int i) {
    int ch = st.chapter;
    switch(i) {
    case H_DU_TRAILEND: return ch < 4 && has(clue::trail);
    case H_DU_TRAIL2: return ch == 4;
    case H_DU_TRAIL3: return ch == 4 && flag(F_TRAIL2);
    case H_DU_TRAIL4: return ch == 4 && flag(F_TRAIL3);
    case H_BU_POCKET: return flag(F_BOGDAN1) && !has(clue::page);
    case H_BU_SHELF: return !has(clue::fuse);
    case H_LH_LENS: return true;
    default: return true;
    }
}

void use_hotspot(int i) {
    int ch = st.chapter;
    switch(i) {
    // ---------------------------------------------------- station
    case H_ST_WINDOW: say(G_prolog_window); break;
    case H_ST_DESK: say(G_prolog_desk); break;
    case H_ST_TELEX:
        say(G_telex);
        if(!flag(F_TELEX)) { mark(F_TELEX); push(A_EVENT, EV_TELEX_DONE); }
        break;
    case H_ST_DOOR:
        if(!flag(F_TELEX)) say(G_prolog_door);
        else if(ch == 0) start_chapter(1, true);
        break;

    // ---------------------------------------------------- dunes
    case H_DU_CAR:
        if(ch >= 4 && !has(clue::crowbar)) { say(G_car_take); give(clue::crowbar); }
        else say(G_car);
        break;
    case H_DU_LHDOOR:
        push(A_SCENE, S_LIGHTHOUSE, 20);
        if(!flag(F_LH_ENTERED)) { mark(F_LH_ENTERED); say(G_lh_door); give(clue::door); }
        break;
    case H_DU_BIKE:
        say(G_bike);
        give(clue::bike);
        break;
    case H_DU_TRAIL:
        if(!st.torch) say(G_trail_dark);
        else { say(G_trail); give(clue::trail); }
        break;
    case H_DU_TRAILEND:
        say(G_trail_end);
        mark(F_TRAIL_END);
        break;
    case H_DU_TRAIL2:
    case H_DU_TRAIL3:
    case H_DU_TRAIL4: {
        if(!st.torch) { say(G_trail_dark); break; }
        static const Group g[3] = {G_trail2, G_trail3, G_trail4};
        static const Flag f[3] = {F_TRAIL2, F_TRAIL3, F_TRAIL4};
        int k = i - H_DU_TRAIL2;
        say(g[k]);
        mark(f[k]);
        break;
    }
    case H_DU_HALINA:
        if(ch == 5) {
            if(!flag(F_BOARD3)) say(G_final_not_ready);
            else if(!has(clue::fuse) || !has(clue::page)) say(G_final_missing);  // the choices need both
            else {
                say(G_halina_final);
                push(A_CHOICE, CH_FINAL);
            }
        } else if(!flag(F_HALINA1)) {
            say(G_halina1);
            give(clue::h_evening);
            give(clue::h_bike);
            give(clue::nosand);
            mark(F_HALINA1);
        } else if(ch == 4) say(G_halina_storm);
        else say(G_halina_again);
        break;
    case H_DU_BARREL:
        if(!flag(F_HALINA1)) say(G_barrel_dark);
        else { say(G_barrel); give(clue::barrel); }
        break;
    case H_DU_BUNKER:
        if(flag(F_BUNKER_OPEN)) push(A_SCENE, S_BUNKER, 24);
        else if(ch < 4 || !flag(F_TRAIL4)) say(G_bunker_far);
        else if(!has(clue::padlock)) { say(G_bunker_lock); give(clue::padlock); }
        else if(!has(clue::crowbar)) say(G_bunker_nocrow);
        else {
            say(G_bunker_open);
            mark(F_BUNKER_OPEN);
            push(A_SCENE, S_BUNKER, 24);
        }
        break;

    // ---------------------------------------------------- lighthouse
    case H_LH_DOOR: push(A_SCENE, S_DUNES, 150); break;
    case H_LH_LOG: say(G_lh_log); give(clue::log); break;
    case H_LH_CAP: say(G_lh_cap); give(clue::cap); break;
    case H_LH_LENS: say(flag(F_RELIT) ? G_lh_relit : G_lh_lamp); break;
    case H_LH_SWITCH:
        if(!flag(F_SWITCH)) {
            say(G_lh_switch);
            mark(F_SWITCH);
            give(clue::lamp);
            give(clue::fuse_gone);
        } else {
            line(lh_switch_1);
        }
        break;

    // ---------------------------------------------------- bar
    case H_BAR_DOOR: push(A_EVENT, 0xffff); break;  // the game opens the map
    case H_BAR_RYSIEK:
        if(!flag(F_RYSIEK1)) {
            say(G_rysiek1);
            give(clue::r_time);
            give(clue::debt);
            mark(F_RYSIEK1);
        } else say(G_rysiek_again);
        break;
    case H_BAR_KUBA:
        if(!flag(F_KUBA1)) {
            say(G_kuba1);
            give(clue::k_1030);
            give(clue::key);
            give(clue::k_bike);
            mark(F_KUBA1);
        } else say(G_kuba_again);
        break;
    case H_BAR_BOOTS: say(G_kuba_boots); give(clue::boots); break;
    case H_BAR_RADIO: say(G_radio); mark(F_RADIO_BAR); break;
    case H_BAR_PHOTO:
        if(!has(clue::photo)) { say(G_photo); give(clue::photo); }
        else line(photo_1);
        break;
    case H_BAR_WINDOW: say(G_window); give(clue::boatlight); break;

    // ---------------------------------------------------- pier and office
    case H_PI_OFFICE: push(A_SCENE, S_OFFICE, 22); break;
    case H_PI_BOAT: say(G_boat); break;
    case H_PI_END: say(G_pier_end); break;
    case H_OF_DOOR: push(A_SCENE, S_PIER, 40); break;
    case H_OF_JAS:
        if(!flag(F_JAS1)) {
            say(G_jas1);
            give(clue::aurora);
            give(clue::h_asked);
            give(clue::legend);
            mark(F_JAS1);
        } else say(G_jas_again);
        break;
    case H_OF_TIMETABLE: say(G_timetable); give(clue::aurora); break;
    case H_OF_CHART: say(G_chart); give(clue::sandbank); break;
    case H_OF_ARCHIVE:
        say(G_archive);
        give(clue::paper);
        give(clue::report);
        mark(F_ARCHIVE);
        break;

    // ---------------------------------------------------- bunker
    case H_BU_DOOR: push(A_SCENE, S_DUNES, 662); break;
    case H_BU_BOGDAN:
        if(!flag(F_BOGDAN1)) {
            say(G_bogdan1);
            give(clue::b_fire);
            give(clue::b_she);
            mark(F_BOGDAN1);
        } else say(G_bogdan_again);
        break;
    case H_BU_POCKET: say(G_pocket); give(clue::page); break;
    case H_BU_SHELF: say(G_shelf); give(clue::fuse); break;
    }
}

// ================================================================ notebook

void combine(clue::Id a, clue::Id b) {
    for(const auto &c : combos) {
        if((c.a == a && c.b == b) || (c.a == b && c.b == a)) {
            if(has(c.result)) { line(combine_bad_2); return; }
            if(c.said != GROUP_COUNT) say(c.said);
            give(c.result);
            return;
        }
    }
    if((a == clue::legend && b == clue::barrel) || (a == clue::barrel && b == clue::legend)) {
        if(st.chapter < 4) say(G_combine_legend_early);
        else say(G_ch4_fire);
        return;
    }
    line((a + b) % 2 ? combine_bad_1 : combine_bad_2);
}

void show(Person p, clue::Id c) {
    for(const auto &s : shows) {
        if(s.p == p && s.c == c) {
            say(s.said);
            if(s.gives != clue::COUNT) give(s.gives);
            if(p == P_RYSIEK && c == clue::photo) mark(F_PHOTO);
            return;
        }
    }
    static const Group fallback[P_COUNT] = {G_show_none, G_halina_show, G_kuba_show, G_rysiek_show, G_jas_show,
                                            G_bogdan_show};
    say(fallback[p]);
}

bool clue_spent(clue::Id c) {
    // a clue is spent when everything it can still do is done; clues that
    // were never part of a combination or a useful show stay bright, so
    // the notebook does not give away the red herrings
    bool used = false;
    for(const auto &k : combos) {
        if(k.a != c && k.b != c) continue;
        used = true;
        if(!has(k.result)) return false;
    }
    for(const auto &k : shows) {
        if(k.c != c || k.gives == clue::COUNT) continue;
        used = true;
        if(!has(k.gives)) return false;
    }
    return used;
}

bool met(Person p) {
    switch(p) {
    case P_HALINA: return flag(F_HALINA1);
    case P_KUBA: return flag(F_KUBA1);
    case P_RYSIEK: return flag(F_RYSIEK1);
    case P_JASINSKI: return flag(F_JAS1);
    case P_BOGDAN: return st.chapter >= 1;
    default: return false;
    }
}

int person_lines(Person p, Id out[3]) {
    int n = 0;
    switch(p) {
    case P_HALINA:
        out[n++] = person_halina;
        out[n++] = person_halina_1;
        if(flag(F_PHOTO) || has(clue::w_mewa) || has(clue::anniv)) out[n++] = person_halina_2;
        break;
    case P_KUBA: out[n++] = person_kuba; out[n++] = person_kuba_1; break;
    case P_RYSIEK: out[n++] = person_rysiek; out[n++] = person_rysiek_1; break;
    case P_JASINSKI: out[n++] = person_jasinski; out[n++] = person_jasinski_1; break;
    case P_BOGDAN:
        out[n++] = person_bogdan;
        out[n++] = person_bogdan_1;
        if(flag(F_BOGDAN1)) out[n++] = person_bogdan_2;
        break;
    default: break;
    }
    return n;
}

int questions(Id out[8], bool closed[8], int8_t brd[8]) {
    int n = 0;
    auto q = [&](Id id, bool done, int8_t b) { out[n] = id; closed[n] = done; brd[n] = b; n++; };
    if(st.chapter >= 5) q(q_final, flag(F_BOARD3), B_FINAL);
    if(has(clue::b_fire)) q(q_fire, flag(F_BOARD2), B_FIRE);
    if(has(clue::w_waits)) q(q_aurora, has(clue::w_mewa) || has(clue::b_fire), -1);
    if(has(clue::w_lie)) q(q_lie, flag(F_BOARD1), B_LIE);
    if(has(clue::log)) q(q_k, has(clue::woman) || has(clue::anniv), -1);
    if(has(clue::w_rider)) q(q_bike, flag(F_BOARD3), -1);
    if(st.chapter >= 1) q(q_bogdan, flag(F_BOGDAN1), -1);
    if(st.chapter >= 1 && n < 8) q(q_light, flag(F_BOARD3), -1);
    return n;
}

int places(Place out[PL_COUNT]) {
    int n = 0;
    // only places Szary has been to, or that the story has just sent him to
    if(st.chapter >= 1) out[n++] = PL_DUNES;
    if(flag(F_LH_ENTERED)) out[n++] = PL_LIGHTHOUSE;
    if(flag(F_CH1_DONE)) out[n++] = PL_BAR;
    if(flag(F_CH2_DONE)) { out[n++] = PL_PIER; out[n++] = PL_OFFICE; }
    if(flag(F_BUNKER_OPEN)) out[n++] = PL_BUNKER;
    return n;
}

Id place_name(Place p) {
    static const Id names[PL_COUNT] = {place_dunes, place_lighthouse, place_bar, place_pier, place_office,
                                       place_bunker};
    return names[p];
}

void travel(Place p) {
    // the next chapter begins when you first go where it happens
    if(p == PL_BAR && st.chapter == 1) { start_chapter(2, true); return; }
    if((p == PL_PIER || p == PL_OFFICE) && st.chapter == 2) { start_chapter(3, true); return; }
    if((p == PL_DUNES || p == PL_LIGHTHOUSE) && st.chapter == 3 && flag(F_CH3_DONE)) {
        start_chapter(4, true);
        return;
    }
    switch(p) {
    case PL_DUNES: push(A_SCENE, S_DUNES, st.chapter >= 4 ? 346 : 74); break;
    case PL_LIGHTHOUSE: push(A_SCENE, S_LIGHTHOUSE, 20); break;
    case PL_BAR: push(A_SCENE, S_BAR, 22); break;
    case PL_PIER: push(A_SCENE, S_PIER, 80); break;
    case PL_OFFICE: push(A_SCENE, S_OFFICE, 22); break;
    case PL_BUNKER: push(A_SCENE, S_BUNKER, 24); break;
    default: break;
    }
}

// ================================================================ boards

const BoardDef &board(Board b) { return boards[b]; }

bool board_done(Board b) { return board_flag(b); }

bool board_available(Board b) {
    switch(b) {
    case B_LIE: return has(clue::w_lie) && !flag(F_BOARD1);
    case B_FIRE: return has(clue::b_fire) && !flag(F_BOARD2);
    case B_FINAL: return st.chapter >= 5 && !flag(F_BOARD3);
    default: return false;
    }
}

bool board_submit(Board b, const uint8_t picks[3]) {
    const BoardDef &d = boards[b];
    bool right = true;
    for(int i = 0; i < d.blanks; i++) right = right && picks[i] == d.answer[i];
    switch(b) {
    case B_LIE:
        if(right) { set_flag(F_BOARD1); push(A_EVENT, EV_BOARD1_OK); }
        break;
    case B_FIRE:
        if(right) { set_flag(F_BOARD2); give(clue::w_false); push(A_EVENT, EV_BOARD2_OK); }
        break;
    case B_FINAL:
        set_flag(F_BOARD3);
        if(right) set_flag(F_ACCUSE_OK);
        else push(A_EVENT, EV_FINAL_BAD);
        push(A_SAVE);
        break;
    default: break;
    }
    return right;
}

// ================================================================ hints

float hint_delay_s() {
    // early on the player is still learning the notebook
    static const float delay[] = {45.0f, 45.0f, 60.0f, 90.0f, 120.0f, 150.0f};
    return delay[st.chapter < 0 ? 0 : st.chapter > 5 ? 5 : st.chapter];
}

Id hint() {
    // in the first chapters, an open pair in the notebook comes first
    if(st.chapter <= 2 && combo_waiting()) return hint_combine;
    switch(st.chapter) {
    case 0: return hint_prolog;
    case 1:
        // only what Szary has seen or could guess: no names before he meets them
        if(!has(clue::lamp) || !has(clue::log)) return hint_ch1a;
        if(!has(clue::bike)) return hint_ch1b;
        if(!flag(F_HALINA1)) return hint_ch1c;
        return hint_ch1d;
    case 2:
        if(has(clue::r_time) && flag(F_KUBA1) && !has(clue::k_time)) return hint_ch2b;
        if(!(flag(F_RYSIEK1) && flag(F_KUBA1) && has(clue::boots))) return hint_ch2a;
        if(!has(clue::w_lie)) return hint_ch2b;
        return hint_ch2c;
    case 3: return hint_ch3;
    case 4:
        if(!flag(F_TRAIL4)) return hint_ch4a;
        if(!flag(F_BUNKER_OPEN)) return has(clue::crowbar) ? hint_ch4a : hint_ch4b;
        return flag(F_BOGDAN1) ? hint_ch4c : hint_ch4d;
    default:
        if(!flag(F_BOARD3)) return hint_ch5a;
        return has(clue::fuse) && has(clue::page) ? hint_ch5b : hint_ch5c;
    }
}

}  // namespace story
}  // namespace sz
