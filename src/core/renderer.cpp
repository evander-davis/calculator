#include "calc/calculator.hpp"

#include <cctype>

namespace calc {
namespace {

constexpr std::uint8_t kBlank[5] = {0, 0, 0, 0, 0};

const std::uint8_t* glyph(char ch) {
    static constexpr std::uint8_t g0[5] = {0x3e, 0x51, 0x49, 0x45, 0x3e};
    static constexpr std::uint8_t g1[5] = {0x00, 0x42, 0x7f, 0x40, 0x00};
    static constexpr std::uint8_t g2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static constexpr std::uint8_t g3[5] = {0x21, 0x41, 0x45, 0x4b, 0x31};
    static constexpr std::uint8_t g4[5] = {0x18, 0x14, 0x12, 0x7f, 0x10};
    static constexpr std::uint8_t g5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static constexpr std::uint8_t g6[5] = {0x3c, 0x4a, 0x49, 0x49, 0x30};
    static constexpr std::uint8_t g7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static constexpr std::uint8_t g8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static constexpr std::uint8_t g9[5] = {0x06, 0x49, 0x49, 0x29, 0x1e};
    static constexpr std::uint8_t ga[5] = {0x7e, 0x11, 0x11, 0x11, 0x7e};
    static constexpr std::uint8_t gb[5] = {0x7f, 0x49, 0x49, 0x49, 0x36};
    static constexpr std::uint8_t gc[5] = {0x3e, 0x41, 0x41, 0x41, 0x22};
    static constexpr std::uint8_t gd[5] = {0x7f, 0x41, 0x41, 0x22, 0x1c};
    static constexpr std::uint8_t ge[5] = {0x7f, 0x49, 0x49, 0x49, 0x41};
    static constexpr std::uint8_t gf[5] = {0x7f, 0x09, 0x09, 0x09, 0x01};
    static constexpr std::uint8_t gg[5] = {0x3e, 0x41, 0x49, 0x49, 0x7a};
    static constexpr std::uint8_t gh[5] = {0x7f, 0x08, 0x08, 0x08, 0x7f};
    static constexpr std::uint8_t gi[5] = {0x00, 0x41, 0x7f, 0x41, 0x00};
    static constexpr std::uint8_t gj[5] = {0x20, 0x40, 0x41, 0x3f, 0x01};
    static constexpr std::uint8_t gk[5] = {0x7f, 0x08, 0x14, 0x22, 0x41};
    static constexpr std::uint8_t gl[5] = {0x7f, 0x40, 0x40, 0x40, 0x40};
    static constexpr std::uint8_t gm[5] = {0x7f, 0x02, 0x0c, 0x02, 0x7f};
    static constexpr std::uint8_t gn[5] = {0x7f, 0x04, 0x08, 0x10, 0x7f};
    static constexpr std::uint8_t go[5] = {0x3e, 0x41, 0x41, 0x41, 0x3e};
    static constexpr std::uint8_t gp[5] = {0x7f, 0x09, 0x09, 0x09, 0x06};
    static constexpr std::uint8_t gq[5] = {0x3e, 0x41, 0x51, 0x21, 0x5e};
    static constexpr std::uint8_t gr[5] = {0x7f, 0x09, 0x19, 0x29, 0x46};
    static constexpr std::uint8_t gs[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static constexpr std::uint8_t gt[5] = {0x01, 0x01, 0x7f, 0x01, 0x01};
    static constexpr std::uint8_t gu[5] = {0x3f, 0x40, 0x40, 0x40, 0x3f};
    static constexpr std::uint8_t gv[5] = {0x1f, 0x20, 0x40, 0x20, 0x1f};
    static constexpr std::uint8_t gw[5] = {0x3f, 0x40, 0x38, 0x40, 0x3f};
    static constexpr std::uint8_t gx[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static constexpr std::uint8_t gy[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
    static constexpr std::uint8_t gz[5] = {0x61, 0x51, 0x49, 0x45, 0x43};
    static constexpr std::uint8_t gplus[5] = {0x08, 0x08, 0x3e, 0x08, 0x08};
    static constexpr std::uint8_t gminus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static constexpr std::uint8_t gstar[5] = {0x14, 0x08, 0x3e, 0x08, 0x14};
    static constexpr std::uint8_t gslash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static constexpr std::uint8_t geq[5] = {0x14, 0x14, 0x14, 0x14, 0x14};
    static constexpr std::uint8_t glp[5] = {0x00, 0x1c, 0x22, 0x41, 0x00};
    static constexpr std::uint8_t grp[5] = {0x00, 0x41, 0x22, 0x1c, 0x00};
    static constexpr std::uint8_t gdot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static constexpr std::uint8_t gcom[5] = {0x00, 0xa0, 0x60, 0x00, 0x00};
    static constexpr std::uint8_t gcolon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static constexpr std::uint8_t gbang[5] = {0x00, 0x00, 0x5f, 0x00, 0x00};
    static constexpr std::uint8_t gquest[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static constexpr std::uint8_t garrow[5] = {0x08, 0x1c, 0x2a, 0x08, 0x08};
    static constexpr std::uint8_t gunder[5] = {0x40, 0x40, 0x40, 0x40, 0x40};

    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    switch (ch) {
        case '0': return g0;
        case '1': return g1;
        case '2': return g2;
        case '3': return g3;
        case '4': return g4;
        case '5': return g5;
        case '6': return g6;
        case '7': return g7;
        case '8': return g8;
        case '9': return g9;
        case 'A': return ga;
        case 'B': return gb;
        case 'C': return gc;
        case 'D': return gd;
        case 'E': return ge;
        case 'F': return gf;
        case 'G': return gg;
        case 'H': return gh;
        case 'I': return gi;
        case 'J': return gj;
        case 'K': return gk;
        case 'L': return gl;
        case 'M': return gm;
        case 'N': return gn;
        case 'O': return go;
        case 'P': return gp;
        case 'Q': return gq;
        case 'R': return gr;
        case 'S': return gs;
        case 'T': return gt;
        case 'U': return gu;
        case 'V': return gv;
        case 'W': return gw;
        case 'X': return gx;
        case 'Y': return gy;
        case 'Z': return gz;
        case '+': return gplus;
        case '-': return gminus;
        case '*': return gstar;
        case '/': return gslash;
        case '=': return geq;
        case '(': return glp;
        case ')': return grp;
        case '.': return gdot;
        case ',': return gcom;
        case ':': return gcolon;
        case '!': return gbang;
        case '?': return gquest;
        case '^': return garrow;
        case '_': return gunder;
        default: return kBlank;
    }
}

}  // namespace

Color rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<Color>(((r & 0xf8u) << 8u) | ((g & 0xfcu) << 3u) | (b >> 3u));
}

void clear(Display& display, Color color) {
    if (display.pixels == nullptr || display.width <= 0 || display.height <= 0) {
        return;
    }
    const int count = display.width * display.height;
    for (int i = 0; i < count; ++i) {
        display.pixels[i] = color;
    }
}

void set_pixel(Display& display, int x, int y, Color color) {
    if (display.pixels == nullptr || x < 0 || y < 0 || x >= display.width || y >= display.height) {
        return;
    }
    display.pixels[y * display.width + x] = color;
}

void draw_line(Display& display, int x0, int y0, int x1, int y1, Color color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        set_pixel(display, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_rect(Display& display, int x, int y, int w, int h, Color color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    draw_line(display, x, y, x + w - 1, y, color);
    draw_line(display, x, y + h - 1, x + w - 1, y + h - 1, color);
    draw_line(display, x, y, x, y + h - 1, color);
    draw_line(display, x + w - 1, y, x + w - 1, y + h - 1, color);
}

void fill_rect(Display& display, int x, int y, int w, int h, Color color) {
    if (display.pixels == nullptr || w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > display.width ? display.width : x + w;
    int y1 = y + h > display.height ? display.height : y + h;
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            display.pixels[yy * display.width + xx] = color;
        }
    }
}

void draw_text(Display& display, int x, int y, const char* text, Color fg, Color bg) {
    if (text == nullptr) {
        return;
    }
    int cx = x;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            cx = x;
            y += 9;
            continue;
        }
        const std::uint8_t* data = glyph(*p);
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                const bool on = (data[col] & (1u << row)) != 0;
                set_pixel(display, cx + col, y + row, on ? fg : bg);
            }
        }
        cx += 6;
    }
}

}  // namespace calc
