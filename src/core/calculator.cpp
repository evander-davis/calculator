#include "calc/calculator.hpp"

#include <cctype>
#include <cmath>
#include <cstring>

namespace calc {
namespace {

constexpr Color kBlack = 0x0000;
constexpr Color kWhite = 0xffff;
constexpr Color kBlue = 0x001f;
constexpr Color kRed = 0xf800;
constexpr Color kGreen = 0x07e0;
constexpr Color kGray = 0xbdf7;
constexpr Color kLightGray = 0xe71c;
constexpr int kTextScale = 2;
constexpr int kTextW = 12;
constexpr int kTextH = 14;
constexpr int kSupW = 8;
constexpr int kSupH = 10;
constexpr int kTitleH = 18;
constexpr int kInputH = 52;

constexpr CalcReal real(double value) {
    return static_cast<CalcReal>(value);
}

struct HistoryEntry {
    char expression[kExpressionCapacity];
    char result[32];
    bool error;
};

struct CalculatorState {
    Platform* platform;
    Screen screen;
    EvalContext eval;
    GraphWindow window;
    char home_expr[kExpressionCapacity];
    int home_len;
    int home_cursor;
    int home_expr_scroll_x;
    char y_expr[kExpressionCapacity];
    int y_len;
    int y_cursor;
    int y_expr_scroll_x;
    HistoryEntry history[kHistoryCapacity];
    int history_count;
    int window_selection;
    std::uint32_t last_tick_ms;
    bool cursor_on;
    Key last_key;
    bool second_active;
    bool alpha_active;
    int history_selection;
    int edit_cursor_before_history;
};

CalculatorState g{};

std::uint32_t millis() {
    if (g.platform != nullptr && g.platform->clock.millis != nullptr) {
        return g.platform->clock.millis(g.platform->clock.context);
    }
    return 0;
}

void copy_string(char* dst, std::size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0u) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::size_t i = 0;
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void append_char(char* dst, std::size_t dst_size, std::size_t& pos, char ch) {
    if (pos + 1u >= dst_size) {
        return;
    }
    dst[pos++] = ch;
    dst[pos] = '\0';
}

void append_string(char* dst, std::size_t dst_size, std::size_t& pos, const char* src) {
    if (src == nullptr) {
        return;
    }
    for (const char* p = src; *p != '\0'; ++p) {
        append_char(dst, dst_size, pos, *p);
    }
}

void append_uint(char* dst, std::size_t dst_size, std::size_t& pos, unsigned int value) {
    char reversed[10]{};
    int count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < static_cast<int>(sizeof(reversed)));

    for (int i = count - 1; i >= 0; --i) {
        append_char(dst, dst_size, pos, reversed[i]);
    }
}

void append_fixed_abs(char* dst, std::size_t dst_size, std::size_t& pos, CalcReal value, int decimals) {
    if (value < 0.0) {
        value = -value;
    }
    unsigned int whole = static_cast<unsigned int>(value);
    append_uint(dst, dst_size, pos, whole);
    if (decimals <= 0) {
        return;
    }

    CalcReal fraction = value - static_cast<CalcReal>(whole);
    char digits[8]{};
    int used = decimals > static_cast<int>(sizeof(digits)) ? static_cast<int>(sizeof(digits)) : decimals;
    for (int i = 0; i < used; ++i) {
        fraction *= 10.0;
        const int digit = static_cast<int>(fraction);
        digits[i] = static_cast<char>('0' + digit);
        fraction -= static_cast<CalcReal>(digit);
    }

    while (used > 0 && digits[used - 1] == '0') {
        --used;
    }
    if (used == 0) {
        return;
    }

    append_char(dst, dst_size, pos, '.');
    for (int i = 0; i < used; ++i) {
        append_char(dst, dst_size, pos, digits[i]);
    }
}

char key_letter(Key key) {
    if (key >= Key::LetterA && key <= Key::LetterZ) {
        return static_cast<char>('A' + static_cast<int>(key) - static_cast<int>(Key::LetterA));
    }
    return '\0';
}

int expression_line_count() {
    return g.history_count * 2;
}

void clear_history_selection() {
    g.history_selection = -1;
}

bool insert_text(char* buffer, int& len, int& cursor, const char* text) {
    if (text == nullptr) {
        return false;
    }
    const int add = static_cast<int>(std::strlen(text));
    if (add <= 0 || len + add >= kExpressionCapacity) {
        return false;
    }
    for (int i = len; i >= cursor; --i) {
        buffer[i + add] = buffer[i];
    }
    for (int i = 0; i < add; ++i) {
        buffer[cursor + i] = text[i];
    }
    len += add;
    cursor += add;
    buffer[len] = '\0';
    return true;
}

bool insert_text_at(char* buffer, int& len, int pos, const char* text) {
    int cursor = pos;
    return insert_text(buffer, len, cursor, text);
}

void delete_range(char* buffer, int& len, int pos, int count) {
    if (pos < 0 || count <= 0 || pos >= len) {
        return;
    }
    if (pos + count > len) {
        count = len - pos;
    }
    for (int i = pos; i + count <= len; ++i) {
        buffer[i] = buffer[i + count];
    }
    len -= count;
}

void insert_template(char* buffer, int& len, int& cursor, const char* before, const char* after) {
    const int before_len = static_cast<int>(std::strlen(before));
    if (!insert_text(buffer, len, cursor, before)) {
        return;
    }
    const int inner_cursor = cursor;
    if (insert_text(buffer, len, cursor, after)) {
        cursor = inner_cursor;
    } else {
        cursor = inner_cursor;
    }
    (void)before_len;
}

void append_or_ans(char* buffer, int& len, int& cursor, const char* op_text) {
    if (cursor == 0) {
        insert_text(buffer, len, cursor, "Ans");
    }
    insert_text(buffer, len, cursor, op_text);
}

void insert_fraction(char* buffer, int& len, int& cursor) {
    if (insert_text(buffer, len, cursor, "()/()")) {
        cursor -= 4;
        if (cursor < 1) {
            cursor = 1;
        }
    }
}

void backspace(char* buffer, int& len, int& cursor) {
    if (cursor <= 0 || len <= 0) {
        return;
    }
    for (int i = cursor - 1; i < len; ++i) {
        buffer[i] = buffer[i + 1];
    }
    --cursor;
    --len;
}

void delete_at(char* buffer, int& len, int& cursor) {
    if (cursor >= len || len <= 0) {
        return;
    }
    for (int i = cursor; i < len; ++i) {
        buffer[i] = buffer[i + 1];
    }
    --len;
}

int fraction_end_at(const char* expr, int pos, int end, int& num_start, int& num_end, int& den_start, int& den_end);
bool move_cursor_horizontal(const char* buffer, int len, int& cursor, int direction);
void normalize_cursor_to_visible(const char* buffer, int len, int& cursor);
void ensure_cursor_visible(const char* expr, int len, int cursor, int visible_w, int& scroll_x);

void normalize_cursor(const char* buffer, int len, int& cursor) {
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > len) {
        cursor = len;
    }
    normalize_cursor_to_visible(buffer, len, cursor);
}

bool move_fraction_vertical(const char* buffer, int len, int& cursor, Key key) {
    int best_cursor = cursor;
    int best_span = 100000;
    for (int i = 0; i < len; ++i) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(buffer, i, len, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            const int span = frac_end - i;
            if (key == Key::Down && cursor >= num_start && cursor <= num_end) {
                int offset = cursor - num_start;
                const int den_len = den_end - den_start;
                if (offset > den_len) {
                    offset = den_len;
                }
                if (span < best_span) {
                    best_span = span;
                    best_cursor = den_start + offset;
                }
            }
            if (key == Key::Up && cursor >= den_start && cursor <= den_end) {
                int offset = cursor - den_start;
                const int num_len = num_end - num_start;
                if (offset > num_len) {
                    offset = num_len;
                }
                if (span < best_span) {
                    best_span = span;
                    best_cursor = num_start + offset;
                }
            }
        }
    }
    if (best_span < 100000) {
        cursor = best_cursor;
        normalize_cursor(buffer, len, cursor);
        return true;
    }
    return false;
}

void reset_home_expression() {
    g.home_expr[0] = '\0';
    g.home_len = 0;
    g.home_cursor = 0;
    g.home_expr_scroll_x = 0;
}

void push_history(const char* expression, const char* result, bool error) {
    for (int i = kHistoryCapacity - 1; i > 0; --i) {
        g.history[i] = g.history[i - 1];
    }
    copy_string(g.history[0].expression, sizeof(g.history[0].expression), expression);
    copy_string(g.history[0].result, sizeof(g.history[0].result), result);
    g.history[0].error = error;
    if (g.history_count < kHistoryCapacity) {
        ++g.history_count;
    }
    clear_history_selection();
}

void format_value(CalcReal value, char* out, std::size_t size) {
    if (out == nullptr || size == 0u) {
        return;
    }
    out[0] = '\0';
    std::size_t pos = 0;

    if (!std::isfinite(value)) {
        copy_string(out, size, "ERR");
        return;
    }
    if (value < 0.0) {
        append_char(out, size, pos, '-');
        value = -value;
    }
    if (value < 0.0000005) {
        append_char(out, size, pos, '0');
        return;
    }

    if (value >= 10000000.0 || value < 0.001) {
        int exponent = 0;
        while (value >= 10.0) {
            value /= 10.0;
            ++exponent;
        }
        while (value < 1.0) {
            value *= 10.0;
            --exponent;
        }
        append_fixed_abs(out, size, pos, value, 5);
        append_char(out, size, pos, 'E');
        if (exponent < 0) {
            append_char(out, size, pos, '-');
            exponent = -exponent;
        }
        append_uint(out, size, pos, static_cast<unsigned int>(exponent));
        return;
    }

    append_fixed_abs(out, size, pos, value, 6);
}

const char* selected_history_text() {
    if (g.history_selection < 0 || g.history_selection >= expression_line_count()) {
        return nullptr;
    }
    const int entry = g.history_selection / 2;
    const bool result_line = (g.history_selection & 1) == 0;
    if (entry < 0 || entry >= g.history_count) {
        return nullptr;
    }
    return result_line ? g.history[entry].result : g.history[entry].expression;
}

void paste_history_selection() {
    const char* text = selected_history_text();
    if (text == nullptr) {
        return;
    }
    g.home_cursor = g.edit_cursor_before_history;
    insert_text(g.home_expr, g.home_len, g.home_cursor, text);
    clear_history_selection();
}

void evaluate_home() {
    if (g.history_selection >= 0) {
        paste_history_selection();
        return;
    }

    const char* expression = g.home_expr;
    if (g.home_len == 0) {
        if (g.history_count == 0) {
            return;
        }
        expression = g.history[0].expression;
    }

    EvalResult result = evaluate_expression(expression, g.eval);
    char formatted[32]{};
    if (result.ok) {
        format_value(result.value, formatted, sizeof(formatted));
        push_history(expression, formatted, false);
        reset_home_expression();
    } else {
        copy_string(formatted, sizeof(formatted), eval_error_text(result.error));
        push_history(expression, formatted, true);
    }
}

bool insert_for_key(Key key, char* buffer, int& len, int& cursor) {
    switch (key) {
        case Key::Digit0: return insert_text(buffer, len, cursor, "0");
        case Key::Digit1: return insert_text(buffer, len, cursor, "1");
        case Key::Digit2: return insert_text(buffer, len, cursor, "2");
        case Key::Digit3: return insert_text(buffer, len, cursor, "3");
        case Key::Digit4: return insert_text(buffer, len, cursor, "4");
        case Key::Digit5: return insert_text(buffer, len, cursor, "5");
        case Key::Digit6: return insert_text(buffer, len, cursor, "6");
        case Key::Digit7: return insert_text(buffer, len, cursor, "7");
        case Key::Digit8: return insert_text(buffer, len, cursor, "8");
        case Key::Digit9: return insert_text(buffer, len, cursor, "9");
        case Key::Dot: return insert_text(buffer, len, cursor, ".");
        case Key::Add: append_or_ans(buffer, len, cursor, "+"); return true;
        case Key::Subtract: append_or_ans(buffer, len, cursor, "-"); return true;
        case Key::Multiply: append_or_ans(buffer, len, cursor, "*"); return true;
        case Key::Divide: append_or_ans(buffer, len, cursor, "/"); return true;
        case Key::Power: append_or_ans(buffer, len, cursor, "^"); return true;
        case Key::Square: append_or_ans(buffer, len, cursor, "^2"); return true;
        case Key::Reciprocal: append_or_ans(buffer, len, cursor, "^-1"); return true;
        case Key::LParen: return insert_text(buffer, len, cursor, "(");
        case Key::RParen: return insert_text(buffer, len, cursor, ")");
        case Key::Comma: return insert_text(buffer, len, cursor, ",");
        case Key::Negate: return insert_text(buffer, len, cursor, "-");
        case Key::Store: append_or_ans(buffer, len, cursor, "->"); return true;
        case Key::Fraction: insert_fraction(buffer, len, cursor); return true;
        case Key::Equal: return insert_text(buffer, len, cursor, "=");
        case Key::Sin: return insert_text(buffer, len, cursor, "sin(");
        case Key::Cos: return insert_text(buffer, len, cursor, "cos(");
        case Key::Tan: return insert_text(buffer, len, cursor, "tan(");
        case Key::ASin: return insert_text(buffer, len, cursor, "asin(");
        case Key::ACos: return insert_text(buffer, len, cursor, "acos(");
        case Key::ATan: return insert_text(buffer, len, cursor, "atan(");
        case Key::Sqrt: insert_template(buffer, len, cursor, "sqrt(", ")"); return true;
        case Key::Log: return insert_text(buffer, len, cursor, "log(");
        case Key::Ln: return insert_text(buffer, len, cursor, "ln(");
        case Key::Ans: return insert_text(buffer, len, cursor, "Ans");
        case Key::Pi: return insert_text(buffer, len, cursor, "pi");
        case Key::ConstE: return insert_text(buffer, len, cursor, "e");
        case Key::X: return insert_text(buffer, len, cursor, "X");
        default: break;
    }

    const char letter = key_letter(key);
    if (letter != '\0') {
        char text[2] = {letter, '\0'};
        return insert_text(buffer, len, cursor, text);
    }
    return false;
}

void edit_expression_key(Key key, char* buffer, int& len, int& cursor) {
    if ((key == Key::Up || key == Key::Down) && move_fraction_vertical(buffer, len, cursor, key)) {
        return;
    }
    switch (key) {
        case Key::Left:
            move_cursor_horizontal(buffer, len, cursor, -1);
            break;
        case Key::Right:
            move_cursor_horizontal(buffer, len, cursor, 1);
            break;
        case Key::Delete:
            delete_at(buffer, len, cursor);
            break;
        case Key::Back:
            backspace(buffer, len, cursor);
            break;
        case Key::Clear:
            buffer[0] = '\0';
            len = 0;
            cursor = 0;
            break;
        default:
            insert_for_key(key, buffer, len, cursor);
            break;
    }
    normalize_cursor(buffer, len, cursor);
}

void pan_graph(CalcReal dx_fraction, CalcReal dy_fraction) {
    const CalcReal dx = (g.window.xmax - g.window.xmin) * dx_fraction;
    const CalcReal dy = (g.window.ymax - g.window.ymin) * dy_fraction;
    g.window.xmin += dx;
    g.window.xmax += dx;
    g.window.ymin += dy;
    g.window.ymax += dy;
}

void zoom_graph(CalcReal factor) {
    const CalcReal cx = (g.window.xmin + g.window.xmax) * real(0.5);
    const CalcReal cy = (g.window.ymin + g.window.ymax) * real(0.5);
    const CalcReal hx = (g.window.xmax - g.window.xmin) * real(0.5) * factor;
    const CalcReal hy = (g.window.ymax - g.window.ymin) * real(0.5) * factor;
    g.window.xmin = cx - hx;
    g.window.xmax = cx + hx;
    g.window.ymin = cy - hy;
    g.window.ymax = cy + hy;
}

Key second_key(Key key) {
    switch (key) {
        case Key::Negate: return Key::Ans;
        case Key::Fraction: return Key::Mode;
        case Key::Square: return Key::Sqrt;
        case Key::Sin: return Key::ASin;
        case Key::Cos: return Key::ACos;
        case Key::Tan: return Key::ATan;
        case Key::Power: return Key::Pi;
        case Key::Divide: return Key::ConstE;
        default: return key;
    }
}

Key alpha_key(Key key) {
    switch (key) {
        case Key::Math: return Key::LetterA;
        case Key::Apps: return Key::LetterB;
        case Key::Program: return Key::LetterC;
        case Key::Reciprocal: return Key::LetterD;
        case Key::Sin: return Key::LetterE;
        case Key::Cos: return Key::LetterF;
        case Key::Tan: return Key::LetterG;
        case Key::Power: return Key::LetterH;
        case Key::Dot: return Key::LetterI;
        case Key::Comma: return Key::LetterJ;
        case Key::LParen: return Key::LetterK;
        case Key::RParen: return Key::LetterL;
        case Key::Divide: return Key::LetterM;
        case Key::Log: return Key::LetterN;
        case Key::Digit7: return Key::LetterO;
        case Key::Digit8: return Key::LetterP;
        case Key::Digit9: return Key::LetterQ;
        case Key::Multiply: return Key::LetterR;
        case Key::Ln: return Key::LetterS;
        case Key::Digit4: return Key::LetterT;
        case Key::Digit5: return Key::LetterU;
        case Key::Digit6: return Key::LetterV;
        case Key::Subtract: return Key::LetterW;
        case Key::Store: return Key::LetterX;
        case Key::Digit1: return Key::LetterY;
        case Key::Digit2: return Key::LetterZ;
        default: return key;
    }
}

Key translate_layer_key(Key key) {
    if (key == Key::Second) {
        g.second_active = !g.second_active;
        g.alpha_active = false;
        return Key::None;
    }
    if (key == Key::Alpha) {
        g.alpha_active = !g.alpha_active;
        g.second_active = false;
        return Key::None;
    }
    if (g.second_active) {
        g.second_active = false;
        return second_key(key);
    }
    if (g.alpha_active) {
        g.alpha_active = false;
        return alpha_key(key);
    }
    return key;
}

void handle_global_key(Key key) {
    switch (key) {
        case Key::Home: g.screen = Screen::Home; break;
        case Key::Graph: g.screen = Screen::Graph; break;
        case Key::YEquals: g.screen = Screen::YEquals; break;
        case Key::Window: g.screen = Screen::Window; break;
        case Key::Settings: g.screen = Screen::Settings; break;
        case Key::Mode: g.screen = Screen::Settings; break;
        case Key::About: g.screen = Screen::About; break;
        case Key::On: g.screen = Screen::Home; break;
        default: break;
    }
}

void handle_home_key(Key key) {
    if (key == Key::Up || key == Key::Down) {
        if (move_fraction_vertical(g.home_expr, g.home_len, g.home_cursor, key)) {
            clear_history_selection();
            ensure_cursor_visible(g.home_expr, g.home_len, g.home_cursor, kLcdWidth - 26, g.home_expr_scroll_x);
            return;
        }
        const int lines = expression_line_count();
        if (lines == 0) {
            return;
        }
        if (g.history_selection < 0) {
            g.edit_cursor_before_history = g.home_cursor;
            g.history_selection = 0;
            if (g.history_selection >= lines) {
                g.history_selection = lines - 1;
            }
        } else if (key == Key::Up) {
            if (g.history_selection + 1 < lines) {
                ++g.history_selection;
            }
        } else {
            --g.history_selection;
            if (g.history_selection < 0) {
                clear_history_selection();
            }
        }
        return;
    }
    if (key == Key::Enter) {
        evaluate_home();
        return;
    }
    if (key == Key::Clear && g.home_len == 0) {
        g.history_count = 0;
        clear_history_selection();
        return;
    }
    clear_history_selection();
    edit_expression_key(key, g.home_expr, g.home_len, g.home_cursor);
    ensure_cursor_visible(g.home_expr, g.home_len, g.home_cursor, kLcdWidth - 26, g.home_expr_scroll_x);
}

void handle_y_key(Key key) {
    if (key == Key::Enter || key == Key::Graph) {
        g.screen = Screen::Graph;
        return;
    }
    edit_expression_key(key, g.y_expr, g.y_len, g.y_cursor);
    ensure_cursor_visible(g.y_expr, g.y_len, g.y_cursor, kLcdWidth - 38, g.y_expr_scroll_x);
}

void handle_graph_key(Key key) {
    switch (key) {
        case Key::Left: pan_graph(real(-0.1), real(0.0)); break;
        case Key::Right: pan_graph(real(0.1), real(0.0)); break;
        case Key::Up: pan_graph(real(0.0), real(0.1)); break;
        case Key::Down: pan_graph(real(0.0), real(-0.1)); break;
        case Key::Add: zoom_graph(real(0.75)); break;
        case Key::Subtract: zoom_graph(real(1.25)); break;
        default: break;
    }
}

void handle_window_key(Key key) {
    CalcReal* values[4] = {&g.window.xmin, &g.window.xmax, &g.window.ymin, &g.window.ymax};
    switch (key) {
        case Key::Up:
            if (g.window_selection > 0) {
                --g.window_selection;
            }
            break;
        case Key::Down:
            if (g.window_selection < 3) {
                ++g.window_selection;
            }
            break;
        case Key::Add:
        case Key::Right:
            *values[g.window_selection] += real(1.0);
            break;
        case Key::Subtract:
        case Key::Left:
            *values[g.window_selection] -= real(1.0);
            break;
        case Key::Enter:
        case Key::Graph:
            g.screen = Screen::Graph;
            break;
        default:
            break;
    }
    if (g.window.xmax <= g.window.xmin + real(0.1)) {
        g.window.xmax = g.window.xmin + real(0.1);
    }
    if (g.window.ymax <= g.window.ymin + real(0.1)) {
        g.window.ymax = g.window.ymin + real(0.1);
    }
}

void title(Display& display, const char* text) {
    fill_rect(display, 0, 0, kLcdWidth, kTitleH, kBlue);
    draw_text_scaled(display, 3, 2, text, kTextScale, kWhite, kBlue);
}

void dotted_line(Display& display, int y) {
    for (int x = 4; x < kLcdWidth - 4; x += 6) {
        draw_line(display, x, y, x + 2, y, kGray);
    }
}

void dotted_rect(Display& display, int x, int y, int w, int h, Color color) {
    for (int xx = x; xx < x + w; xx += 4) {
        set_pixel(display, xx, y, color);
        set_pixel(display, xx, y + h - 1, color);
    }
    for (int yy = y; yy < y + h; yy += 4) {
        set_pixel(display, x, yy, color);
        set_pixel(display, x + w - 1, yy, color);
    }
}

bool starts_with_at(const char* text, int pos, const char* prefix) {
    for (int i = 0; prefix[i] != '\0'; ++i) {
        if (text[pos + i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

void draw_cursor(Display& display, int x, int y, int h, Color color) {
    if (g.cursor_on) {
        draw_line(display, x, y, x, y + h - 1, color);
    }
}

int matching_paren(const char* expr, int open, int end) {
    int depth = 0;
    for (int i = open; i < end; ++i) {
        if (expr[i] == '(') {
            ++depth;
        } else if (expr[i] == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

int fraction_end_at(const char* expr, int pos, int end, int& num_start, int& num_end, int& den_start, int& den_end) {
    if (pos >= end || expr[pos] != '(') {
        return -1;
    }
    const int first_close = matching_paren(expr, pos, end);
    if (first_close < 0 || first_close + 2 >= end || expr[first_close + 1] != '/' || expr[first_close + 2] != '(') {
        return -1;
    }
    const int second_close = matching_paren(expr, first_close + 2, end);
    if (second_close < 0) {
        return -1;
    }
    num_start = pos + 1;
    num_end = first_close;
    den_start = first_close + 3;
    den_end = second_close;
    return second_close + 1;
}

struct ExprBox {
    int w;
    int h;
    int ascent;
    int descent;
};

enum class LayoutKind : std::uint8_t {
    Row,
    TextRun,
    Fraction,
    Sqrt,
    Superscript,
    StoreArrow,
    Placeholder
};

enum class CursorRegion : std::uint8_t {
    Main,
    Numerator,
    Denominator,
    Exponent,
    Radical
};

struct LayoutNode {
    LayoutKind kind;
    int source_start;
    int source_end;
    int parent;
    int child;
    int next;
    int x;
    int y;
    int w;
    int ascent;
    int descent;
    bool small;
};

struct CursorAnchor {
    int source;
    int x;
    int y;
    int h;
    CursorRegion region;
};

constexpr int kMaxLayoutNodes = 96;
constexpr int kMaxCursorAnchors = kExpressionCapacity + 1;

struct LayoutContext {
    LayoutNode nodes[kMaxLayoutNodes];
    CursorAnchor anchors[kMaxCursorAnchors];
    int node_count;
    int anchor_count;
    bool overflow;
};

int font_w(bool small) {
    return small ? kSupW : kTextW;
}

int font_h(bool small) {
    return small ? kSupH : kTextH;
}

int font_ascent(bool small) {
    return small ? 8 : 11;
}

int font_descent(bool small) {
    return font_h(small) - font_ascent(small);
}

ExprBox box_from_ascent(int w, int ascent, int descent) {
    return {w, ascent + descent, ascent, descent};
}

ExprBox font_box(bool small) {
    return box_from_ascent(0, font_ascent(small), font_descent(small));
}

ExprBox placeholder_box(bool small) {
    const int w = small ? 18 : 24;
    return box_from_ascent(w, font_ascent(small) + 1, font_descent(small) + 1);
}

void add_node(LayoutContext* layout, LayoutKind kind, int start, int end, bool small, const ExprBox& box) {
    if (layout == nullptr) {
        return;
    }
    if (layout->node_count >= kMaxLayoutNodes) {
        layout->overflow = true;
        return;
    }
    LayoutNode& node = layout->nodes[layout->node_count++];
    node.kind = kind;
    node.source_start = start;
    node.source_end = end;
    node.parent = -1;
    node.child = -1;
    node.next = -1;
    node.x = 0;
    node.y = 0;
    node.w = box.w;
    node.ascent = box.ascent;
    node.descent = box.descent;
    node.small = small;
}

void add_anchor(LayoutContext* layout, int source, int x, int baseline, int h, CursorRegion region) {
    if (layout == nullptr) {
        return;
    }
    if (layout->anchor_count > 0) {
        const CursorAnchor& prev = layout->anchors[layout->anchor_count - 1];
        if (prev.source == source && prev.x == x && prev.region == region) {
            return;
        }
    }
    if (layout->anchor_count >= kMaxCursorAnchors) {
        layout->overflow = true;
        return;
    }
    CursorAnchor& anchor = layout->anchors[layout->anchor_count++];
    anchor.source = source;
    anchor.x = x;
    anchor.y = baseline - h + 1;
    anchor.h = h;
    anchor.region = region;
}

bool token_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '.';
}

int function_call_end(const char* expr, int start, int end) {
    int i = start;
    while (i < end && std::isalpha(static_cast<unsigned char>(expr[i]))) {
        ++i;
    }
    if (i > start && i < end && expr[i] == '(') {
        const int close = matching_paren(expr, i, end);
        if (close >= 0) {
            return close + 1;
        }
    }
    return -1;
}

void exponent_range(const char* expr, int caret, int end, int& visual_start, int& visual_end, int& source_end) {
    visual_start = caret + 1;
    visual_end = visual_start;
    source_end = visual_start;
    if (visual_start >= end) {
        return;
    }
    if (expr[visual_start] == '(') {
        const int close = matching_paren(expr, visual_start, end);
        if (close >= 0) {
            visual_start = visual_start + 1;
            visual_end = close;
            source_end = close + 1;
            return;
        }
    }
    const int call_end = function_call_end(expr, visual_start, end);
    if (call_end > 0) {
        visual_end = call_end;
        source_end = call_end;
        return;
    }
    int i = visual_start;
    if (i < end && expr[i] == '-') {
        ++i;
    }
    while (i < end && token_char(expr[i])) {
        ++i;
    }
    visual_end = i;
    source_end = i;
}

ExprBox measure_expression_range_impl(const char* expr, int start, int end, bool small, LayoutContext* layout, CursorRegion region);

ExprBox measure_expression_range_impl(const char* expr, int start, int end, bool small, LayoutContext* layout, CursorRegion region) {
    if (start >= end) {
        const ExprBox box = placeholder_box(small);
        add_node(layout, LayoutKind::Placeholder, start, end, small, box);
        return box;
    }

    int w = 0;
    int ascent = font_ascent(small);
    int descent = font_descent(small);
    for (int i = start; i < end;) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, layout, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, layout, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const ExprBox box = box_from_ascent(inner_w + 10, num.h + 3, den.h + 4);
            add_node(layout, LayoutKind::Fraction, i, frac_end, true, box);
            w += box.w + 2;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = frac_end;
            continue;
        }
        if (i + 5 <= end && starts_with_at(expr, i, "sqrt(")) {
            const int close = matching_paren(expr, i + 4, end);
            const int inner_start = i + 5;
            const int inner_end = close < 0 ? end : close;
            const ExprBox inner = measure_expression_range_impl(expr, inner_start, inner_end, small, layout, CursorRegion::Radical);
            const ExprBox box = box_from_ascent(inner.w + 19, inner.ascent + 4, inner.descent);
            add_node(layout, LayoutKind::Sqrt, i, close < 0 ? end : close + 1, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = close < 0 ? end : close + 1;
            continue;
        }
        if (expr[i] == '^') {
            int exp_start = 0;
            int exp_end = 0;
            int source_end = 0;
            exponent_range(expr, i, end, exp_start, exp_end, source_end);
            const ExprBox exp = exp_start == exp_end ? placeholder_box(true) : measure_expression_range_impl(expr, exp_start, exp_end, true, layout, CursorRegion::Exponent);
            const ExprBox box = box_from_ascent(exp.w, exp.h + 2, 0);
            add_node(layout, LayoutKind::Superscript, i, source_end, true, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            i = source_end;
            continue;
        }
        if (expr[i] == '-' && i + 1 < end && expr[i + 1] == '>') {
            const ExprBox box = box_from_ascent(18, font_ascent(small), font_descent(small));
            add_node(layout, LayoutKind::StoreArrow, i, i + 2, small, box);
            w += box.w;
            i += 2;
            continue;
        }
        const ExprBox box = box_from_ascent(font_w(small), font_ascent(small), font_descent(small));
        add_node(layout, LayoutKind::TextRun, i, i + 1, small, box);
        w += box.w;
        ++i;
    }
    const ExprBox box = box_from_ascent(w, ascent, descent);
    add_node(layout, LayoutKind::Row, start, end, small, box);
    (void)region;
    return box;
}

ExprBox measure_expression_range(const char* expr, int start, int end) {
    LayoutContext layout{};
    return measure_expression_range_impl(expr, start, end, false, &layout, CursorRegion::Main);
}

int expression_top_overhang_range(const char* expr, int start, int end) {
    return measure_expression_range(expr, start, end).ascent - font_ascent(false);
}

void draw_placeholder(Display& display, int x, int baseline, bool small, Color fg) {
    const ExprBox box = placeholder_box(small);
    dotted_rect(display, x, baseline - box.ascent, box.w, box.h, fg);
}

void draw_expression_range_impl(Display& display, int x, int baseline, const char* expr, int start, int end, bool small, Color fg, Color bg);

void draw_expression_range_impl(Display& display, int x, int baseline, const char* expr, int start, int end, bool small, Color fg, Color bg) {
    if (start >= end) {
        draw_placeholder(display, x, baseline, small, fg);
        return;
    }

    int cx = x;
    for (int i = start; i < end;) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, nullptr, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, nullptr, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const int box_w = inner_w + 10;
            const int num_x = cx + 5 + (inner_w - num.w) / 2;
            const int den_x = cx + 5 + (inner_w - den.w) / 2;
            draw_expression_range_impl(display, num_x, baseline - 3 - num.descent, expr, num_start, num_end, true, fg, bg);
            draw_line(display, cx + 2, baseline, cx + box_w - 3, baseline, fg);
            draw_expression_range_impl(display, den_x, baseline + 4 + den.ascent, expr, den_start, den_end, true, fg, bg);
            cx += box_w + 2;
            i = frac_end;
            continue;
        }
        if (i + 5 <= end && starts_with_at(expr, i, "sqrt(")) {
            const int close = matching_paren(expr, i + 4, end);
            const int inner_start = i + 5;
            const int inner_end = close < 0 ? end : close;
            const ExprBox inner = measure_expression_range_impl(expr, inner_start, inner_end, small, nullptr, CursorRegion::Radical);
            const int top = baseline - inner.ascent - 4;
            const int inner_x = cx + 15;
            draw_line(display, cx + 1, baseline - 3, cx + 5, baseline + 2, fg);
            draw_line(display, cx + 5, baseline + 2, cx + 12, top, fg);
            draw_line(display, cx + 12, top, inner_x + inner.w + 3, top, fg);
            draw_expression_range_impl(display, inner_x, baseline, expr, inner_start, inner_end, small, fg, bg);
            cx += inner.w + 19;
            i = close < 0 ? end : close + 1;
            continue;
        }
        if (expr[i] == '-' && i + 1 < end && expr[i + 1] == '>') {
            const int mid = baseline - font_ascent(small) / 2;
            draw_line(display, cx + 1, mid, cx + 15, mid, fg);
            draw_line(display, cx + 10, mid - 4, cx + 15, mid, fg);
            draw_line(display, cx + 10, mid + 4, cx + 15, mid, fg);
            cx += 18;
            i += 2;
            continue;
        }
        if (expr[i] == '^') {
            int exp_start = 0;
            int exp_end = 0;
            int source_end = 0;
            exponent_range(expr, i, end, exp_start, exp_end, source_end);
            const ExprBox exp = exp_start == exp_end ? placeholder_box(true) : measure_expression_range_impl(expr, exp_start, exp_end, true, nullptr, CursorRegion::Exponent);
            const int exp_baseline = baseline - font_ascent(small) + exp.ascent - 1;
            if (exp_start == exp_end) {
                draw_placeholder(display, cx, exp_baseline, true, fg);
            } else {
                draw_expression_range_impl(display, cx, exp_baseline, expr, exp_start, exp_end, true, fg, bg);
            }
            cx += exp.w;
            i = source_end;
            continue;
        }
        char text[2] = {expr[i], '\0'};
        draw_text_scaled(display, cx, baseline - font_ascent(small), text, small ? 1 : kTextScale, fg, bg);
        cx += font_w(small);
        ++i;
    }
}

void emit_anchors_range(LayoutContext& layout, int x, int baseline, const char* expr, int start, int end, bool small, CursorRegion region) {
    if (start >= end) {
        add_anchor(&layout, start, x + 3, baseline, font_h(small), region);
        return;
    }

    int cx = x;
    for (int i = start; i < end;) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, nullptr, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, nullptr, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const int box_w = inner_w + 10;
            const int num_x = cx + 5 + (inner_w - num.w) / 2;
            const int den_x = cx + 5 + (inner_w - den.w) / 2;
            emit_anchors_range(layout, num_x, baseline - 3 - num.descent, expr, num_start, num_end, true, CursorRegion::Numerator);
            emit_anchors_range(layout, den_x, baseline + 4 + den.ascent, expr, den_start, den_end, true, CursorRegion::Denominator);
            cx += box_w + 2;
            add_anchor(&layout, frac_end, cx, baseline, font_h(small), region);
            i = frac_end;
            continue;
        }
        if (i + 5 <= end && starts_with_at(expr, i, "sqrt(")) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const int close = matching_paren(expr, i + 4, end);
            const int inner_start = i + 5;
            const int inner_end = close < 0 ? end : close;
            const ExprBox inner = measure_expression_range_impl(expr, inner_start, inner_end, small, nullptr, CursorRegion::Radical);
            emit_anchors_range(layout, cx + 15, baseline, expr, inner_start, inner_end, small, CursorRegion::Radical);
            cx += inner.w + 19;
            const int source_end = close < 0 ? end : close + 1;
            add_anchor(&layout, source_end, cx, baseline, font_h(small), region);
            i = source_end;
            continue;
        }
        if (expr[i] == '-' && i + 1 < end && expr[i + 1] == '>') {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            cx += 18;
            i += 2;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            continue;
        }
        if (expr[i] == '^') {
            int exp_start = 0;
            int exp_end = 0;
            int source_end = 0;
            exponent_range(expr, i, end, exp_start, exp_end, source_end);
            const ExprBox exp = exp_start == exp_end ? placeholder_box(true) : measure_expression_range_impl(expr, exp_start, exp_end, true, nullptr, CursorRegion::Exponent);
            const int exp_baseline = baseline - font_ascent(small) + exp.ascent - 1;
            emit_anchors_range(layout, cx, exp_baseline, expr, exp_start, exp_end, true, CursorRegion::Exponent);
            cx += exp.w;
            add_anchor(&layout, source_end, cx, baseline, font_h(small), region);
            i = source_end;
            continue;
        }
        add_anchor(&layout, i, cx, baseline, font_h(small), region);
        cx += font_w(small);
        ++i;
    }
    add_anchor(&layout, end, cx, baseline, font_h(small), region);
}

LayoutContext build_layout_anchors(const char* expr, int len, int x, int y) {
    LayoutContext layout{};
    const ExprBox box = measure_expression_range_impl(expr, 0, len, false, &layout, CursorRegion::Main);
    emit_anchors_range(layout, x, y + box.ascent, expr, 0, len, false, CursorRegion::Main);
    return layout;
}

int anchor_index_for_cursor(const LayoutContext& layout, int cursor) {
    int best = -1;
    int best_dist = 100000;
    for (int i = 0; i < layout.anchor_count; ++i) {
        const int dist = layout.anchors[i].source > cursor ? layout.anchors[i].source - cursor : cursor - layout.anchors[i].source;
        if (dist < best_dist) {
            best = i;
            best_dist = dist;
        }
        if (dist == 0) {
            return i;
        }
    }
    return best;
}

void normalize_cursor_to_visible(const char* buffer, int len, int& cursor) {
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > len) {
        cursor = len;
    }
    LayoutContext layout = build_layout_anchors(buffer, len, 0, 0);
    const int index = anchor_index_for_cursor(layout, cursor);
    if (index >= 0) {
        cursor = layout.anchors[index].source;
    }
}

bool move_cursor_horizontal(const char* buffer, int len, int& cursor, int direction) {
    LayoutContext layout = build_layout_anchors(buffer, len, 0, 0);
    const int index = anchor_index_for_cursor(layout, cursor);
    if (index < 0) {
        return false;
    }
    const int next = index + (direction < 0 ? -1 : 1);
    if (next < 0 || next >= layout.anchor_count) {
        return false;
    }
    cursor = layout.anchors[next].source;
    return true;
}

void ensure_cursor_visible(const char* expr, int len, int cursor, int visible_w, int& scroll_x) {
    const ExprBox box = measure_expression_range(expr, 0, len);
    const int max_scroll = box.w > visible_w ? box.w - visible_w : 0;
    if (scroll_x > max_scroll) {
        scroll_x = max_scroll;
    }
    if (scroll_x < 0) {
        scroll_x = 0;
    }
    LayoutContext layout = build_layout_anchors(expr, len, 0, 0);
    const int index = anchor_index_for_cursor(layout, cursor);
    if (index < 0) {
        return;
    }
    const int x = layout.anchors[index].x;
    if (x < scroll_x + 4) {
        scroll_x = x - 4;
    } else if (x > scroll_x + visible_w - 5) {
        scroll_x = x - visible_w + 5;
    }
    if (scroll_x < 0) {
        scroll_x = 0;
    }
    if (scroll_x > max_scroll) {
        scroll_x = max_scroll;
    }
}

int draw_expression(Display& display, int x, int y, const char* expr, int cursor, Color fg, Color bg) {
    const int len = static_cast<int>(std::strlen(expr));
    const ExprBox box = measure_expression_range(expr, 0, len);
    const int baseline = y + box.ascent;
    draw_expression_range_impl(display, x, baseline, expr, 0, len, false, fg, bg);
    if (cursor >= 0) {
        LayoutContext layout = build_layout_anchors(expr, len, x, y);
        const int index = anchor_index_for_cursor(layout, cursor);
        if (index >= 0) {
            draw_cursor(display, layout.anchors[index].x, layout.anchors[index].y, layout.anchors[index].h, fg);
        }
    }
    return box.w;
}

void draw_input_line(Display& display, const char* prompt, const char* expr, int cursor, int& scroll_x) {
    const int y = kLcdHeight - kInputH;
    const int prompt_w = 24;
    const int expr_x = prompt_w;
    const int visible_w = kLcdWidth - expr_x - 2;
    const int len = static_cast<int>(std::strlen(expr));
    ensure_cursor_visible(expr, len, cursor, visible_w, scroll_x);
    const ExprBox box = measure_expression_range(expr, 0, len);
    int expr_y = y + 4;
    if (box.h + 8 < kInputH) {
        expr_y = y + (kInputH - box.h) / 2;
    }
    fill_rect(display, 0, y, kLcdWidth, kInputH, kWhite);
    draw_expression(display, expr_x - scroll_x, expr_y, expr, cursor, kBlack, kWhite);
    fill_rect(display, 0, y, expr_x, kInputH, kWhite);
    draw_text_scaled(display, 4, y + 6, prompt, kTextScale, kBlack, kWhite);
}

int history_row_height(const HistoryEntry& entry) {
    const ExprBox expr_box = measure_expression_range(entry.expression, 0, static_cast<int>(std::strlen(entry.expression)));
    const ExprBox ans_box = measure_expression_range(entry.result, 0, static_cast<int>(std::strlen(entry.result)));
    return expr_box.h + 3 + ans_box.h + 7;
}

void render_home(Display& display) {
    clear(display, kWhite);
    const int input_y = kLcdHeight - kInputH;
    const int selected_entry = g.history_selection >= 0 ? g.history_selection / 2 : 0;
    int first = selected_entry;
    int last = selected_entry - 1;
    int used_h = 0;
    while (last + 1 < g.history_count) {
        const int next = last + 1;
        const int row_h = history_row_height(g.history[next]);
        if (used_h + row_h > input_y - 2 && last >= first) {
            break;
        }
        used_h += row_h;
        last = next;
        if (g.history_selection < 0 && last + 1 >= g.history_count) {
            break;
        }
    }
    if (g.history_selection < 0) {
        while (first > 0) {
            const int next = first - 1;
            const int row_h = history_row_height(g.history[next]);
            if (used_h + row_h > input_y - 2) {
                break;
            }
            used_h += row_h;
            first = next;
        }
    }
    if (g.history_count == 0) {
        first = 0;
        last = -1;
    }
    int y = 2;
    for (int i = last; i >= first; --i) {
        const ExprBox expr_box = measure_expression_range(g.history[i].expression, 0, static_cast<int>(std::strlen(g.history[i].expression)));
        const ExprBox ans_box = measure_expression_range(g.history[i].result, 0, static_cast<int>(std::strlen(g.history[i].result)));
        const int expr_sel = i * 2 + 1;
        const int ans_sel = i * 2;
        if (g.history_selection == expr_sel) {
            fill_rect(display, 0, y - 1, kLcdWidth, expr_box.h + 2, kLightGray);
        }
        draw_expression(display, 4, y, g.history[i].expression, -1, kBlack, g.history_selection == expr_sel ? kLightGray : kWhite);
        y += expr_box.h + 3;
        if (g.history_selection == ans_sel) {
            fill_rect(display, 0, y - 1, kLcdWidth, ans_box.h + 2, kLightGray);
        }
        const int answer_w = ans_box.w;
        draw_expression(display,
                        kLcdWidth - 4 - answer_w,
                        y,
                        g.history[i].result,
                        -1,
                        g.history[i].error ? kRed : kBlue,
                        g.history_selection == ans_sel ? kLightGray : kWhite);
        y += ans_box.h + 3;
        if (i != 0) {
            dotted_line(display, y);
        }
        y += 4;
    }
    draw_input_line(display, ">", g.home_expr, g.home_cursor, g.home_expr_scroll_x);
}

void draw_axes(Display& display) {
    int sx = 0;
    int sy = 0;
    const Color grid = rgb565(210, 210, 210);
    for (int i = -10; i <= 10; ++i) {
        if (graph_to_screen(g.window, static_cast<CalcReal>(i), g.window.ymin, sx, sy)) {
            draw_line(display, sx, kTitleH, sx, kLcdHeight - 1, grid);
        }
        if (graph_to_screen(g.window, g.window.xmin, static_cast<CalcReal>(i), sx, sy)) {
            draw_line(display, 0, sy, kLcdWidth - 1, sy, grid);
        }
    }
    int ax0 = 0;
    int ay0 = 0;
    int ax1 = 0;
    int ay1 = 0;
    if (graph_to_screen(g.window, 0.0, g.window.ymin, ax0, ay0) &&
        graph_to_screen(g.window, 0.0, g.window.ymax, ax1, ay1)) {
        draw_line(display, ax0, kTitleH, ax0, kLcdHeight - 1, kBlack);
    }
    if (graph_to_screen(g.window, g.window.xmin, 0.0, ax0, ay0) &&
        graph_to_screen(g.window, g.window.xmax, 0.0, ax1, ay1)) {
        draw_line(display, 0, ay0, kLcdWidth - 1, ay0, kBlack);
    }
}

void render_graph(Display& display) {
    clear(display, kWhite);
    title(display, "GRAPH");
    draw_axes(display);

    if (g.y_len == 0) {
        draw_text_scaled(display, 8, 22, "Y1 EMPTY - PRESS Y=", kTextScale, kRed, kWhite);
        return;
    }

    bool have_prev = false;
    int prev_x = 0;
    int prev_y = 0;
    for (int px = 0; px < kLcdWidth; ++px) {
        const CalcReal x = screen_to_graph_x(g.window, px);
        EvalResult result = evaluate_expression_with_x_readonly(g.y_expr, g.eval, x);
        int sx = 0;
        int sy = 0;
        if (result.ok && graph_to_screen(g.window, x, result.value, sx, sy)) {
            if (have_prev) {
                draw_line(display, prev_x, prev_y, sx, sy, kRed);
            }
            prev_x = sx;
            prev_y = sy;
            have_prev = true;
        } else {
            have_prev = false;
        }
    }
    draw_expression(display, 4, 224, g.y_expr, -1, kBlue, kWhite);
}

void render_y(Display& display) {
    clear(display, kWhite);
    title(display, "Y=");
    draw_text_scaled(display, 4, 24, "Y1=", kTextScale, kBlack, kWhite);
    ensure_cursor_visible(g.y_expr, g.y_len, g.y_cursor, kLcdWidth - 38, g.y_expr_scroll_x);
    draw_expression(display, 34 - g.y_expr_scroll_x, 24, g.y_expr, g.y_cursor, kBlack, kWhite);
    fill_rect(display, 0, 20, 34, 28, kWhite);
    draw_text_scaled(display, 4, 24, "Y1=", kTextScale, kBlack, kWhite);
    draw_text_scaled(display, 4, 210, "ENTER OR GRAPH TO PLOT", kTextScale, kBlue, kWhite);
}

void render_window(Display& display) {
    clear(display, kWhite);
    title(display, "WINDOW");
    const char* labels[4] = {"XMIN", "XMAX", "YMIN", "YMAX"};
    const CalcReal values[4] = {g.window.xmin, g.window.xmax, g.window.ymin, g.window.ymax};
    for (int i = 0; i < 4; ++i) {
        const int y = 24 + i * 20;
        if (g.window_selection == i) {
            fill_rect(display, 2, y - 2, kLcdWidth - 4, 15, kLightGray);
        }
        char line[48]{};
        char value[24]{};
        format_value(values[i], value, sizeof(value));
        std::size_t pos = 0;
        append_string(line, sizeof(line), pos, labels[i]);
        append_char(line, sizeof(line), pos, '=');
        append_string(line, sizeof(line), pos, value);
        draw_text_scaled(display, 8, y, line, kTextScale, kBlack, g.window_selection == i ? kLightGray : kWhite);
    }
    draw_text_scaled(display, 4, 210, "ARROWS SELECT  +/- EDIT", kTextScale, kBlue, kWhite);
}

void render_settings(Display& display) {
    clear(display, kWhite);
    title(display, "SETTINGS");
    draw_text_scaled(display, 8, 24, "ANGLE: RADIANS", kTextScale, kBlack, kWhite);
#if defined(CALC_USE_FLOAT) && CALC_USE_FLOAT
    draw_text_scaled(display, 8, 40, "REAL: FLOAT", kTextScale, kBlack, kWhite);
#else
    draw_text_scaled(display, 8, 40, "REAL: DOUBLE", kTextScale, kBlack, kWhite);
#endif
    draw_text_scaled(display, 8, 56, "DISPLAY: RGB565 320X240", kTextScale, kBlack, kWhite);
    draw_text_scaled(display, 8, 80, "FIXED BUFFERS ENABLED", kTextScale, kGreen, kWhite);
}

void render_about(Display& display) {
    clear(display, kWhite);
    title(display, "ABOUT");
    draw_text_scaled(display, 8, 24, "RP2350 CALC MVP", kTextScale, kBlack, kWhite);
    draw_text_scaled(display, 8, 40, "PORTABLE CORE", kTextScale, kBlack, kWhite);
    draw_text_scaled(display, 8, 56, "DESKTOP EMULATOR HOST", kTextScale, kBlack, kWhite);
}

}  // namespace

void calc_init(Platform& platform) {
    g = CalculatorState{};
    g.platform = &platform;
    g.screen = Screen::Home;
    eval_context_init(g.eval);
    g.window = {real(-10.0), real(10.0), real(-6.55), real(6.55)};
    copy_string(g.y_expr, sizeof(g.y_expr), "sin(X)");
    g.y_len = static_cast<int>(std::strlen(g.y_expr));
    g.y_cursor = g.y_len;
    reset_home_expression();
    g.cursor_on = true;
    g.history_selection = -1;
    g.edit_cursor_before_history = 0;
    g.last_tick_ms = millis();
}

void calc_tick() {
    const std::uint32_t now = millis();
    if (now - g.last_tick_ms >= 500u) {
        g.cursor_on = !g.cursor_on;
        g.last_tick_ms = now;
    }
}

void calc_key_down(Key key) {
    key = translate_layer_key(key);
    if (key == Key::None) {
        return;
    }
    g.last_key = key;
    handle_global_key(key);
    switch (g.screen) {
        case Screen::Home: handle_home_key(key); break;
        case Screen::YEquals: handle_y_key(key); break;
        case Screen::Graph: handle_graph_key(key); break;
        case Screen::Window: handle_window_key(key); break;
        case Screen::Settings:
        case Screen::About:
            if (key == Key::Enter || key == Key::Clear) {
                g.screen = Screen::Home;
            }
            break;
    }
}

void calc_key_up(Key) {}

void calc_render() {
    if (g.platform == nullptr || g.platform->display.pixels == nullptr) {
        return;
    }
    Display& display = g.platform->display;
    switch (g.screen) {
        case Screen::Home: render_home(display); break;
        case Screen::Graph: render_graph(display); break;
        case Screen::YEquals: render_y(display); break;
        case Screen::Window: render_window(display); break;
        case Screen::Settings: render_settings(display); break;
        case Screen::About: render_about(display); break;
    }
}

Screen calc_screen() {
    return g.screen;
}

bool calc_debug_layout_expression(const char* expression, LayoutDebugInfo& info) {
    if (expression == nullptr) {
        expression = "";
    }
    const int len = static_cast<int>(std::strlen(expression));
    LayoutContext layout{};
    const ExprBox box = measure_expression_range_impl(expression, 0, len, false, &layout, CursorRegion::Main);
    emit_anchors_range(layout, 0, box.ascent, expression, 0, len, false, CursorRegion::Main);
    info.width = box.w;
    info.ascent = box.ascent;
    info.descent = box.descent;
    info.height = box.h;
    info.anchor_count = layout.anchor_count;
    info.overflow = layout.overflow;
    return !layout.overflow;
}

}  // namespace calc
