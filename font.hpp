#pragma once
// ASCII glyphs drawn straight into the screen in shades of grey.
//
// Two fonts:
//  - text font: 3x5 capitals in a 4x7 cell (row 0 holds Polish accents,
//    row 6 the ogonek), so 32 characters fit across 128 px. Used for words
//    and for the bigger ASCII art (grass, people, the lighthouse).
//  - micro font: 2x3 marks (. : - ' ~ _ |) used as texture for far things,
//    like the dots and colons in an ASCII-shaded picture.
//
// Glyphs land on any pixel position, so things move one pixel at a time
// instead of jumping a whole character cell.

#include <cstdint>
#include <string_view>

namespace font {

constexpr int CELL_W = 4;   // text font advance
constexpr int CELL_H = 7;   // text font line height
constexpr int TEXT_COLS = 128 / CELL_W;

// Shades of grey, 0 = black (pixel off on the OLED) .. 7 = white.
constexpr int SHADES = 8;
void set_shade_table(const uint8_t *grey);  // 8 grey levels, 0..255

// Index of a glyph for a Unicode code point (capitals; small letters map to
// capitals; Polish letters included). Returns -1 when there is no glyph.
int glyph_index(uint32_t cp);

// One glyph of the text font with its top-left cell corner at (x, y).
void glyph(int x, int y, int index, int shade);
// One ASCII character (art), e.g. '|', '/', 'o'.
void ch(int x, int y, char c, int shade);
// One mark of the micro font: '.', ':', '-', '\'', '~', '_', '|', '+'.
void micro(int x, int y, char c, int shade);

// UTF-8 text on one line; returns the number of characters drawn.
int text(int x, int y, std::string_view utf8, int shade, int max_chars = 1 << 30);
// Number of characters (not bytes) in a UTF-8 string.
int length(std::string_view utf8);
// True when every character in the string has a glyph.
bool has_all_glyphs(std::string_view utf8);

// Decodes one UTF-8 code point at s[i] and advances i.
uint32_t next_cp(std::string_view s, size_t &i);

}  // namespace font
