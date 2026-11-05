#pragma once
// Every word the player reads, in Polish and English.
// The font has capitals only, so write in capitals. Keep each line short:
// the dialog box wraps at 30 characters and shows 3 lines at a time.

namespace texts {

enum Lang { PL = 0, EN = 1, LANGS };

struct Line {
    const char *speaker[LANGS];  // nullptr = Szary's thought (no box)
    const char *text[LANGS];
};

// The detective's name in the box header.
inline const char *const SZARY[LANGS] = {"SZARY", "SZARY"};
inline const char *const HALINA[LANGS] = {"HALINA", "HALINA"};

inline const Line intro[] = {
    {{nullptr, nullptr},
     {"WYDMA, LISTOPAD. LATARNIA ZGASŁA O 22:40. LATARNIK ZNIKNĄŁ.",
      "WYDMA, NOVEMBER. THE LIGHTHOUSE WENT DARK AT 22:40. THE KEEPER IS GONE."}},
};

inline const Line trail_dark[] = {
    {{nullptr, nullptr},
     {"ZA CIEMNO. BEZ LATARKI NIC TU NIE ZOBACZĘ.", "TOO DARK. I WON'T SEE A THING WITHOUT THE TORCH."}},
};

inline const Line trail_lit[] = {
    {{nullptr, nullptr},
     {"TRAWA LEŻY POD WIATR. KTOŚ TU SZEDŁ, I TO NIEDAWNO.",
      "THE GRASS LIES AGAINST THE WIND. SOMEONE WALKED HERE, NOT LONG AGO."}},
    {{nullptr, nullptr},
     {"ŚLAD IDZIE OD ŚCIEŻKI W STRONĘ MORZA.", "THE TRAIL LEAVES THE PATH AND HEADS FOR THE SEA."}},
};

inline const Line halina[] = {
    {{"HALINA", "HALINA"},
     {"ZNOWU PAN? O TEJ PORZE NA PLAŻY JEST TYLKO WIATR I JA.",
      "YOU AGAIN? AT THIS HOUR THE BEACH IS JUST THE WIND AND ME."}},
    {{"SZARY", "SZARY"},
     {"LATARNIA ZGASŁA O 22:40. WIDZIAŁA PANI KOGOŚ?", "THE LIGHT WENT OUT AT 22:40. DID YOU SEE ANYONE?"}},
    {{"HALINA", "HALINA"},
     {"ROWER BOGDANA STAŁ POD LATARNIĄ. A BOGDAN PO PIACHU NIE JEŹDZI.",
      "BOGDAN'S BIKE WAS BY THE LIGHTHOUSE. BOGDAN NEVER RIDES ON SAND."}},
    {{nullptr, nullptr}, {"NOTUJĘ: ROWER. KTO NIM PRZYJECHAŁ?", "NOTED: THE BIKE. WHO RODE IT HERE?"}},
};

// Small labels
inline const char *const TALK[LANGS] = {"A: ROZMOWA", "A: TALK"};
inline const char *const LOOK[LANGS] = {"A: OBEJRZYJ", "A: LOOK"};
inline const char *const LANG_NAME[LANGS] = {"POLSKI", "ENGLISH"};
inline const char *const TITLE_HINT[LANGS] = {"Y: LATARKA  X: JĘZYK", "Y: TORCH  X: LANGUAGE"};

}  // namespace texts
