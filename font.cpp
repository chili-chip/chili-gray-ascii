#include "font.hpp"

#include "32blit.hpp"

using namespace blit;

namespace font {
namespace {

// 3x5 body, rows top to bottom, '#' = pixel. Row 0 and row 6 of the 4x7
// cell are left for accents.
// One string of 15 characters per glyph (5 rows of 3), from ' ' to DEL.
const char *const ascii_rows[96] = {
    /*   */ "...............",
    /* ! */ ".#..#..#.....#.",
    /* " */ "#.##.#.........",
    /* # */ "#.####.####.#.#",
    /* $ */ ".###.#.#.#.###.",
    /* % */ "#....#.#.#....#",
    /* & */ ".#.#.#.#.#.#.##",
    /* ' */ ".#..#..........",
    /* ( */ "..#.#..#..#...#",
    /* ) */ "#...#..#..#.#..",
    /* * */ "#.#.#.#.#......",
    /* + */ "....#.###.#....",
    /* , */ "..........#.#..",
    /* - */ "......###......",
    /* . */ ".............#.",
    /* / */ "..#..#.#.#..#..",
    /* 0 */ "####.##.##.####",
    /* 1 */ ".#.##..#..#.###",
    /* 2 */ "##...#.#.#..###",
    /* 3 */ "##...#.#...###.",
    /* 4 */ "#.##.####..#..#",
    /* 5 */ "####..##...###.",
    /* 6 */ ".###..####.####",
    /* 7 */ "###..#.#..#..#.",
    /* 8 */ "####.#####.####",
    /* 9 */ "####.####..###.",
    /* : */ "....#.....#....",
    /* ; */ "....#.....#.#..",
    /* < */ "..#.#.#...#...#",
    /* = */ "...###...###...",
    /* > */ "#...#...#.#.#..",
    /* ? */ "##...#.#.....#.",
    /* @ */ ".#.#.####...##.",
    /* A */ ".#.#.#####.##.#",
    /* B */ "##.#.###.#.###.",
    /* C */ ".###..#..#...##",
    /* D */ "##.#.##.##.###.",
    /* E */ "####..##.#..###",
    /* F */ "####..##.#..#..",
    /* G */ ".###..#.##.#.##",
    /* H */ "#.##.#####.##.#",
    /* I */ "###.#..#..#.###",
    /* J */ "..#..#..##.#.#.",
    /* K */ "#.##.###.#.##.#",
    /* L */ "#..#..#..#..###",
    /* M */ "#.#####.##.##.#",
    /* N */ "##.#.##.##.##.#",
    /* O */ ".#.#.##.##.#.#.",
    /* P */ "##.#.###.#..#..",
    /* Q */ ".#.#.##.###..##",
    /* R */ "##.#.###.#.##.#",
    /* S */ ".###...#...###.",
    /* T */ "###.#..#..#..#.",
    /* U */ "#.##.##.##.####",
    /* V */ "#.##.##.#.#..#.",
    /* W */ "#.##.##.#####.#",
    /* X */ "#.##.#.#.#.##.#",
    /* Y */ "#.##.#.#..#..#.",
    /* Z */ "###..#.#.#..###",
    /* [ */ "##.#..#..#..##.",
    /* \ */ "#..#...#...#..#",
    /* ] */ ".##..#..#..#.##",
    /* ^ */ ".#.#.#.........",
    /* _ */ "............###",
    /* ` */ "#...#..........",
    // small letters are only used as art; text maps a-z to capitals
    /* a */ "...............",
    /* b */ "...............",
    /* c */ "...............",
    /* d */ "...............",
    /* e */ "...............",
    /* f */ "...............",
    /* g */ "...............",
    /* h */ "...............",
    /* i */ "...............",
    /* j */ "...............",
    /* k */ "...............",
    /* l */ "...............",
    /* m */ "...............",
    /* n */ "...............",
    /* o */ "....#.#.#.#....",  // small round head
    /* p */ "...............",
    /* q */ "...............",
    /* r */ "...............",
    /* s */ "...............",
    /* t */ "...............",
    /* u */ "...............",
    /* v */ "......#.#.#....",  // a far bird
    /* w */ "...............",
    /* x */ "...............",
    /* y */ "...............",
    /* z */ "...............",
    /* { */ ".##.#.#...#..##",
    /* | */ ".#..#..#..#..#.",
    /* } */ "##..#...#.#.##.",
    /* ~ */ "...#.#.#.......",
    /* DEL */ "###############",
};

// Polish letters: base capital + mark.
enum Mark { ACUTE, DOT, OGONEK, STROKE };
struct Pl {
    uint32_t upper, lower;
    char base;
    Mark mark;
};
const Pl polish[] = {
    {0x104, 0x105, 'A', OGONEK}, {0x106, 0x107, 'C', ACUTE}, {0x118, 0x119, 'E', OGONEK},
    {0x141, 0x142, 'L', STROKE}, {0x143, 0x144, 'N', ACUTE}, {0x0D3, 0x0F3, 'O', ACUTE},
    {0x15A, 0x15B, 'S', ACUTE}, {0x179, 0x17A, 'Z', ACUTE}, {0x17B, 0x17C, 'Z', DOT},
};
constexpr int N_ASCII = 96;
constexpr int N_GLYPHS = N_ASCII + int(sizeof(polish) / sizeof(polish[0]));

// rows[0..6], 3-bit masks, bit 2 = left column
uint8_t glyphs[N_GLYPHS][7];
bool built = false;

uint8_t row_mask(const char *r) {
    return uint8_t((r[0] == '#' ? 4 : 0) | (r[1] == '#' ? 2 : 0) | (r[2] == '#' ? 1 : 0));
}

void build() {
    if(built) return;
    built = true;
    for(int g = 0; g < N_ASCII; g++) {
        const char *s = ascii_rows[g];
        char buf[16] = "...............";
        for(int i = 0; i < 15 && s[i]; i++) buf[i] = s[i];
        glyphs[g][0] = 0;
        for(int r = 0; r < 5; r++) glyphs[g][r + 1] = row_mask(buf + r * 3);
        glyphs[g][6] = 0;
    }
    for(int p = 0; p < int(sizeof(polish) / sizeof(polish[0])); p++) {
        uint8_t *g = glyphs[N_ASCII + p];
        const uint8_t *b = glyphs[polish[p].base - 32];
        for(int r = 0; r < 7; r++) g[r] = b[r];
        switch(polish[p].mark) {
        case ACUTE: g[0] = 1; break;            // ..#
        case DOT: g[0] = 2; break;              // .#.
        case OGONEK: g[6] = 1; break;           // ..# under the letter
        case STROKE: g[3] = uint8_t(g[3] | 6); break;  // ##. across the L
        }
    }
}

// micro marks, 2x3, bit 1 = left column
struct Micro {
    char c;
    uint8_t rows[3];
};
const Micro micros[] = {
    {'.', {0, 0, 2}}, {':', {2, 0, 2}}, {'-', {0, 3, 0}}, {'\'', {2, 0, 0}},
    {'~', {1, 2, 0}}, {'_', {0, 0, 3}}, {'|', {2, 2, 2}}, {'+', {2, 3, 2}},
    {',', {0, 1, 2}}, {'`', {2, 1, 0}},
};

uint8_t grey_of[SHADES] = {0, 50, 80, 112, 148, 186, 222, 252};
Pen pens[SHADES];
bool pens_ready = false;

void ready_pens() {
    if(pens_ready) return;
    for(int i = 0; i < SHADES; i++) pens[i] = Pen(grey_of[i], grey_of[i], grey_of[i]);
    pens_ready = true;
}

inline void span(int x, int y, int len, int shade) {
    if(y < 0 || y >= screen.bounds.h) return;
    if(x < 0) { len += x; x = 0; }
    if(x + len > screen.bounds.w) len = screen.bounds.w - x;
    if(len <= 0) return;
    screen.pbf(&pens[shade], &screen, screen.offset(x, y), len);
}

// draw one row mask of width w (bits from high = left)
inline void row(int x, int y, uint8_t mask, int w, int shade) {
    int start = -1;
    for(int i = 0; i <= w; i++) {
        bool on = i < w && (mask >> (w - 1 - i)) & 1;
        if(on && start < 0) start = i;
        if(!on && start >= 0) { span(x + start, y, i - start, shade); start = -1; }
    }
}

inline int clamp_shade(int s) { return s < 0 ? 0 : (s >= SHADES ? SHADES - 1 : s); }

}  // namespace

void set_shade_table(const uint8_t *grey) {
    for(int i = 0; i < SHADES; i++) grey_of[i] = grey[i];
    pens_ready = false;
}

int glyph_index(uint32_t cp) {
    build();
    if(cp >= 'a' && cp <= 'z') cp -= 32;
    if(cp >= 32 && cp < 128) return int(cp) - 32;
    for(int p = 0; p < int(sizeof(polish) / sizeof(polish[0])); p++)
        if(cp == polish[p].upper || cp == polish[p].lower) return N_ASCII + p;
    return -1;
}

void glyph(int x, int y, int index, int shade) {
    build();
    ready_pens();
    shade = clamp_shade(shade);
    if(index < 0 || index >= N_GLYPHS || shade == 0) return;
    if(x <= -CELL_W || x >= screen.bounds.w || y <= -CELL_H || y >= screen.bounds.h) return;
    const uint8_t *g = glyphs[index];
    for(int r = 0; r < 7; r++)
        if(g[r]) row(x, y + r, g[r], 3, shade);
}

void ch(int x, int y, char c, int shade) {
    build();
    int i = (uint8_t(c) >= 32 && uint8_t(c) < 128) ? c - 32 : -1;
    glyph(x, y, i, shade);
}

void hline(int x, int y, int len, int shade) {
    ready_pens();
    shade = clamp_shade(shade);
    if(shade == 0) return;
    span(x, y, len, shade);
}

void pixels(int x, int y, const char *const *rows, int n, int dir, int skin, int hair) {
    ready_pens();
    int w = 0;
    while(rows[0][w]) w++;
    for(int r = 0; r < n; r++) {
        for(int c = 0; rows[r][c]; c++) {
            int s;
            switch(rows[r][c]) {
            case 'S': s = skin; break;            // skin
            case 's': s = skin - 1; break;        // skin in shadow
            case 'H': s = hair; break;            // hair, hat, hood
            case 'h': s = hair + 2; break;        // light band on a hat
            case 'E': s = 0; break;               // eye: a black pixel
            default: continue;                    // see-through
            }
            if(s < 0) s = 0;
            s = clamp_shade(s);
            int px = dir < 0 ? x + (w - 1 - c) : x + c;
            span(px, y + r, 1, s);
        }
    }
}

void micro(int x, int y, char c, int shade) {
    ready_pens();
    shade = clamp_shade(shade);
    if(shade == 0 || x <= -2 || x >= screen.bounds.w || y <= -3 || y >= screen.bounds.h) return;
    for(const auto &m : micros) {
        if(m.c != c) continue;
        for(int r = 0; r < 3; r++)
            if(m.rows[r]) row(x, y + r, m.rows[r], 2, shade);
        return;
    }
}

uint32_t next_cp(std::string_view s, size_t &i) {
    uint8_t c = uint8_t(s[i++]);
    if(c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t cp = c & (0x3F >> extra);
    while(extra-- > 0 && i < s.size()) cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
    return cp;
}

int length(std::string_view s) {
    int n = 0;
    for(size_t i = 0; i < s.size();) { next_cp(s, i); n++; }
    return n;
}

bool has_all_glyphs(std::string_view s) {
    for(size_t i = 0; i < s.size();) {
        uint32_t cp = next_cp(s, i);
        if(cp != '\n' && glyph_index(cp) < 0) return false;
    }
    return true;
}

int text(int x, int y, std::string_view s, int shade, int max_chars) {
    int n = 0;
    for(size_t i = 0; i < s.size() && n < max_chars;) {
        uint32_t cp = next_cp(s, i);
        glyph(x + n * CELL_W, y, glyph_index(cp), shade);
        n++;
    }
    return n;
}

}  // namespace font
