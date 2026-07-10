#include "calc/calculator.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
constexpr int kMaxYEquations = 10;
constexpr int kWindowRows = 7;
constexpr int kWindowNumericRows = 6;
constexpr int kWindowValueCapacity = 24;
constexpr int kTableColumns = kMaxYEquations + 1;
constexpr int kTableVisibleYColumns = 4;
constexpr int kTableVisibleRows = 12;
constexpr int kTableManualCapacity = 32;
constexpr int kMaxTracePoiLabels = 4;
constexpr int kMathTabCount = 5;
constexpr int kMathVisibleRows = 12;
constexpr int kSolverValueRows = 3;
constexpr int kPlotTop = kTitleH;
constexpr int kPlotBottom = kLcdHeight - 1;
constexpr char kAutoOpenParen = '\x1c';
constexpr char kAutoCloseParen = '\x1d';

constexpr CalcReal real(double value) {
    return static_cast<CalcReal>(value);
}

struct HistoryEntry {
    char expression[kExpressionCapacity];
    char result[32];
    bool error;
};

struct ManualTableEntry {
    int row;
    char text[kWindowValueCapacity];
    int len;
    int cursor;
    bool used;
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
    char y_expr[kMaxYEquations][kExpressionCapacity];
    int y_len[kMaxYEquations];
    int y_cursor[kMaxYEquations];
    int y_expr_scroll_x[kMaxYEquations];
    int y_selection;
    int y_first_row;
    char window_edit[kWindowNumericRows][kWindowValueCapacity];
    int window_edit_len[kWindowNumericRows];
    int window_edit_cursor[kWindowNumericRows];
    bool table_auto;
    CalcReal table_start;
    CalcReal table_step;
    int table_row;
    int table_col;
    int table_first_row;
    int table_first_col;
    ManualTableEntry table_manual[kTableManualCapacity];
    HistoryEntry history[kHistoryCapacity];
    int history_count;
    int window_selection;
    int settings_selection;
    std::uint32_t last_tick_ms;
    bool cursor_on;
    Key last_key;
    bool second_active;
    bool alpha_active;
    bool render_dirty;
    bool zoom_pending;
    int history_selection;
    int edit_cursor_before_history;
    int history_first_entry;
    bool fraction_output;
    Screen math_return_screen;
    int math_tab;
    int math_row;
    int math_first_row;
    char solver_expr[kExpressionCapacity];
    int solver_len;
    int solver_cursor;
    int solver_scroll_x;
    char solver_variable;
    char solver_value[kSolverValueRows][kWindowValueCapacity];
    int solver_value_len[kSolverValueRows];
    int solver_value_cursor[kSolverValueRows];
    int solver_selection;
    char solver_status[32];
    bool trace_active;
    int trace_eq;
    CalcReal trace_x;
    int trace_grid_index;
    bool trace_poi_active;
    bool trace_poi_special;
    int trace_poi_count;
    char trace_poi_labels[kMaxTracePoiLabels][32];
    char trace_entry[kWindowValueCapacity];
    int trace_entry_len;
    int trace_entry_cursor;
};

CalculatorState g{};

constexpr std::int16_t kGraphSampleMissing = 32767;
constexpr std::int16_t kGraphSampleInvalid = 32766;
std::int16_t g_graph_sample_y[kMaxYEquations][kLcdWidth]{};
std::uint32_t g_graph_sample_fingerprint = 0;
bool g_graph_sample_cache_valid = false;
int g_last_graph_evaluations = 0;

enum class MathMenuAction : std::uint8_t {
    InsertRaw,
    InsertFunction,
    InsertFraction,
    InsertMixedFraction,
    InsertNthRoot,
    InsertCubeRoot,
    InsertCube,
    FracOutput,
    DecOutput,
    ToggleFracDecimal,
    OpenSolver,
    NoOp
};

struct MathMenuItem {
    char direct;
    const char* label;
    MathMenuAction action;
    const char* text;
    int cursor_delta;
};

struct MathMenuTab {
    const char* title;
    const MathMenuItem* items;
    int count;
};

constexpr MathMenuItem kMathItems[] = {
    {'1', ">Frac", MathMenuAction::FracOutput, nullptr, 0},
    {'2', ">Dec", MathMenuAction::DecOutput, nullptr, 0},
    {'3', "x^3", MathMenuAction::InsertCube, nullptr, 0},
    {'4', "3root(", MathMenuAction::InsertCubeRoot, nullptr, 0},
    {'5', "xroot", MathMenuAction::InsertNthRoot, nullptr, 0},
    {'6', "fMin(", MathMenuAction::InsertRaw, "fMin(,,,)", -4},
    {'7', "fMax(", MathMenuAction::InsertRaw, "fMax(,,,)", -4},
    {'8', "nDeriv(", MathMenuAction::InsertRaw, "nDeriv(,,)", -3},
    {'9', "fnInt(", MathMenuAction::InsertRaw, "fnInt(,,,)", -4},
    {'0', "sum(", MathMenuAction::InsertRaw, "sum(,,,)", -4},
    {'A', "logBASE(", MathMenuAction::InsertRaw, "logBASE(,)", -2},
    {'B', "piecewise(", MathMenuAction::InsertRaw, "piecewise(,,,)", -4},
    {'C', "Numeric Solver...", MathMenuAction::OpenSolver, nullptr, 0},
};

constexpr MathMenuItem kNumItems[] = {
    {'1', "abs(", MathMenuAction::InsertFunction, "abs(", 0},
    {'2', "round(", MathMenuAction::InsertRaw, "round(,)", -2},
    {'3', "iPart(", MathMenuAction::InsertFunction, "iPart(", 0},
    {'4', "fPart(", MathMenuAction::InsertFunction, "fPart(", 0},
    {'5', "int(", MathMenuAction::InsertFunction, "int(", 0},
    {'6', "min(", MathMenuAction::InsertRaw, "min(,)", -2},
    {'7', "max(", MathMenuAction::InsertRaw, "max(,)", -2},
    {'8', "lcm(", MathMenuAction::InsertRaw, "lcm(,)", -2},
    {'9', "gcd(", MathMenuAction::InsertRaw, "gcd(,)", -2},
    {'0', "remainder(", MathMenuAction::InsertRaw, "remainder(,)", -2},
    {'A', ">n/d<>Un/d", MathMenuAction::NoOp, nullptr, 0},
    {'B', ">F<>D", MathMenuAction::ToggleFracDecimal, nullptr, 0},
    {'C', "Un/d", MathMenuAction::InsertMixedFraction, nullptr, 0},
    {'D', "n/d", MathMenuAction::InsertFraction, nullptr, 0},
};

constexpr MathMenuItem kCmplxItems[] = {
    {'1', "conj(", MathMenuAction::InsertFunction, "conj(", 0},
    {'2', "real(", MathMenuAction::InsertFunction, "real(", 0},
    {'3', "imag(", MathMenuAction::InsertFunction, "imag(", 0},
    {'4', "angle(", MathMenuAction::InsertFunction, "angle(", 0},
    {'5', "abs(", MathMenuAction::InsertFunction, "abs(", 0},
    {'6', ">Rect", MathMenuAction::NoOp, nullptr, 0},
    {'7', ">Polar", MathMenuAction::NoOp, nullptr, 0},
};

constexpr MathMenuItem kPrbItems[] = {
    {'1', "rand", MathMenuAction::InsertRaw, "rand", 0},
    {'2', "nPr", MathMenuAction::InsertRaw, "nPr(,)", -2},
    {'3', "nCr", MathMenuAction::InsertRaw, "nCr(,)", -2},
    {'4', "!", MathMenuAction::InsertRaw, "!", 0},
    {'5', "randInt(", MathMenuAction::InsertRaw, "randInt(,)", -2},
    {'6', "randNorm(", MathMenuAction::InsertRaw, "randNorm(,)", -2},
    {'7', "randBin(", MathMenuAction::InsertRaw, "randBin(,)", -2},
    {'8', "randIntNoRep(", MathMenuAction::InsertRaw, "randIntNoRep(,)", -2},
};

constexpr MathMenuItem kFracItems[] = {
    {'1', "n/d", MathMenuAction::InsertFraction, nullptr, 0},
    {'2', "Un/d", MathMenuAction::InsertMixedFraction, nullptr, 0},
    {'3', ">F<>D", MathMenuAction::ToggleFracDecimal, nullptr, 0},
    {'4', ">n/d<>Un/d", MathMenuAction::NoOp, nullptr, 0},
};

constexpr MathMenuTab kMathTabs[kMathTabCount] = {
    {"MATH", kMathItems, static_cast<int>(sizeof(kMathItems) / sizeof(kMathItems[0]))},
    {"NUM", kNumItems, static_cast<int>(sizeof(kNumItems) / sizeof(kNumItems[0]))},
    {"CMPLX", kCmplxItems, static_cast<int>(sizeof(kCmplxItems) / sizeof(kCmplxItems[0]))},
    {"PRB", kPrbItems, static_cast<int>(sizeof(kPrbItems) / sizeof(kPrbItems[0]))},
    {"FRAC", kFracItems, static_cast<int>(sizeof(kFracItems) / sizeof(kFracItems[0]))},
};

struct AtomicRenderPrefix {
    const char* source;
    const char* label;
};

constexpr AtomicRenderPrefix kAtomicRenderPrefixes[] = {
    {"abs(", "ABS("},
    {"round(", "ROUND("},
    {"iPart(", "IPART("},
    {"fPart(", "FPART("},
    {"int(", "INT("},
    {"min(", "MIN("},
    {"max(", "MAX("},
    {"lcm(", "LCM("},
    {"gcd(", "GCD("},
    {"remainder(", "REMAINDER("},
    {"logBASE(", "LOGBASE("},
    {"conj(", "CONJ("},
    {"real(", "REAL("},
    {"imag(", "IMAG("},
    {"angle(", "ANGLE("},
    {"randInt(", "RANDINT("},
    {"randNorm(", "RANDNORM("},
    {"randBin(", "RANDBIN("},
    {"randIntNoRep(", "RANDINTNOREP("},
    {"nPr(", "NPR("},
    {"nCr(", "NCR("},
    {"fMin(", "FMIN("},
    {"fMax(", "FMAX("},
    {"nDeriv(", "NDERIV("},
    {"fnInt(", "FNINT("},
    {"sum(", "SUM("},
    {"piecewise(", "PIECEWISE("},
};

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
    g.history_first_entry = 0;
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

bool is_open_paren(char ch) {
    return ch == '(' || ch == kAutoOpenParen;
}

bool is_close_paren(char ch) {
    return ch == ')' || ch == kAutoCloseParen;
}

char display_char(char ch) {
    if (ch == kAutoOpenParen) {
        return '(';
    }
    if (ch == kAutoCloseParen) {
        return ')';
    }
    return ch;
}

bool atomic_text_at(const char* text, int pos, int end, const char* token) {
    const int len = static_cast<int>(std::strlen(token));
    return pos >= 0 && pos + len <= end && std::strncmp(text + pos, token, len) == 0;
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

int find_furthest_left_auto_close(const char* buffer, int len) {
    for (int i = 0; i < len; ++i) {
        if (buffer[i] == kAutoCloseParen) {
            return i;
        }
    }
    return -1;
}

int find_furthest_right_auto_open(const char* buffer, int len) {
    for (int i = len - 1; i >= 0; --i) {
        if (buffer[i] == kAutoOpenParen) {
            return i;
        }
    }
    return -1;
}

void delete_auto_placeholder_at(char* buffer, int& len, int& cursor, int pos) {
    if (pos < 0 || pos >= len) {
        return;
    }
    delete_range(buffer, len, pos, 1);
    if (cursor > pos) {
        --cursor;
    }
}

int matching_auto_close_for_explicit_open(const char* buffer, int len, int open_pos) {
    if (open_pos < 0 || open_pos >= len || buffer[open_pos] != '(') {
        return -1;
    }
    int depth = 0;
    for (int i = open_pos; i < len; ++i) {
        if (is_open_paren(buffer[i])) {
            ++depth;
        } else if (is_close_paren(buffer[i])) {
            --depth;
            if (depth == 0) {
                return buffer[i] == kAutoCloseParen ? i : -1;
            }
        }
    }
    return -1;
}

int matching_auto_open_for_explicit_close(const char* buffer, int close_pos) {
    if (close_pos < 0 || buffer[close_pos] != ')') {
        return -1;
    }
    int depth = 0;
    for (int i = close_pos; i >= 0; --i) {
        if (is_close_paren(buffer[i])) {
            ++depth;
        } else if (is_open_paren(buffer[i])) {
            --depth;
            if (depth == 0) {
                return buffer[i] == kAutoOpenParen ? i : -1;
            }
        }
    }
    return -1;
}

void delete_explicit_paren_and_auto_pair(char* buffer, int& len, int& cursor, int paren_pos) {
    int auto_pos = -1;
    if (paren_pos >= 0 && paren_pos < len && buffer[paren_pos] == '(') {
        auto_pos = matching_auto_close_for_explicit_open(buffer, len, paren_pos);
    } else if (paren_pos >= 0 && paren_pos < len && buffer[paren_pos] == ')') {
        auto_pos = matching_auto_open_for_explicit_close(buffer, paren_pos);
    }
    if (auto_pos > paren_pos) {
        delete_auto_placeholder_at(buffer, len, cursor, auto_pos);
        delete_range(buffer, len, paren_pos, 1);
    } else {
        delete_range(buffer, len, paren_pos, 1);
        if (auto_pos >= 0) {
            delete_auto_placeholder_at(buffer, len, cursor, auto_pos);
        }
    }
    if (cursor > paren_pos) {
        --cursor;
    }
}

int unmatched_close_count(const char* buffer, int len) {
    int depth = 0;
    int unmatched = 0;
    for (int i = 0; i < len; ++i) {
        if (is_open_paren(buffer[i])) {
            ++depth;
        } else if (is_close_paren(buffer[i])) {
            if (depth > 0) {
                --depth;
            } else {
                ++unmatched;
            }
        }
    }
    return unmatched;
}

bool insert_auto_open_at_start(char* buffer, int& len, int& cursor) {
    int start = 0;
    if (!insert_text(buffer, len, start, "\x1c")) {
        return false;
    }
    if (cursor >= 0) {
        ++cursor;
    }
    return true;
}

bool insert_open_paren_auto(char* buffer, int& len, int& cursor) {
    if (cursor > 0 && buffer[cursor - 1] == kAutoOpenParen) {
        buffer[cursor - 1] = '(';
        return true;
    }
    const int replaced = find_furthest_right_auto_open(buffer, len);
    if (replaced >= 0) {
        if (!insert_text(buffer, len, cursor, "(")) {
            return false;
        }
        delete_auto_placeholder_at(buffer, len, cursor, replaced);
        return true;
    }
    if (!insert_text(buffer, len, cursor, "(")) {
        return false;
    }
    const int inner_cursor = cursor;
    if (!insert_text(buffer, len, cursor, "\x1d")) {
        return true;
    }
    cursor = inner_cursor;
    return true;
}

bool insert_close_paren_auto(char* buffer, int& len, int& cursor) {
    if (cursor < len && buffer[cursor] == kAutoCloseParen) {
        buffer[cursor] = ')';
        ++cursor;
        return true;
    }
    if (!insert_text(buffer, len, cursor, ")")) {
        return false;
    }
    const int replaced = find_furthest_left_auto_close(buffer, len);
    if (replaced >= 0) {
        delete_auto_placeholder_at(buffer, len, cursor, replaced);
        return true;
    }
    while (unmatched_close_count(buffer, len) > 0) {
        if (!insert_auto_open_at_start(buffer, len, cursor)) {
            return true;
        }
    }
    return true;
}

bool insert_function_open_auto(char* buffer, int& len, int& cursor, const char* text) {
    if (!insert_text(buffer, len, cursor, text)) {
        return false;
    }
    const int inner_cursor = cursor;
    if (!insert_text(buffer, len, cursor, "\x1d")) {
        return true;
    }
    cursor = inner_cursor;
    return true;
}

void sanitize_expression(const char* source, char* out, std::size_t out_size) {
    if (out == nullptr || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    std::size_t pos = 0;
    if (source == nullptr) {
        return;
    }
    for (const char* p = source; *p != '\0'; ++p) {
        append_char(out, out_size, pos, display_char(*p));
    }
}

void append_or_ans(char* buffer, int& len, int& cursor, const char* op_text) {
    if (cursor == 0) {
        insert_text(buffer, len, cursor, "Ans");
    }
    insert_text(buffer, len, cursor, op_text);
}

void insert_power_template(char* buffer, int& len, int& cursor) {
    if (cursor == 0) {
        insert_text(buffer, len, cursor, "Ans");
    }
    if (insert_text(buffer, len, cursor, "^()")) {
        --cursor;
    }
}

void insert_power_value(char* buffer, int& len, int& cursor, const char* value) {
    insert_power_template(buffer, len, cursor);
    if (!insert_text(buffer, len, cursor, value)) {
        return;
    }
    if (cursor < len && buffer[cursor] == ')') {
        ++cursor;
    }
}

void insert_base_power(char* buffer, int& len, int& cursor, const char* base) {
    if (!insert_text(buffer, len, cursor, base)) {
        return;
    }
    insert_power_template(buffer, len, cursor);
}

void insert_fraction(char* buffer, int& len, int& cursor) {
    if (insert_text(buffer, len, cursor, "()/()")) {
        cursor -= 4;
        if (cursor < 1) {
            cursor = 1;
        }
    }
}

void insert_nth_root(char* buffer, int& len, int& cursor) {
    if (insert_text(buffer, len, cursor, "root()()")) {
        cursor -= 4;
        if (cursor < 5) {
            cursor = 5;
        }
    }
}

void backspace(char* buffer, int& len, int& cursor) {
    if (cursor <= 0 || len <= 0) {
        return;
    }
    if (buffer[cursor - 1] == kAutoOpenParen || buffer[cursor - 1] == kAutoCloseParen) {
        --cursor;
        return;
    }
    const char* atomic_functions[] = {"sin(",
                                      "cos(",
                                      "tan(",
                                      "asin(",
                                      "acos(",
                                      "atan(",
                                      "abs(",
                                      "round(",
                                      "iPart(",
                                      "fPart(",
                                      "int(",
                                      "min(",
                                      "max(",
                                      "lcm(",
                                      "gcd(",
                                      "remainder(",
                                      "logBASE(",
                                      "conj(",
                                      "real(",
                                      "imag(",
                                      "angle(",
                                      "randInt(",
                                      "randNorm(",
                                      "randBin(",
                                      "randIntNoRep(",
                                      "nPr(",
                                      "nCr(",
                                      "fMin(",
                                      "fMax(",
                                      "nDeriv(",
                                      "fnInt(",
                                      "sum(",
                                      "piecewise(",
                                      "Ans"};
    for (const char* name : atomic_functions) {
        const int name_len = static_cast<int>(std::strlen(name));
        if (cursor >= name_len && std::strncmp(buffer + cursor - name_len, name, name_len) == 0) {
            const int auto_close = matching_auto_close_for_explicit_open(buffer, len, cursor - 1);
            if (auto_close >= 0) {
                delete_auto_placeholder_at(buffer, len, cursor, auto_close);
            }
            delete_range(buffer, len, cursor - name_len, name_len);
            cursor -= name_len;
            return;
        }
    }
    if (buffer[cursor - 1] == '(' || buffer[cursor - 1] == ')') {
        delete_explicit_paren_and_auto_pair(buffer, len, cursor, cursor - 1);
        return;
    }
    for (int i = cursor - 1; i < len; ++i) {
        buffer[i] = buffer[i + 1];
    }
    --cursor;
    --len;
}

int fraction_end_at(const char* expr, int pos, int end, int& num_start, int& num_end, int& den_start, int& den_end);
int nth_root_end_at(const char* expr, int pos, int end, int& index_start, int& index_end, int& radicand_start, int& radicand_end);
void exponent_range(const char* expr, int caret, int end, int& visual_start, int& visual_end, int& source_end);
bool move_cursor_horizontal(const char* buffer, int len, int& cursor, int direction);
void normalize_cursor_to_visible(const char* buffer, int len, int& cursor);
void ensure_cursor_visible(const char* expr, int len, int cursor, int visible_w, int& scroll_x);
int history_row_height(const HistoryEntry& entry);
void ensure_history_selection_visible();
void ensure_y_selection_visible();
void start_trace();
void move_trace_horizontal(int direction);
void move_trace_vertical(int direction);
void clear_trace_entry();
bool insert_trace_entry_key(Key key);
bool parse_trace_entry(CalcReal& value);
void jump_trace_to_x(CalcReal x);
void clear_trace_poi_labels();
void update_trace_current_poi_labels(bool special_stop);
bool evaluate_y_at(int eq, CalcReal x, CalcReal& y);
int trace_grid_index_for_x(CalcReal x);
void ensure_trace_cursor_visible();

bool delete_blank_fraction_slot(char* buffer, int& len, int& cursor) {
    int best_start = -1;
    int best_end = -1;
    int best_span = 100000;
    for (int i = 0; i < len; ++i) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(buffer, i, len, num_start, num_end, den_start, den_end);
        if (frac_end <= 0) {
            continue;
        }
        const bool in_blank_num = cursor == num_start && num_start == num_end;
        const bool in_blank_den = cursor == den_start && den_start == den_end;
        const int span = frac_end - i;
        if ((in_blank_num || in_blank_den) && span < best_span) {
            best_start = i;
            best_end = frac_end;
            best_span = span;
        }
    }
    if (best_start < 0) {
        return false;
    }
    delete_range(buffer, len, best_start, best_end - best_start);
    cursor = best_start;
    return true;
}

bool delete_blank_exponent_slot(char* buffer, int& len, int& cursor) {
    int best_start = -1;
    int best_span = 100000;
    for (int i = 0; i < len; ++i) {
        if (buffer[i] != '^') {
            continue;
        }
        int exp_start = 0;
        int exp_end = 0;
        int source_end = 0;
        exponent_range(buffer, i, len, exp_start, exp_end, source_end);
        const int span = source_end - i;
        if (cursor == exp_start && exp_start == exp_end && span < best_span) {
            best_start = i;
            best_span = span;
        }
    }
    if (best_start < 0) {
        return false;
    }
    delete_range(buffer, len, best_start, best_span);
    cursor = best_start;
    return true;
}

bool delete_nested_left(char* buffer, int& len, int& cursor) {
    int best_start = -1;
    int best_span = 100000;
    for (int i = 0; i < len; ++i) {
        int num_start = 0;
        int num_end = 0;
        int den_start = 0;
        int den_end = 0;
        const int frac_end = fraction_end_at(buffer, i, len, num_start, num_end, den_start, den_end);
        if (frac_end == cursor) {
            const int span = frac_end - i;
            if (span < best_span) {
                best_start = i;
                best_span = span;
            }
        }
    }
    for (int i = 0; i < len; ++i) {
        int index_start = 0;
        int index_end = 0;
        int radicand_start = 0;
        int radicand_end = 0;
        const int root_end = nth_root_end_at(buffer, i, len, index_start, index_end, radicand_start, radicand_end);
        if (root_end == cursor) {
            const int span = root_end - i;
            if (span < best_span) {
                best_start = i;
                best_span = span;
            }
        }
    }
    for (int i = 0; i < len; ++i) {
        if (buffer[i] != '^') {
            continue;
        }
        int exp_start = 0;
        int exp_end = 0;
        int source_end = 0;
        exponent_range(buffer, i, len, exp_start, exp_end, source_end);
        if (source_end == cursor) {
            const int span = source_end - i;
            if (span < best_span) {
                best_start = i;
                best_span = span;
            }
        }
    }
    if (best_start < 0) {
        return false;
    }
    delete_range(buffer, len, best_start, best_span);
    cursor = best_start;
    return true;
}

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

bool move_nth_root_vertical(const char* buffer, int len, int& cursor, Key key) {
    int best_cursor = cursor;
    int best_span = 100000;
    for (int i = 0; i < len; ++i) {
        int index_start = 0;
        int index_end = 0;
        int radicand_start = 0;
        int radicand_end = 0;
        const int root_end = nth_root_end_at(buffer, i, len, index_start, index_end, radicand_start, radicand_end);
        if (root_end <= 0) {
            continue;
        }
        const int span = root_end - i;
        if (key == Key::Down && cursor >= index_start && cursor <= index_end) {
            int offset = cursor - index_start;
            const int rad_len = radicand_end - radicand_start;
            if (offset > rad_len) {
                offset = rad_len;
            }
            if (span < best_span) {
                best_span = span;
                best_cursor = radicand_start + offset;
            }
        }
        if (key == Key::Up && cursor >= radicand_start && cursor <= radicand_end) {
            int offset = cursor - radicand_start;
            const int index_len = index_end - index_start;
            if (offset > index_len) {
                offset = index_len;
            }
            if (span < best_span) {
                best_span = span;
                best_cursor = index_start + offset;
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

CalcReal round_table_value(CalcReal value) {
    if (!std::isfinite(value)) {
        return value;
    }
    constexpr CalcReal scale = static_cast<CalcReal>(10000.0);
    return std::round(value * scale) / scale;
}

int scientific_exponent_chars(CalcReal value) {
    int exponent = 0;
    value = std::fabs(value);
    if (value == real(0.0)) return 0;
    while (value >= real(10.0)) {
        value /= real(10.0);
        ++exponent;
    }
    while (value < real(1.0)) {
        value *= real(10.0);
        --exponent;
    }
    int digits = 1;
    for (int magnitude = exponent < 0 ? -exponent : exponent; magnitude >= 10; magnitude /= 10) ++digits;
    return 1 + (exponent < 0 ? 1 : 0) + digits;
}

void format_scientific_value(CalcReal value, char* out, std::size_t size, int precision) {
    out[0] = '\0';
    std::size_t pos = 0;
    if (value < real(0.0)) {
        append_char(out, size, pos, '-');
        value = -value;
    }
    int exponent = 0;
    while (value >= real(10.0)) {
        value /= real(10.0);
        ++exponent;
    }
    while (value > real(0.0) && value < real(1.0)) {
        value *= real(10.0);
        --exponent;
    }
    append_fixed_abs(out, size, pos, value, precision);
    append_char(out, size, pos, 'E');
    if (exponent < 0) {
        append_char(out, size, pos, '-');
        exponent = -exponent;
    }
    append_uint(out, size, pos, static_cast<unsigned int>(exponent));
}

void format_table_value(CalcReal value, char* out, std::size_t size, int max_chars) {
    if (out == nullptr || size == 0u) {
        return;
    }
    out[0] = '\0';
    if (!std::isfinite(value)) {
        copy_string(out, size, "ERR");
        return;
    }
    if (std::fabs(value) < real(0.00000000005)) {
        value = real(0.0);
    }
    if (max_chars < 4) {
        max_chars = 4;
    }
    const CalcReal abs_value = value < real(0.0) ? -value : value;
    if (abs_value > real(0.0) && (abs_value < real(0.001) || abs_value > real(100000.0))) {
        const int sign_chars = value < real(0.0) ? 1 : 0;
        const int exponent_chars = scientific_exponent_chars(value);
        int precision = max_chars - sign_chars - 1 - exponent_chars;
        if (precision > 0) {
            --precision;
        }
        if (precision < 0) {
            precision = 0;
        }
        format_scientific_value(value, out, size, precision);
        while (static_cast<int>(std::strlen(out)) > max_chars && precision > 0) {
            --precision;
            format_scientific_value(value, out, size, precision);
        }
        return;
    }
    value = round_table_value(value);
    const int sign_chars = value < real(0.0) ? 1 : 0;
    int whole_digits = 1;
    if (abs_value >= real(1.0)) {
        whole_digits = 0;
        CalcReal whole = std::floor(abs_value);
        while (whole >= real(1.0)) {
            whole /= real(10.0);
            ++whole_digits;
        }
    }
    int decimals = max_chars - sign_chars - whole_digits - 1;
    if (decimals < 0) {
        decimals = 0;
    }
    if (decimals > 4) {
        decimals = 4;
    }
    std::size_t pos = 0;
    if (value < real(0.0)) {
        append_char(out, size, pos, '-');
        value = -value;
    }
    append_fixed_abs(out, size, pos, value, decimals);
}

Color graph_color(int index) {
    static constexpr Color colors[kMaxYEquations] = {
        0xf800, 0x001f, 0x07e0, 0xf81f, 0xffe0,
        0x07ff, 0xfd20, 0x780f, 0x03ef, 0x8410
    };
    if (index < 0 || index >= kMaxYEquations) {
        return kRed;
    }
    return colors[index];
}

CalcReal default_window_value(int index) {
    switch (index) {
        case 0:
        case 1:
        case 2:
        case 3: return index == 0 || index == 2 ? real(-10.0) : real(10.0);
        case 4: return real(0.0);
        case 5: return real(1.0);
        default: return real(0.0);
    }
}

void sync_window_edit_from_values() {
    CalcReal values[kWindowNumericRows] = {
        g.window.xmin, g.window.xmax, g.window.ymin, g.window.ymax, g.table_start, g.table_step
    };
    for (int i = 0; i < kWindowNumericRows; ++i) {
        format_value(values[i], g.window_edit[i], sizeof(g.window_edit[i]));
        g.window_edit_len[i] = static_cast<int>(std::strlen(g.window_edit[i]));
        g.window_edit_cursor[i] = g.window_edit_len[i];
    }
}

bool parse_window_value(int index, CalcReal& value) {
    if (index < 0 || index >= kWindowNumericRows || g.window_edit_len[index] == 0) {
        value = default_window_value(index);
        return true;
    }
    EvalContext copy = g.eval;
    EvalResult result = evaluate_expression(g.window_edit[index], copy);
    if (!result.ok || std::fabs(result.imag) >= real(0.0000000005) || !std::isfinite(result.value)) {
        return false;
    }
    value = result.value;
    return true;
}

void commit_window_edits() {
    CalcReal values[kWindowNumericRows]{};
    for (int i = 0; i < kWindowNumericRows; ++i) {
        if (!parse_window_value(i, values[i])) {
            values[i] = default_window_value(i);
        }
    }
    g.window.xmin = values[0];
    g.window.xmax = values[1];
    g.window.ymin = values[2];
    g.window.ymax = values[3];
    g.table_start = values[4];
    g.table_step = values[5];
    if (g.window.xmax <= g.window.xmin + real(0.1)) {
        g.window.xmax = g.window.xmin + real(0.1);
        format_value(g.window.xmax, g.window_edit[1], sizeof(g.window_edit[1]));
        g.window_edit_len[1] = static_cast<int>(std::strlen(g.window_edit[1]));
        g.window_edit_cursor[1] = g.window_edit_len[1];
    }
    if (g.window.ymax <= g.window.ymin + real(0.1)) {
        g.window.ymax = g.window.ymin + real(0.1);
        format_value(g.window.ymax, g.window_edit[3], sizeof(g.window_edit[3]));
        g.window_edit_len[3] = static_cast<int>(std::strlen(g.window_edit[3]));
        g.window_edit_cursor[3] = g.window_edit_len[3];
    }
}

bool insert_window_value_key(Key key, char* buffer, int& len, int& cursor) {
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
        case Key::Negate:
        case Key::Subtract: return insert_text(buffer, len, cursor, "-");
        case Key::Add: return insert_text(buffer, len, cursor, "+");
        case Key::Divide: return insert_text(buffer, len, cursor, "/");
        case Key::LParen: return insert_text(buffer, len, cursor, "(");
        case Key::RParen: return insert_text(buffer, len, cursor, ")");
        case Key::ConstE: return insert_text(buffer, len, cursor, "E");
        default: return false;
    }
}

bool near_zero(CalcReal value) {
    return std::fabs(value) < real(0.0000000005);
}

bool near_one(CalcReal value) {
    return std::fabs(std::fabs(value) - real(1.0)) < real(0.0000000005);
}

void append_imaginary_part(char* out, std::size_t size, std::size_t& pos, CalcReal imag_part, bool include_plus) {
    if (imag_part < real(0.0)) {
        append_char(out, size, pos, '-');
    } else if (include_plus) {
        append_char(out, size, pos, '+');
    }
    if (!near_one(imag_part)) {
        append_fixed_abs(out, size, pos, imag_part, 6);
    }
    append_char(out, size, pos, 'i');
}

void format_complex_value(CalcReal real_part, CalcReal imag_part, char* out, std::size_t size) {
    if (near_zero(imag_part)) {
        format_value(real_part, out, size);
        return;
    }
    if (near_zero(real_part)) {
        std::size_t pos = 0;
        append_imaginary_part(out, size, pos, imag_part, false);
        return;
    }

    format_value(real_part, out, size);
    std::size_t pos = std::strlen(out);
    append_imaginary_part(out, size, pos, imag_part, true);
}

long long abs_ll(long long value) {
    return value < 0 ? -value : value;
}

long long gcd_ll(long long a, long long b) {
    a = abs_ll(a);
    b = abs_ll(b);
    while (b != 0) {
        const long long r = a % b;
        a = b;
        b = r;
    }
    return a == 0 ? 1 : a;
}

bool format_fraction_approx(CalcReal value, char* out, std::size_t size) {
    constexpr int kMaxDen = 10000;
    if (!std::isfinite(value)) {
        return false;
    }
    long long best_num = 0;
    long long best_den = 1;
    CalcReal best_err = std::fabs(value);
    for (int den = 1; den <= kMaxDen; ++den) {
        const CalcReal scaled = value * static_cast<CalcReal>(den);
        const long long num = static_cast<long long>(scaled >= real(0.0) ? std::floor(scaled + real(0.5))
                                                                         : std::ceil(scaled - real(0.5)));
        const CalcReal err = std::fabs(value - static_cast<CalcReal>(num) / static_cast<CalcReal>(den));
        if (err < best_err) {
            best_err = err;
            best_num = num;
            best_den = den;
            if (err < real(0.0000000005)) {
                break;
            }
        }
    }
    const long long div = gcd_ll(best_num, best_den);
    best_num /= div;
    best_den /= div;
    if (best_den == 1) {
        std::snprintf(out, size, "%lld", best_num);
    } else {
        std::snprintf(out, size, "(%lld)/(%lld)", best_num, best_den);
    }
    return true;
}

bool format_surd_approx(CalcReal value, char* out, std::size_t size) {
    if (!std::isfinite(value) || near_zero(value)) {
        return false;
    }
    const bool negative = value < real(0.0);
    const CalcReal target = negative ? -value : value;
    int best_coeff = 0;
    int best_rad = 0;
    int best_den = 1;
    CalcReal best_err = real(0.00000001);
    for (int rad = 2; rad <= 32; ++rad) {
        const int root = static_cast<int>(std::sqrt(static_cast<CalcReal>(rad)) + real(0.5));
        if (root * root == rad) {
            continue;
        }
        const CalcReal sqrt_rad = std::sqrt(static_cast<CalcReal>(rad));
        for (int den = 1; den <= 16; ++den) {
            for (int coeff = 1; coeff <= 16; ++coeff) {
                const CalcReal candidate = static_cast<CalcReal>(coeff) * sqrt_rad / static_cast<CalcReal>(den);
                const CalcReal err = std::fabs(target - candidate);
                if (err < best_err) {
                    best_err = err;
                    best_coeff = coeff;
                    best_rad = rad;
                    best_den = den;
                }
            }
        }
    }
    if (best_coeff == 0) {
        return false;
    }
    const long long div = gcd_ll(best_coeff, best_den);
    best_coeff = static_cast<int>(best_coeff / div);
    best_den = static_cast<int>(best_den / div);
    const char* sign = negative ? "-" : "";
    if (best_den == 1) {
        if (best_coeff == 1) {
            std::snprintf(out, size, "%ssqrt(%d)", sign, best_rad);
        } else {
            std::snprintf(out, size, "%s%d*sqrt(%d)", sign, best_coeff, best_rad);
        }
    } else {
        if (best_coeff == 1) {
            std::snprintf(out, size, "%s(sqrt(%d))/(%d)", sign, best_rad, best_den);
        } else {
            std::snprintf(out, size, "%s(%d*sqrt(%d))/(%d)", sign, best_coeff, best_rad, best_den);
        }
    }
    return true;
}

void format_fraction_mode_part(CalcReal value, char* out, std::size_t size) {
    if (!format_surd_approx(value, out, size) && !format_fraction_approx(value, out, size)) {
        format_value(value, out, size);
    }
}

void append_fraction_mode_imaginary(char* out, std::size_t size, std::size_t& pos, CalcReal imag_part, bool include_plus) {
    if (imag_part < real(0.0)) {
        append_char(out, size, pos, '-');
    } else if (include_plus) {
        append_char(out, size, pos, '+');
    }
    const CalcReal magnitude = imag_part < real(0.0) ? -imag_part : imag_part;
    if (!near_one(magnitude)) {
        char coeff[32]{};
        format_fraction_mode_part(magnitude, coeff, sizeof(coeff));
        append_string(out, size, pos, coeff);
    }
    append_char(out, size, pos, 'i');
}

void format_fraction_mode_result(const EvalResult& result, char* out, std::size_t size) {
    if (near_zero(result.imag)) {
        format_fraction_mode_part(result.value, out, size);
        return;
    }
    if (near_zero(result.value)) {
        std::size_t pos = 0;
        append_fraction_mode_imaginary(out, size, pos, result.imag, false);
        return;
    }
    char real_text[32]{};
    format_fraction_mode_part(result.value, real_text, sizeof(real_text));
    copy_string(out, size, real_text);
    std::size_t pos = std::strlen(out);
    append_fraction_mode_imaginary(out, size, pos, result.imag, true);
}

bool whole_fraction_expression(const char* source) {
    int num_start = 0;
    int num_end = 0;
    int den_start = 0;
    int den_end = 0;
    const int len = static_cast<int>(std::strlen(source));
    return fraction_end_at(source, 0, len, num_start, num_end, den_start, den_end) == len;
}

void toggle_fraction_decimal() {
    const char* source = g.home_len > 0 ? g.home_expr : (g.history_count > 0 ? g.history[0].result : nullptr);
    if (source == nullptr || source[0] == '\0') {
        return;
    }
    EvalContext copy = g.eval;
    EvalResult result = evaluate_expression(source, copy);
    if (!result.ok || !near_zero(result.imag)) {
        return;
    }
    char text[kExpressionCapacity]{};
    if (whole_fraction_expression(source) || std::strstr(source, "sqrt(") != nullptr) {
        format_value(result.value, text, sizeof(text));
    } else if (format_surd_approx(result.value, text, sizeof(text))) {
        // Prefer compact exact-looking radicals for common trig values.
    } else if (!format_fraction_approx(result.value, text, sizeof(text))) {
        return;
    }
    copy_string(g.home_expr, sizeof(g.home_expr), text);
    g.home_len = static_cast<int>(std::strlen(g.home_expr));
    g.home_cursor = g.home_len;
    g.home_expr_scroll_x = 0;
    clear_history_selection();
}

void format_result_value(const EvalResult& result, char* out, std::size_t size) {
    if (g.fraction_output) {
        format_fraction_mode_result(result, out, size);
        return;
    }
    format_complex_value(result.value, result.imag, out, size);
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

    char sanitized[kExpressionCapacity]{};
    const char* expression = g.home_expr;
    if (g.home_len == 0) {
        if (g.history_count == 0) {
            return;
        }
        expression = g.history[0].expression;
    }
    sanitize_expression(expression, sanitized, sizeof(sanitized));
    expression = sanitized;

    EvalResult result = evaluate_expression(expression, g.eval);
    char formatted[32]{};
    if (result.ok) {
        format_result_value(result, formatted, sizeof(formatted));
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
        case Key::Power: insert_power_template(buffer, len, cursor); return true;
        case Key::Square: insert_power_value(buffer, len, cursor, "2"); return true;
        case Key::NthRoot: insert_nth_root(buffer, len, cursor); return true;
        case Key::LParen: return insert_open_paren_auto(buffer, len, cursor);
        case Key::RParen: return insert_close_paren_auto(buffer, len, cursor);
        case Key::Comma: return insert_text(buffer, len, cursor, ",");
        case Key::Negate: return insert_text(buffer, len, cursor, "-");
        case Key::Store: append_or_ans(buffer, len, cursor, "->"); return true;
        case Key::Fraction: insert_fraction(buffer, len, cursor); return true;
        case Key::Equal: return insert_text(buffer, len, cursor, "=");
        case Key::Sin: return insert_function_open_auto(buffer, len, cursor, "sin(");
        case Key::Cos: return insert_function_open_auto(buffer, len, cursor, "cos(");
        case Key::Tan: return insert_function_open_auto(buffer, len, cursor, "tan(");
        case Key::ASin: return insert_function_open_auto(buffer, len, cursor, "asin(");
        case Key::ACos: return insert_function_open_auto(buffer, len, cursor, "acos(");
        case Key::ATan: return insert_function_open_auto(buffer, len, cursor, "atan(");
        case Key::Sqrt: return insert_function_open_auto(buffer, len, cursor, "sqrt(");
        case Key::Log: return insert_function_open_auto(buffer, len, cursor, "log(");
        case Key::Ln: return insert_function_open_auto(buffer, len, cursor, "ln(");
        case Key::TenPower: insert_base_power(buffer, len, cursor, "10"); return true;
        case Key::ExpPower: insert_base_power(buffer, len, cursor, "e"); return true;
        case Key::Ans: return insert_text(buffer, len, cursor, "Ans");
        case Key::Pi: return insert_text(buffer, len, cursor, "pi");
        case Key::ConstE: return insert_text(buffer, len, cursor, "e");
        case Key::X: return insert_text(buffer, len, cursor, "X");
        case Key::Imaginary: return insert_text(buffer, len, cursor, "i");
        default: break;
    }

    const char letter = key_letter(key);
    if (letter != '\0') {
        char text[2] = {letter, '\0'};
        return insert_text(buffer, len, cursor, text);
    }
    return false;
}

void set_solver_value(int row, const char* text) {
    if (row < 0 || row >= kSolverValueRows) {
        return;
    }
    copy_string(g.solver_value[row], sizeof(g.solver_value[row]), text);
    g.solver_value_len[row] = static_cast<int>(std::strlen(g.solver_value[row]));
    g.solver_value_cursor[row] = g.solver_value_len[row];
}

void init_solver_fields() {
    g.solver_variable = 'X';
    set_solver_value(0, "-10");
    set_solver_value(1, "10");
    set_solver_value(2, "0");
    g.solver_selection = 0;
    g.solver_status[0] = '\0';
}

void open_solver() {
    if (g.solver_variable < 'A' || g.solver_variable > 'Z') {
        init_solver_fields();
    }
    g.solver_selection = 0;
    g.screen = Screen::Solver;
}

void edit_expression_key(Key key, char* buffer, int& len, int& cursor) {
    if ((key == Key::Up || key == Key::Down) &&
        (move_fraction_vertical(buffer, len, cursor, key) || move_nth_root_vertical(buffer, len, cursor, key))) {
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
            if (!delete_blank_fraction_slot(buffer, len, cursor) && !delete_blank_exponent_slot(buffer, len, cursor) &&
                !delete_nested_left(buffer, len, cursor)) {
                backspace(buffer, len, cursor);
            }
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

const MathMenuTab& current_math_tab() {
    if (g.math_tab < 0) {
        g.math_tab = 0;
    }
    if (g.math_tab >= kMathTabCount) {
        g.math_tab = kMathTabCount - 1;
    }
    return kMathTabs[g.math_tab];
}

void ensure_math_selection_visible() {
    const MathMenuTab& tab = current_math_tab();
    if (g.math_row < 0) {
        g.math_row = 0;
    }
    if (g.math_row >= tab.count) {
        g.math_row = tab.count - 1;
    }
    if (g.math_first_row > g.math_row) {
        g.math_first_row = g.math_row;
    }
    if (g.math_row >= g.math_first_row + kMathVisibleRows) {
        g.math_first_row = g.math_row - kMathVisibleRows + 1;
    }
    if (g.math_first_row < 0) {
        g.math_first_row = 0;
    }
    const int max_first = tab.count > kMathVisibleRows ? tab.count - kMathVisibleRows : 0;
    if (g.math_first_row > max_first) {
        g.math_first_row = max_first;
    }
}

void open_math_menu() {
    if (g.screen != Screen::MathMenu) {
        g.math_return_screen = g.screen;
        g.math_tab = 0;
        g.math_row = 0;
        g.math_first_row = 0;
    }
    g.zoom_pending = false;
    g.screen = Screen::MathMenu;
    ensure_math_selection_visible();
}

bool math_editor(char*& buffer, int*& len, int*& cursor, int*& scroll_x) {
    if (g.math_return_screen == Screen::YEquals) {
        buffer = g.y_expr[g.y_selection];
        len = &g.y_len[g.y_selection];
        cursor = &g.y_cursor[g.y_selection];
        scroll_x = &g.y_expr_scroll_x[g.y_selection];
        return true;
    }
    buffer = g.home_expr;
    len = &g.home_len;
    cursor = &g.home_cursor;
    scroll_x = &g.home_expr_scroll_x;
    return g.math_return_screen == Screen::Home;
}

void finish_math_insert(char* buffer, int& len, int& cursor, int& scroll_x) {
    normalize_cursor(buffer, len, cursor);
    ensure_cursor_visible(buffer, len, cursor, g.math_return_screen == Screen::YEquals ? kLcdWidth - 62 : kLcdWidth - 26, scroll_x);
    g.screen = g.math_return_screen == Screen::YEquals ? Screen::YEquals : Screen::Home;
    clear_history_selection();
}

void insert_mixed_fraction(char* buffer, int& len, int& cursor) {
    if (insert_text(buffer, len, cursor, "()+()/()")) {
        cursor -= 7;
        if (cursor < 1) {
            cursor = 1;
        }
    }
}

void select_math_item(const MathMenuItem& item) {
    char* buffer = nullptr;
    int* len = nullptr;
    int* cursor = nullptr;
    int* scroll_x = nullptr;
    const bool editor_target = math_editor(buffer, len, cursor, scroll_x);

    switch (item.action) {
        case MathMenuAction::FracOutput:
            g.fraction_output = true;
            g.screen = editor_target ? g.math_return_screen : Screen::Home;
            return;
        case MathMenuAction::DecOutput:
            g.fraction_output = false;
            g.screen = editor_target ? g.math_return_screen : Screen::Home;
            return;
        case MathMenuAction::ToggleFracDecimal:
            g.screen = Screen::Home;
            toggle_fraction_decimal();
            ensure_cursor_visible(g.home_expr, g.home_len, g.home_cursor, kLcdWidth - 26, g.home_expr_scroll_x);
            return;
        case MathMenuAction::OpenSolver:
            open_solver();
            return;
        case MathMenuAction::NoOp:
            g.screen = editor_target ? g.math_return_screen : Screen::Home;
            return;
        case MathMenuAction::InsertFunction:
            insert_function_open_auto(buffer, *len, *cursor, item.text);
            break;
        case MathMenuAction::InsertRaw:
            if (insert_text(buffer, *len, *cursor, item.text)) {
                *cursor += item.cursor_delta;
            }
            break;
        case MathMenuAction::InsertFraction:
            insert_fraction(buffer, *len, *cursor);
            break;
        case MathMenuAction::InsertMixedFraction:
            insert_mixed_fraction(buffer, *len, *cursor);
            break;
        case MathMenuAction::InsertNthRoot:
            insert_nth_root(buffer, *len, *cursor);
            break;
        case MathMenuAction::InsertCubeRoot:
            if (insert_text(buffer, *len, *cursor, "root(3)()")) {
                --(*cursor);
            }
            break;
        case MathMenuAction::InsertCube:
            insert_power_value(buffer, *len, *cursor, "3");
            break;
    }

    if (!editor_target) {
        g.math_return_screen = Screen::Home;
    }
    finish_math_insert(buffer, *len, *cursor, *scroll_x);
}

char direct_key_label(Key key) {
    switch (key) {
        case Key::Digit0: return '0';
        case Key::Digit1: return '1';
        case Key::Digit2: return '2';
        case Key::Digit3: return '3';
        case Key::Digit4: return '4';
        case Key::Digit5: return '5';
        case Key::Digit6: return '6';
        case Key::Digit7: return '7';
        case Key::Digit8: return '8';
        case Key::Digit9: return '9';
        default: break;
    }
    const char letter = key_letter(key);
    if (letter >= 'A' && letter <= 'D') {
        return letter;
    }
    return '\0';
}

void select_math_direct(char direct) {
    const MathMenuTab& tab = current_math_tab();
    for (int i = 0; i < tab.count; ++i) {
        if (tab.items[i].direct == direct) {
            g.math_row = i;
            ensure_math_selection_visible();
            select_math_item(tab.items[i]);
            return;
        }
    }
}

void handle_math_menu_key(Key key) {
    if (key == Key::Clear) {
        g.screen = g.math_return_screen == Screen::MathMenu ? Screen::Home : g.math_return_screen;
        return;
    }
    if (key == Key::Left) {
        if (g.math_tab > 0) {
            --g.math_tab;
            g.math_row = 0;
            g.math_first_row = 0;
        }
        ensure_math_selection_visible();
        return;
    }
    if (key == Key::Right) {
        if (g.math_tab + 1 < kMathTabCount) {
            ++g.math_tab;
            g.math_row = 0;
            g.math_first_row = 0;
        }
        ensure_math_selection_visible();
        return;
    }
    if (key == Key::Up) {
        if (g.math_row > 0) {
            --g.math_row;
        }
        ensure_math_selection_visible();
        return;
    }
    if (key == Key::Down) {
        const MathMenuTab& tab = current_math_tab();
        if (g.math_row + 1 < tab.count) {
            ++g.math_row;
        }
        ensure_math_selection_visible();
        return;
    }
    if (key == Key::Enter) {
        const MathMenuTab& tab = current_math_tab();
        if (g.math_row >= 0 && g.math_row < tab.count) {
            select_math_item(tab.items[g.math_row]);
        }
        return;
    }
    const char direct = direct_key_label(key);
    if (direct != '\0') {
        select_math_direct(direct);
    }
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

void zoom_graph_at(CalcReal factor, CalcReal cx, CalcReal cy) {
    const CalcReal hx = (g.window.xmax - g.window.xmin) * real(0.5) * factor;
    const CalcReal hy = (g.window.ymax - g.window.ymin) * real(0.5) * factor;
    g.window.xmin = cx - hx;
    g.window.xmax = cx + hx;
    g.window.ymin = cy - hy;
    g.window.ymax = cy + hy;
}

void zoom_graph_trace_or_origin(CalcReal factor) {
    CalcReal cx = real(0.0);
    CalcReal cy = real(0.0);
    if (g.trace_active) {
        CalcReal trace_y = real(0.0);
        if (evaluate_y_at(g.trace_eq, g.trace_x, trace_y)) {
            cx = g.trace_x;
            cy = trace_y;
        }
    }
    zoom_graph_at(factor, cx, cy);
    if (g.trace_active) {
        g.trace_grid_index = trace_grid_index_for_x(g.trace_x);
        update_trace_current_poi_labels(g.trace_poi_special);
    }
}

void reset_graph_window() {
    g.window.xmin = real(-10.0);
    g.window.xmax = real(10.0);
    g.window.ymin = real(-10.0);
    g.window.ymax = real(10.0);
}

Key second_key(Key key) {
    switch (key) {
        case Key::Negate: return Key::Ans;
        case Key::Fraction: return Key::Mode;
        case Key::Square: return Key::Sqrt;
        case Key::Power: return Key::NthRoot;
        case Key::Sin: return Key::ASin;
        case Key::Cos: return Key::ACos;
        case Key::Tan: return Key::ATan;
        case Key::Log: return Key::TenPower;
        case Key::Ln: return Key::ExpPower;
        case Key::FracDecimal: return Key::Pi;
        case Key::Divide: return Key::ConstE;
        case Key::Comma: return Key::ConstE;
        case Key::Graph: return Key::Table;
        default: return key;
    }
}

Key alpha_key(Key key) {
    switch (key) {
        case Key::Math: return Key::LetterA;
        case Key::Apps: return Key::LetterB;
        case Key::Program: return Key::LetterC;
        case Key::Power: return Key::LetterD;
        case Key::Sin: return Key::LetterE;
        case Key::Cos: return Key::LetterF;
        case Key::Tan: return Key::LetterG;
        case Key::FracDecimal: return Key::LetterH;
        case Key::Square: return Key::LetterI;
        case Key::Dot: return Key::Imaginary;
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
        case Key::Home: g.zoom_pending = false; g.screen = Screen::Home; break;
        case Key::Graph: g.zoom_pending = false; g.screen = Screen::Graph; break;
        case Key::Table:
            g.zoom_pending = false;
            commit_window_edits();
            g.screen = Screen::Table;
            break;
        case Key::YEquals:
            g.zoom_pending = false;
            g.trace_active = false;
            clear_trace_poi_labels();
            clear_trace_entry();
            g.screen = Screen::YEquals;
            break;
        case Key::Window:
            g.zoom_pending = false;
            sync_window_edit_from_values();
            g.window_edit_cursor[g.window_selection] = g.window_edit_len[g.window_selection];
            g.trace_active = false;
            clear_trace_poi_labels();
            clear_trace_entry();
            g.screen = Screen::Window;
            break;
        case Key::Settings: g.zoom_pending = false; g.screen = Screen::Settings; break;
        case Key::Mode: g.zoom_pending = false; g.screen = Screen::Settings; break;
        case Key::About: g.zoom_pending = false; g.screen = Screen::About; break;
        case Key::On: g.zoom_pending = false; g.screen = Screen::Home; break;
        case Key::Math: open_math_menu(); break;
        default: break;
    }
}

void handle_home_key(Key key) {
    if (key == Key::Up || key == Key::Down) {
        if (move_fraction_vertical(g.home_expr, g.home_len, g.home_cursor, key) ||
            move_nth_root_vertical(g.home_expr, g.home_len, g.home_cursor, key)) {
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
        ensure_history_selection_visible();
        return;
    }
    if (key == Key::Enter) {
        evaluate_home();
        return;
    }
    if (key == Key::FracDecimal) {
        toggle_fraction_decimal();
        ensure_cursor_visible(g.home_expr, g.home_len, g.home_cursor, kLcdWidth - 26, g.home_expr_scroll_x);
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
    if ((key == Key::Up || key == Key::Down) &&
        (move_fraction_vertical(g.y_expr[g.y_selection], g.y_len[g.y_selection], g.y_cursor[g.y_selection], key) ||
         move_nth_root_vertical(g.y_expr[g.y_selection], g.y_len[g.y_selection], g.y_cursor[g.y_selection], key))) {
        ensure_cursor_visible(g.y_expr[g.y_selection],
                              g.y_len[g.y_selection],
                              g.y_cursor[g.y_selection],
                              kLcdWidth - 62,
                              g.y_expr_scroll_x[g.y_selection]);
        ensure_y_selection_visible();
        return;
    }
    if (key == Key::Up) {
        if (g.y_selection > 0) {
            --g.y_selection;
            g.y_cursor[g.y_selection] = g.y_len[g.y_selection];
        }
        ensure_y_selection_visible();
        return;
    }
    if (key == Key::Down) {
        if (g.y_selection + 1 < kMaxYEquations) {
            ++g.y_selection;
            g.y_cursor[g.y_selection] = g.y_len[g.y_selection];
        }
        ensure_y_selection_visible();
        return;
    }
    edit_expression_key(key, g.y_expr[g.y_selection], g.y_len[g.y_selection], g.y_cursor[g.y_selection]);
    ensure_cursor_visible(g.y_expr[g.y_selection],
                          g.y_len[g.y_selection],
                          g.y_cursor[g.y_selection],
                          kLcdWidth - 62,
                          g.y_expr_scroll_x[g.y_selection]);
    ensure_y_selection_visible();
}

void handle_graph_key(Key key) {
    if (key == Key::Zoom) {
        if (g.zoom_pending) {
            reset_graph_window();
            if (g.trace_active) {
                g.trace_grid_index = trace_grid_index_for_x(g.trace_x);
                update_trace_current_poi_labels(g.trace_poi_special);
                ensure_trace_cursor_visible();
            }
            sync_window_edit_from_values();
            g.zoom_pending = false;
        } else {
            g.zoom_pending = true;
        }
        return;
    }
    if (g.zoom_pending) {
        g.zoom_pending = false;
        if (key == Key::Add) {
            zoom_graph_trace_or_origin(real(0.75));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
        if (key == Key::Subtract) {
            zoom_graph_trace_or_origin(real(1.25));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
        if (key == Key::Left) {
            pan_graph(real(-0.35), real(0.0));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
        if (key == Key::Right) {
            pan_graph(real(0.35), real(0.0));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
        if (key == Key::Up) {
            pan_graph(real(0.0), real(0.35));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
        if (key == Key::Down) {
            pan_graph(real(0.0), real(-0.35));
            ensure_trace_cursor_visible();
            sync_window_edit_from_values();
            return;
        }
    }
    if (key == Key::Trace) {
        start_trace();
        return;
    }
    if (g.trace_active) {
        if (insert_trace_entry_key(key)) {
            clear_trace_poi_labels();
            return;
        }
        if (key == Key::Back || key == Key::Delete) {
            if (g.trace_entry_len > 0) {
                backspace(g.trace_entry, g.trace_entry_len, g.trace_entry_cursor);
            }
            return;
        }
        if (key == Key::Enter) {
            CalcReal x = real(0.0);
            if (parse_trace_entry(x)) {
                jump_trace_to_x(x);
            } else {
                clear_trace_entry();
            }
            return;
        }
        switch (key) {
            case Key::Left: move_trace_horizontal(-1); return;
            case Key::Right: move_trace_horizontal(1); return;
            case Key::Up: move_trace_vertical(1); return;
            case Key::Down: move_trace_vertical(-1); return;
            case Key::Clear:
                if (g.trace_entry_len > 0) {
                    clear_trace_entry();
                    return;
                }
                g.trace_active = false;
                clear_trace_poi_labels();
                clear_trace_entry();
                return;
            default: break;
        }
    }
    switch (key) {
        case Key::Left: pan_graph(real(-0.1), real(0.0)); ensure_trace_cursor_visible(); break;
        case Key::Right: pan_graph(real(0.1), real(0.0)); ensure_trace_cursor_visible(); break;
        case Key::Up: pan_graph(real(0.0), real(0.1)); ensure_trace_cursor_visible(); break;
        case Key::Down: pan_graph(real(0.0), real(-0.1)); ensure_trace_cursor_visible(); break;
        default: break;
    }
}

void handle_window_key(Key key) {
    switch (key) {
        case Key::Up:
            commit_window_edits();
            if (g.window_selection > 0) {
                --g.window_selection;
                if (g.window_selection < kWindowNumericRows) {
                    g.window_edit_cursor[g.window_selection] = g.window_edit_len[g.window_selection];
                }
            }
            break;
        case Key::Down:
            commit_window_edits();
            if (g.window_selection + 1 < kWindowRows) {
                ++g.window_selection;
                if (g.window_selection < kWindowNumericRows) {
                    g.window_edit_cursor[g.window_selection] = g.window_edit_len[g.window_selection];
                }
            }
            break;
        case Key::Right:
            if (g.window_selection == kWindowNumericRows) {
                g.table_auto = false;
                break;
            }
            if (g.window_selection >= kWindowNumericRows) {
                break;
            }
            if (g.window_edit_cursor[g.window_selection] < g.window_edit_len[g.window_selection]) {
                ++g.window_edit_cursor[g.window_selection];
            }
            break;
        case Key::Left:
            if (g.window_selection == kWindowNumericRows) {
                g.table_auto = true;
                break;
            }
            if (g.window_selection >= kWindowNumericRows) {
                break;
            }
            if (g.window_edit_cursor[g.window_selection] > 0) {
                --g.window_edit_cursor[g.window_selection];
            }
            break;
        case Key::Delete:
        case Key::Back:
            if (g.window_selection >= kWindowNumericRows) {
                break;
            }
            if (g.window_edit_cursor[g.window_selection] > 0) {
                delete_range(g.window_edit[g.window_selection],
                             g.window_edit_len[g.window_selection],
                             g.window_edit_cursor[g.window_selection] - 1,
                             1);
                --g.window_edit_cursor[g.window_selection];
            }
            break;
        case Key::Clear:
            if (g.window_selection >= kWindowNumericRows) {
                break;
            }
            g.window_edit[g.window_selection][0] = '\0';
            g.window_edit_len[g.window_selection] = 0;
            g.window_edit_cursor[g.window_selection] = 0;
            break;
        case Key::Enter:
            if (g.window_selection == kWindowNumericRows) {
                g.table_auto = !g.table_auto;
                break;
            }
            commit_window_edits();
            break;
        case Key::Graph:
            commit_window_edits();
            g.screen = Screen::Graph;
            break;
        default: {
            if (g.window_selection >= kWindowNumericRows) {
                break;
            }
            const int before_len = g.window_edit_len[g.window_selection];
            const int before_cursor = g.window_edit_cursor[g.window_selection];
            if (!insert_window_value_key(key,
                                         g.window_edit[g.window_selection],
                                         g.window_edit_len[g.window_selection],
                                         g.window_edit_cursor[g.window_selection])) {
                g.window_edit_len[g.window_selection] = before_len;
                g.window_edit_cursor[g.window_selection] = before_cursor;
            }
            break;
        }
    }
}

int top_level_equal_pos(const char* expr) {
    int depth = 0;
    for (int i = 0; expr[i] != '\0'; ++i) {
        if (is_open_paren(expr[i])) {
            ++depth;
        } else if (is_close_paren(expr[i]) && depth > 0) {
            --depth;
        } else if (depth == 0 && expr[i] == '=') {
            return i;
        }
    }
    return -1;
}

bool copy_solver_range(const char* expr, int start, int end, char* out, std::size_t out_size) {
    if (out == nullptr || out_size == 0u || start < 0 || end < start) {
        return false;
    }
    const int len = end - start;
    if (len <= 0 || len >= static_cast<int>(out_size)) {
        return false;
    }
    for (int i = 0; i < len; ++i) {
        out[i] = expr[start + i];
    }
    out[len] = '\0';
    return true;
}

bool evaluate_solver_expression(const char* expr, char variable, CalcReal x, CalcReal& value) {
    EvalContext copy = g.eval;
    const int idx = variable - 'A';
    if (idx < 0 || idx >= 26) {
        return false;
    }
    copy.variables[idx] = x;
    copy.variable_imag[idx] = real(0.0);
    copy.variable_valid[idx] = true;
    EvalResult result = evaluate_expression(expr, copy);
    if (!result.ok || std::fabs(result.imag) > real(0.000000001) || !std::isfinite(result.value)) {
        return false;
    }
    value = result.value;
    return true;
}

bool solver_residual(const char* expr, char variable, CalcReal x, CalcReal& value) {
    const int eq = top_level_equal_pos(expr);
    if (eq < 0) {
        return evaluate_solver_expression(expr, variable, x, value);
    }
    char lhs[kExpressionCapacity]{};
    char rhs[kExpressionCapacity]{};
    const int len = static_cast<int>(std::strlen(expr));
    if (!copy_solver_range(expr, 0, eq, lhs, sizeof(lhs)) ||
        !copy_solver_range(expr, eq + 1, len, rhs, sizeof(rhs))) {
        return false;
    }
    CalcReal lhs_value = real(0.0);
    CalcReal rhs_value = real(0.0);
    if (!evaluate_solver_expression(lhs, variable, x, lhs_value) ||
        !evaluate_solver_expression(rhs, variable, x, rhs_value)) {
        return false;
    }
    value = lhs_value - rhs_value;
    return std::isfinite(value);
}

bool parse_solver_value(int row, CalcReal& value) {
    if (row < 0 || row >= kSolverValueRows || g.solver_value_len[row] <= 0) {
        value = row == 0 ? real(-10.0) : (row == 1 ? real(10.0) : real(0.0));
        return true;
    }
    EvalContext copy = g.eval;
    EvalResult result = evaluate_expression(g.solver_value[row], copy);
    if (!result.ok || std::fabs(result.imag) > real(0.000000001) || !std::isfinite(result.value)) {
        return false;
    }
    value = result.value;
    return true;
}

bool bisect_solver_root(const char* expr, char variable, CalcReal lo, CalcReal hi, CalcReal flo, CalcReal fhi, CalcReal& root) {
    if (std::fabs(flo) <= real(0.0000000001)) {
        root = lo;
        return true;
    }
    if (std::fabs(fhi) <= real(0.0000000001)) {
        root = hi;
        return true;
    }
    if ((flo < real(0.0) && fhi < real(0.0)) || (flo > real(0.0) && fhi > real(0.0))) {
        return false;
    }
    for (int i = 0; i < 80; ++i) {
        const CalcReal mid = (lo + hi) / real(2.0);
        CalcReal fmid = real(0.0);
        if (!solver_residual(expr, variable, mid, fmid)) {
            return false;
        }
        if (std::fabs(fmid) <= real(0.0000000001) || std::fabs(hi - lo) <= real(0.0000000001)) {
            root = mid;
            return true;
        }
        if ((flo < real(0.0) && fmid > real(0.0)) || (flo > real(0.0) && fmid < real(0.0))) {
            hi = mid;
            fhi = fmid;
        } else {
            lo = mid;
            flo = fmid;
        }
    }
    (void)fhi;
    root = (lo + hi) / real(2.0);
    return true;
}

void run_solver() {
    char expr[kExpressionCapacity]{};
    sanitize_expression(g.solver_expr, expr, sizeof(expr));
    if (expr[0] == '\0') {
        copy_string(g.solver_status, sizeof(g.solver_status), "NO EQUATION");
        return;
    }
    CalcReal lo = real(-10.0);
    CalcReal hi = real(10.0);
    CalcReal guess = real(0.0);
    if (!parse_solver_value(0, lo) || !parse_solver_value(1, hi) || !parse_solver_value(2, guess)) {
        copy_string(g.solver_status, sizeof(g.solver_status), "BAD BOUND");
        return;
    }
    if (hi < lo) {
        const CalcReal tmp = hi;
        hi = lo;
        lo = tmp;
    }
    if (hi == lo) {
        copy_string(g.solver_status, sizeof(g.solver_status), "BAD BOUND");
        return;
    }
    const char variable = (g.solver_variable >= 'A' && g.solver_variable <= 'Z') ? g.solver_variable : 'X';

    constexpr int samples = 64;
    CalcReal prev_x = lo;
    CalcReal prev_f = real(0.0);
    bool have_prev = solver_residual(expr, variable, prev_x, prev_f);
    CalcReal best_x = prev_x;
    CalcReal best_abs = have_prev ? std::fabs(prev_f) : real(1.0e30);
    bool found = false;
    CalcReal root = guess;
    for (int i = 1; i <= samples; ++i) {
        const CalcReal x = lo + (hi - lo) * static_cast<CalcReal>(i) / static_cast<CalcReal>(samples);
        CalcReal f = real(0.0);
        const bool ok = solver_residual(expr, variable, x, f);
        if (ok && std::fabs(f) < best_abs) {
            best_abs = std::fabs(f);
            best_x = x;
        }
        if (ok && have_prev && ((prev_f <= real(0.0) && f >= real(0.0)) || (prev_f >= real(0.0) && f <= real(0.0)))) {
            found = bisect_solver_root(expr, variable, prev_x, x, prev_f, f, root);
            break;
        }
        if (ok) {
            prev_x = x;
            prev_f = f;
            have_prev = true;
        }
    }
    if (!found) {
        CalcReal x0 = best_x;
        CalcReal x1 = guess;
        if (x1 < lo || x1 > hi || x1 == x0) {
            x1 = best_x + (hi - lo) / real(100.0);
            if (x1 > hi) {
                x1 = best_x - (hi - lo) / real(100.0);
            }
        }
        CalcReal f0 = real(0.0);
        CalcReal f1 = real(0.0);
        if (solver_residual(expr, variable, x0, f0) && solver_residual(expr, variable, x1, f1)) {
            for (int i = 0; i < 40; ++i) {
                const CalcReal denom = f1 - f0;
                if (std::fabs(denom) <= real(0.000000000001)) {
                    break;
                }
                CalcReal x2 = x1 - f1 * (x1 - x0) / denom;
                if (x2 < lo || x2 > hi || !std::isfinite(x2)) {
                    x2 = (x0 + x1) / real(2.0);
                }
                CalcReal f2 = real(0.0);
                if (!solver_residual(expr, variable, x2, f2)) {
                    break;
                }
                x0 = x1;
                f0 = f1;
                x1 = x2;
                f1 = f2;
                if (std::fabs(f1) <= real(0.00000001)) {
                    root = x1;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found && best_abs <= real(0.000001)) {
        root = best_x;
        found = true;
    }
    if (!found) {
        copy_string(g.solver_status, sizeof(g.solver_status), "NO SIGN CHANGE");
        return;
    }

    const int idx = variable - 'A';
    g.eval.variables[idx] = root;
    g.eval.variable_imag[idx] = real(0.0);
    g.eval.variable_valid[idx] = true;
    g.eval.ans = root;
    g.eval.ans_imag = real(0.0);
    char value[24]{};
    format_value(root, value, sizeof(value));
    g.solver_status[0] = variable;
    g.solver_status[1] = '=';
    g.solver_status[2] = '\0';
    std::size_t pos = 2;
    append_string(g.solver_status, sizeof(g.solver_status), pos, value);
}

void handle_solver_key(Key key) {
    if (key == Key::Clear) {
        g.screen = Screen::Home;
        return;
    }
    if (key == Key::Up) {
        if (g.solver_selection > 0) {
            --g.solver_selection;
        }
        return;
    }
    if (key == Key::Down) {
        if (g.solver_selection < 4) {
            ++g.solver_selection;
        }
        return;
    }
    if (key == Key::Enter) {
        run_solver();
        return;
    }
    if (g.solver_selection == 0) {
        edit_expression_key(key, g.solver_expr, g.solver_len, g.solver_cursor);
        ensure_cursor_visible(g.solver_expr, g.solver_len, g.solver_cursor, kLcdWidth - 58, g.solver_scroll_x);
        return;
    }
    if (g.solver_selection == 1) {
        const char letter = key_letter(key);
        if (letter >= 'A' && letter <= 'Z') {
            g.solver_variable = letter;
        }
        return;
    }
    const int value_row = g.solver_selection - 2;
    if (value_row >= 0 && value_row < kSolverValueRows) {
        if (key == Key::Delete || key == Key::Back) {
            if (g.solver_value_cursor[value_row] > 0) {
                delete_range(g.solver_value[value_row],
                             g.solver_value_len[value_row],
                             g.solver_value_cursor[value_row] - 1,
                             1);
                --g.solver_value_cursor[value_row];
            }
            return;
        }
        if (key == Key::Left) {
            if (g.solver_value_cursor[value_row] > 0) {
                --g.solver_value_cursor[value_row];
            }
            return;
        }
        if (key == Key::Right) {
            if (g.solver_value_cursor[value_row] < g.solver_value_len[value_row]) {
                ++g.solver_value_cursor[value_row];
            }
            return;
        }
        if (insert_window_value_key(key,
                                    g.solver_value[value_row],
                                    g.solver_value_len[value_row],
                                    g.solver_value_cursor[value_row])) {
            return;
        }
    }
}

void ensure_table_selection_visible() {
    if (g.table_col < 0) {
        g.table_col = 0;
    }
    if (g.table_col >= kTableColumns) {
        g.table_col = kTableColumns - 1;
    }
    if (g.table_row < g.table_first_row) {
        g.table_first_row = g.table_row;
    }
    if (g.table_row >= g.table_first_row + kTableVisibleRows) {
        g.table_first_row = g.table_row - kTableVisibleRows + 1;
    }
    if (g.table_col <= 0) {
        g.table_first_col = 1;
    } else if (g.table_col < g.table_first_col) {
        g.table_first_col = g.table_col;
    }
    if (g.table_col > 0 && g.table_col >= g.table_first_col + kTableVisibleYColumns) {
        g.table_first_col = g.table_col - kTableVisibleYColumns + 1;
    }
    if (g.table_first_col < 1) {
        g.table_first_col = 1;
    }
    if (g.table_first_col > kTableColumns - kTableVisibleYColumns) {
        g.table_first_col = kTableColumns - kTableVisibleYColumns;
    }
}

ManualTableEntry* find_manual_table_entry(int row) {
    for (int i = 0; i < kTableManualCapacity; ++i) {
        if (g.table_manual[i].used && g.table_manual[i].row == row) {
            return &g.table_manual[i];
        }
    }
    return nullptr;
}

ManualTableEntry* manual_table_entry_for_edit(int row) {
    ManualTableEntry* existing = find_manual_table_entry(row);
    if (existing != nullptr) {
        return existing;
    }
    for (int i = 0; i < kTableManualCapacity; ++i) {
        if (!g.table_manual[i].used) {
            g.table_manual[i] = ManualTableEntry{};
            g.table_manual[i].row = row;
            g.table_manual[i].used = true;
            return &g.table_manual[i];
        }
    }
    ManualTableEntry& recycled = g.table_manual[0];
    recycled = ManualTableEntry{};
    recycled.row = row;
    recycled.used = true;
    return &recycled;
}

bool table_manual_x_value(int row, CalcReal& value) {
    ManualTableEntry* entry = find_manual_table_entry(row);
    if (entry == nullptr || entry->len == 0) {
        return false;
    }
    EvalContext copy = g.eval;
    EvalResult result = evaluate_expression(entry->text, copy);
    if (!result.ok || !near_zero(result.imag) || !std::isfinite(result.value)) {
        return false;
    }
    value = result.value;
    return true;
}

CalcReal table_x_for_row(int row) {
    const CalcReal step = std::fabs(g.table_step) < real(0.0000000005) ? real(1.0) : g.table_step;
    return g.table_start + static_cast<CalcReal>(row) * step;
}

void handle_table_key(Key key) {
    switch (key) {
        case Key::Graph:
            g.screen = Screen::Graph;
            return;
        case Key::Window:
            sync_window_edit_from_values();
            g.screen = Screen::Window;
            return;
        case Key::YEquals:
            g.screen = Screen::YEquals;
            return;
        case Key::Up:
            --g.table_row;
            break;
        case Key::Down:
            ++g.table_row;
            break;
        case Key::Left:
            if (!g.table_auto && g.table_col == 0) {
                ManualTableEntry* entry = manual_table_entry_for_edit(g.table_row);
                if (entry->cursor > 0) {
                    --entry->cursor;
                    break;
                }
            }
            if (g.table_col > 0) {
                --g.table_col;
            }
            break;
        case Key::Right:
            if (!g.table_auto && g.table_col == 0) {
                ManualTableEntry* entry = manual_table_entry_for_edit(g.table_row);
                if (entry->cursor < entry->len) {
                    ++entry->cursor;
                    break;
                }
            }
            if (g.table_col + 1 < kTableColumns) {
                ++g.table_col;
            }
            break;
        case Key::Delete:
        case Key::Back:
            if (!g.table_auto && g.table_col == 0) {
                ManualTableEntry* entry = manual_table_entry_for_edit(g.table_row);
                if (entry->cursor > 0) {
                    delete_range(entry->text, entry->len, entry->cursor - 1, 1);
                    --entry->cursor;
                }
            }
            break;
        case Key::Clear:
            if (!g.table_auto && g.table_col == 0) {
                ManualTableEntry* entry = manual_table_entry_for_edit(g.table_row);
                entry->text[0] = '\0';
                entry->len = 0;
                entry->cursor = 0;
            }
            break;
        default:
            if (!g.table_auto && g.table_col == 0) {
                ManualTableEntry* entry = manual_table_entry_for_edit(g.table_row);
                insert_window_value_key(key, entry->text, entry->len, entry->cursor);
            }
            break;
    }
    ensure_table_selection_visible();
}

void handle_settings_key(Key key) {
    switch (key) {
        case Key::Up:
            if (g.settings_selection > 0) {
                --g.settings_selection;
            }
            break;
        case Key::Down:
            if (g.settings_selection < 1) {
                ++g.settings_selection;
            }
            break;
        case Key::Left:
            if (g.settings_selection == 0) {
                g.eval.degree_mode = false;
            } else {
                g.fraction_output = false;
            }
            break;
        case Key::Right:
            if (g.settings_selection == 0) {
                g.eval.degree_mode = true;
            } else {
                g.fraction_output = true;
            }
            break;
        case Key::Enter:
            if (g.settings_selection == 0) {
                g.eval.degree_mode = !g.eval.degree_mode;
            } else {
                g.fraction_output = !g.fraction_output;
            }
            break;
        case Key::Clear:
            g.screen = Screen::Home;
            break;
        default:
            break;
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
        if (is_open_paren(expr[i])) {
            ++depth;
        } else if (is_close_paren(expr[i])) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

int fraction_end_at(const char* expr, int pos, int end, int& num_start, int& num_end, int& den_start, int& den_end) {
    if (pos >= end || !is_open_paren(expr[pos])) {
        return -1;
    }
    const int first_close = matching_paren(expr, pos, end);
    if (first_close < 0 || first_close + 2 >= end || expr[first_close + 1] != '/' || !is_open_paren(expr[first_close + 2])) {
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

int nth_root_end_at(const char* expr, int pos, int end, int& index_start, int& index_end, int& radicand_start, int& radicand_end) {
    if (pos + 6 > end || !starts_with_at(expr, pos, "root(")) {
        return -1;
    }
    const int first_open = pos + 4;
    const int first_close = matching_paren(expr, first_open, end);
    if (first_close < 0 || first_close + 1 >= end || !is_open_paren(expr[first_close + 1])) {
        return -1;
    }
    const int second_open = first_close + 1;
    const int second_close = matching_paren(expr, second_open, end);
    if (second_close < 0) {
        return -1;
    }
    index_start = first_open + 1;
    index_end = first_close;
    radicand_start = second_open + 1;
    radicand_end = second_close;
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
    NthRoot,
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
    Radical,
    RootIndex
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

int superscript_raise(bool parent_small) {
    return parent_small ? 5 : 7;
}

int superscript_extra_raise_for_range(const char* expr, int start, int end) {
    int num_start = 0;
    int num_end = 0;
    int den_start = 0;
    int den_end = 0;
    const int frac_end = fraction_end_at(expr, start, end, num_start, num_end, den_start, den_end);
    return frac_end == end ? 3 : 0;
}

int fraction_bar_offset(bool small) {
    return small ? -3 : -4;
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
    anchor.y = baseline - (h <= kSupH ? font_ascent(true) : font_ascent(false));
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
    if (i > start && i < end && is_open_paren(expr[i])) {
        const int close = matching_paren(expr, i, end);
        if (close >= 0) {
            return close + 1;
        }
    }
    return -1;
}

const char* inverse_trig_label_at(const char* expr, int start, int end, int& source_end) {
    source_end = start;
    if (start + 5 > end) {
        return nullptr;
    }
    if (starts_with_at(expr, start, "asin(")) {
        source_end = start + 5;
        return "SIN";
    }
    if (starts_with_at(expr, start, "acos(")) {
        source_end = start + 5;
        return "COS";
    }
    if (starts_with_at(expr, start, "atan(")) {
        source_end = start + 5;
        return "TAN";
    }
    return nullptr;
}

const char* standard_trig_label_at(const char* expr, int start, int end, int& source_end) {
    source_end = start;
    if (start + 4 > end) {
        return nullptr;
    }
    if (starts_with_at(expr, start, "sin(")) {
        source_end = start + 4;
        return "SIN";
    }
    if (starts_with_at(expr, start, "cos(")) {
        source_end = start + 4;
        return "COS";
    }
    if (starts_with_at(expr, start, "tan(")) {
        source_end = start + 4;
        return "TAN";
    }
    return nullptr;
}

ExprBox standard_trig_prefix_box(bool small) {
    return box_from_ascent(4 * font_w(small), font_ascent(small), font_descent(small));
}

ExprBox inverse_trig_prefix_box(bool small) {
    const ExprBox exp = box_from_ascent(2 * font_w(true), font_ascent(true), font_descent(true));
    const int ascent = exp.ascent + superscript_raise(small) > font_ascent(small)
                           ? exp.ascent + superscript_raise(small)
                           : font_ascent(small);
    return box_from_ascent(4 * font_w(small) + exp.w, ascent, font_descent(small));
}

const char* atomic_text_label_at(const char* expr, int start, int end, int& source_end) {
    source_end = start;
    if (atomic_text_at(expr, start, end, "Ans")) {
        source_end = start + 3;
        return "Ans";
    }
    return nullptr;
}

const char* atomic_function_label_at(const char* expr, int start, int end, int& source_end) {
    source_end = start;
    for (const AtomicRenderPrefix& prefix : kAtomicRenderPrefixes) {
        const int len = static_cast<int>(std::strlen(prefix.source));
        if (start + len <= end && std::strncmp(expr + start, prefix.source, len) == 0) {
            source_end = start + len;
            return prefix.label;
        }
    }
    return nullptr;
}

void exponent_range(const char* expr, int caret, int end, int& visual_start, int& visual_end, int& source_end) {
    visual_start = caret + 1;
    visual_end = visual_start;
    source_end = visual_start;
    if (visual_start >= end) {
        return;
    }
    if (is_open_paren(expr[visual_start])) {
        const int close = matching_paren(expr, visual_start, end);
        if (close >= 0) {
            visual_start = visual_start + 1;
            visual_end = close;
            source_end = close + 1;
            return;
        }
        if (end - 1 > visual_start && is_close_paren(expr[end - 1])) {
            visual_start = visual_start + 1;
            visual_end = end - 1;
            source_end = end;
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
        if (region == CursorRegion::Main) {
            return box_from_ascent(0, font_ascent(small), font_descent(small));
        }
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
        int index_start = 0;
        int index_end = 0;
        int radicand_start = 0;
        int radicand_end = 0;
        const int root_end = nth_root_end_at(expr, i, end, index_start, index_end, radicand_start, radicand_end);
        if (root_end > 0) {
            const ExprBox index = measure_expression_range_impl(expr, index_start, index_end, true, layout, CursorRegion::RootIndex);
            const ExprBox radicand = measure_expression_range_impl(expr, radicand_start, radicand_end, small, layout, CursorRegion::Radical);
            const int index_w = index.w > 10 ? index.w : 10;
            const ExprBox box = box_from_ascent(index_w + radicand.w + 17, radicand.ascent + index.h + 3, radicand.descent);
            add_node(layout, LayoutKind::NthRoot, i, root_end, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = root_end;
            continue;
        }
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, layout, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, layout, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const int bar_offset = fraction_bar_offset(small);
            const ExprBox box = box_from_ascent(inner_w + 10, num.h + 3 - bar_offset, den.h + 4 + bar_offset);
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
        int standard_source_end = 0;
        if (standard_trig_label_at(expr, i, end, standard_source_end) != nullptr) {
            const ExprBox box = standard_trig_prefix_box(small);
            add_node(layout, LayoutKind::TextRun, i, standard_source_end, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = standard_source_end;
            continue;
        }
        int inverse_source_end = 0;
        if (inverse_trig_label_at(expr, i, end, inverse_source_end) != nullptr) {
            const ExprBox box = inverse_trig_prefix_box(small);
            add_node(layout, LayoutKind::TextRun, i, inverse_source_end, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = inverse_source_end;
            continue;
        }
        int atomic_source_end = 0;
        if (atomic_text_label_at(expr, i, end, atomic_source_end) != nullptr) {
            const ExprBox box = box_from_ascent(3 * font_w(small), font_ascent(small), font_descent(small));
            add_node(layout, LayoutKind::TextRun, i, atomic_source_end, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = atomic_source_end;
            continue;
        }
        int function_source_end = 0;
        const char* function_label = atomic_function_label_at(expr, i, end, function_source_end);
        if (function_label != nullptr) {
            const int label_len = static_cast<int>(std::strlen(function_label));
            const ExprBox box = box_from_ascent(label_len * font_w(small), font_ascent(small), font_descent(small));
            add_node(layout, LayoutKind::TextRun, i, function_source_end, small, box);
            w += box.w;
            if (box.ascent > ascent) {
                ascent = box.ascent;
            }
            if (box.descent > descent) {
                descent = box.descent;
            }
            i = function_source_end;
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
            const ExprBox box = box_from_ascent(exp.w, exp.ascent + superscript_raise(small) + superscript_extra_raise_for_range(expr, exp_start, exp_end), 0);
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
        int index_start = 0;
        int index_end = 0;
        int radicand_start = 0;
        int radicand_end = 0;
        const int root_end = nth_root_end_at(expr, i, end, index_start, index_end, radicand_start, radicand_end);
        if (root_end > 0) {
            const ExprBox index = measure_expression_range_impl(expr, index_start, index_end, true, nullptr, CursorRegion::RootIndex);
            const ExprBox radicand = measure_expression_range_impl(expr, radicand_start, radicand_end, small, nullptr, CursorRegion::Radical);
            const int index_w = index.w > 10 ? index.w : 10;
            const int index_x = cx + 4 + (index_w - index.w) / 2;
            const int index_baseline = baseline - radicand.ascent - 3;
            const int radical_x = cx + index_w + 13;
            const int radical_top = baseline - radicand.ascent - 4;
            const int radical_box_h = radicand.h + 4;
            if (index_start == index_end) {
                dotted_rect(display, index_x - 1, index_baseline - index.ascent - 1, index_w + 2, index.h + 2, kGray);
            }
            if (radicand_start == radicand_end) {
                dotted_rect(display, radical_x - 1, radical_top, radicand.w + 4, radical_box_h, kGray);
            }
            draw_expression_range_impl(display, index_x, index_baseline, expr, index_start, index_end, true, fg, bg);
            draw_line(display, cx + index_w + 1, baseline - 3, cx + index_w + 5, baseline + 2, fg);
            draw_line(display, cx + index_w + 5, baseline + 2, cx + index_w + 11, radical_top, fg);
            draw_line(display, cx + index_w + 11, radical_top, radical_x + radicand.w + 3, radical_top, fg);
            draw_expression_range_impl(display, radical_x, baseline, expr, radicand_start, radicand_end, small, fg, bg);
            cx += index_w + radicand.w + 17;
            i = root_end;
            continue;
        }
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, nullptr, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, nullptr, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const int box_w = inner_w + 10;
            const int num_x = cx + 5 + (inner_w - num.w) / 2;
            const int den_x = cx + 5 + (inner_w - den.w) / 2;
            const int bar_y = baseline + fraction_bar_offset(small);
            draw_expression_range_impl(display, num_x, bar_y - 3 - num.descent, expr, num_start, num_end, true, fg, bg);
            draw_line(display, cx + 2, bar_y, cx + box_w - 3, bar_y, fg);
            draw_expression_range_impl(display, den_x, bar_y + 4 + den.ascent, expr, den_start, den_end, true, fg, bg);
            cx += box_w + 2;
            i = frac_end;
            continue;
        }
        int standard_source_end = 0;
        const char* standard_label = standard_trig_label_at(expr, i, end, standard_source_end);
        if (standard_label != nullptr) {
            draw_text_scaled(display, cx, baseline - font_ascent(small), standard_label, small ? 1 : kTextScale, fg, bg);
            cx += 3 * font_w(small);
            draw_text_scaled(display, cx, baseline - font_ascent(small), "(", small ? 1 : kTextScale, fg, bg);
            cx += font_w(small);
            i = standard_source_end;
            continue;
        }
        int inverse_source_end = 0;
        const char* inverse_label = inverse_trig_label_at(expr, i, end, inverse_source_end);
        if (inverse_label != nullptr) {
            draw_text_scaled(display, cx, baseline - font_ascent(small), inverse_label, small ? 1 : kTextScale, fg, bg);
            cx += 3 * font_w(small);
            const int exp_baseline = baseline - superscript_raise(small);
            draw_expression_range_impl(display, cx, exp_baseline, "-1", 0, 2, true, fg, bg);
            cx += 2 * font_w(true);
            draw_text_scaled(display, cx, baseline - font_ascent(small), "(", small ? 1 : kTextScale, fg, bg);
            cx += font_w(small);
            i = inverse_source_end;
            continue;
        }
        int atomic_source_end = 0;
        const char* atomic_label = atomic_text_label_at(expr, i, end, atomic_source_end);
        if (atomic_label != nullptr) {
            draw_text_scaled(display, cx, baseline - font_ascent(small), atomic_label, small ? 1 : kTextScale, fg, bg);
            cx += 3 * font_w(small);
            i = atomic_source_end;
            continue;
        }
        int function_source_end = 0;
        const char* function_label = atomic_function_label_at(expr, i, end, function_source_end);
        if (function_label != nullptr) {
            draw_text_scaled(display, cx, baseline - font_ascent(small), function_label, small ? 1 : kTextScale, fg, bg);
            cx += static_cast<int>(std::strlen(function_label)) * font_w(small);
            i = function_source_end;
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
            const int exp_baseline = baseline - superscript_raise(small) - superscript_extra_raise_for_range(expr, exp_start, exp_end);
            if (exp_start == exp_end) {
                draw_placeholder(display, cx, exp_baseline, true, fg);
            } else {
                draw_expression_range_impl(display, cx, exp_baseline, expr, exp_start, exp_end, true, fg, bg);
            }
            cx += exp.w;
            i = source_end;
            continue;
        }
        char text[2] = {display_char(expr[i]), '\0'};
        const Color text_fg = (expr[i] == kAutoOpenParen || expr[i] == kAutoCloseParen) ? kGray : fg;
        draw_text_scaled(display, cx, baseline - font_ascent(small), text, small ? 1 : kTextScale, text_fg, bg);
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
        int index_start = 0;
        int index_end = 0;
        int radicand_start = 0;
        int radicand_end = 0;
        const int root_end = nth_root_end_at(expr, i, end, index_start, index_end, radicand_start, radicand_end);
        if (root_end > 0) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox index = measure_expression_range_impl(expr, index_start, index_end, true, nullptr, CursorRegion::RootIndex);
            const ExprBox radicand = measure_expression_range_impl(expr, radicand_start, radicand_end, small, nullptr, CursorRegion::Radical);
            const int index_w = index.w > 10 ? index.w : 10;
            const int index_x = cx + 4 + (index_w - index.w) / 2;
            const int index_baseline = baseline - radicand.ascent - 3;
            const int radical_x = cx + index_w + 13;
            emit_anchors_range(layout, index_x, index_baseline, expr, index_start, index_end, true, CursorRegion::RootIndex);
            emit_anchors_range(layout, radical_x, baseline, expr, radicand_start, radicand_end, small, CursorRegion::Radical);
            cx += index_w + radicand.w + 17;
            i = root_end;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            continue;
        }
        const int frac_end = fraction_end_at(expr, i, end, num_start, num_end, den_start, den_end);
        if (frac_end > 0) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox num = measure_expression_range_impl(expr, num_start, num_end, true, nullptr, CursorRegion::Numerator);
            const ExprBox den = measure_expression_range_impl(expr, den_start, den_end, true, nullptr, CursorRegion::Denominator);
            const int inner_w = num.w > den.w ? num.w : den.w;
            const int box_w = inner_w + 10;
            const int num_x = cx + 5 + (inner_w - num.w) / 2;
            const int den_x = cx + 5 + (inner_w - den.w) / 2;
            const int bar_y = baseline + fraction_bar_offset(small);
            emit_anchors_range(layout, num_x, bar_y - 3 - num.descent, expr, num_start, num_end, true, CursorRegion::Numerator);
            emit_anchors_range(layout, den_x, bar_y + 4 + den.ascent, expr, den_start, den_end, true, CursorRegion::Denominator);
            cx += box_w + 2;
            add_anchor(&layout, frac_end, cx, baseline, font_h(small), region);
            i = frac_end;
            continue;
        }
        int standard_source_end = 0;
        if (standard_trig_label_at(expr, i, end, standard_source_end) != nullptr) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox box = standard_trig_prefix_box(small);
            cx += box.w;
            i = standard_source_end;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            continue;
        }
        int inverse_source_end = 0;
        if (inverse_trig_label_at(expr, i, end, inverse_source_end) != nullptr) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox box = inverse_trig_prefix_box(small);
            cx += box.w;
            i = inverse_source_end;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            continue;
        }
        int atomic_source_end = 0;
        if (atomic_text_label_at(expr, i, end, atomic_source_end) != nullptr) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            cx += 3 * font_w(small);
            i = atomic_source_end;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            continue;
        }
        int function_source_end = 0;
        const char* function_label = atomic_function_label_at(expr, i, end, function_source_end);
        if (function_label != nullptr) {
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            cx += static_cast<int>(std::strlen(function_label)) * font_w(small);
            i = function_source_end;
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
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
            add_anchor(&layout, i, cx, baseline, font_h(small), region);
            const ExprBox exp = exp_start == exp_end ? placeholder_box(true) : measure_expression_range_impl(expr, exp_start, exp_end, true, nullptr, CursorRegion::Exponent);
            const int exp_baseline = baseline - superscript_raise(small) - superscript_extra_raise_for_range(expr, exp_start, exp_end);
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
        if (dist == 0 && best_dist == 0) {
            if (layout.anchors[i].region == CursorRegion::Main) {
                best = i;
            }
            continue;
        }
        if (dist < best_dist) {
            best = i;
            best_dist = dist;
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
    int next = index + (direction < 0 ? -1 : 1);
    while (next >= 0 && next < layout.anchor_count && layout.anchors[next].source == layout.anchors[index].source) {
        next += direction < 0 ? -1 : 1;
    }
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
    if (len > 0) {
        draw_expression_range_impl(display, x, baseline, expr, 0, len, false, fg, bg);
    }
    if (cursor >= 0) {
        LayoutContext layout = build_layout_anchors(expr, len, x, y);
        const int index = anchor_index_for_cursor(layout, cursor);
        if (index >= 0) {
            draw_cursor(display, layout.anchors[index].x, layout.anchors[index].y, layout.anchors[index].h, fg);
        }
    }
    return box.w;
}

int input_height_for_expression(const char* expr) {
    const int len = static_cast<int>(std::strlen(expr));
    const ExprBox box = measure_expression_range(expr, 0, len);
    int h = box.h + 8;
    if (h < kInputH) {
        h = kInputH;
    }
    if (h > kLcdHeight - 4) {
        h = kLcdHeight - 4;
    }
    return h;
}

int history_visible_last_from_first(int first, int available_h) {
    if (g.history_count <= 0) {
        return -1;
    }
    if (first < 0) {
        first = 0;
    }
    if (first >= g.history_count) {
        first = g.history_count - 1;
    }
    int last = first - 1;
    int used_h = 0;
    while (last + 1 < g.history_count) {
        const int next = last + 1;
        const int row_h = history_row_height(g.history[next]);
        if (used_h + row_h > available_h && last >= first) {
            break;
        }
        used_h += row_h;
        last = next;
    }
    return last;
}

void clamp_history_view() {
    if (g.history_count <= 0) {
        g.history_first_entry = 0;
        return;
    }
    if (g.history_first_entry < 0) {
        g.history_first_entry = 0;
    }
    if (g.history_first_entry >= g.history_count) {
        g.history_first_entry = g.history_count - 1;
    }
}

void ensure_history_selection_visible() {
    if (g.history_selection < 0) {
        g.history_first_entry = 0;
        return;
    }
    clamp_history_view();
    const int input_h = input_height_for_expression(g.home_expr);
    const int available_h = kLcdHeight - input_h - 2;
    const int selected_entry = g.history_selection / 2;
    if (selected_entry < g.history_first_entry) {
        g.history_first_entry = selected_entry;
        return;
    }
    int last = history_visible_last_from_first(g.history_first_entry, available_h);
    while (selected_entry > last && g.history_first_entry + 1 <= selected_entry) {
        ++g.history_first_entry;
        last = history_visible_last_from_first(g.history_first_entry, available_h);
    }
}

void draw_input_line(Display& display, const char* prompt, const char* expr, int cursor, int& scroll_x, int input_y, int input_h) {
    const int expr_x = 8;
    const int visible_w = kLcdWidth - expr_x - 2;
    const int len = static_cast<int>(std::strlen(expr));
    ensure_cursor_visible(expr, len, cursor, visible_w, scroll_x);
    const ExprBox box = measure_expression_range(expr, 0, len);
    int expr_y = input_y + 4;
    if (box.h + 8 < input_h) {
        expr_y = input_y + (input_h - box.h) / 2;
    }
    fill_rect(display, 0, input_y, kLcdWidth, input_h, kWhite);
    draw_expression(display, expr_x - scroll_x, expr_y, expr, cursor, kBlack, kWhite);
    fill_rect(display, 0, input_y, 3, input_h, kWhite);
    (void)prompt;
}

int y_row_height(int row) {
    if (row < 0 || row >= kMaxYEquations) {
        return 20;
    }
    const ExprBox expr_box = measure_expression_range(g.y_expr[row], 0, g.y_len[row]);
    int h = expr_box.h + 8;
    if (h < 20) {
        h = 20;
    }
    return h;
}

void ensure_y_selection_visible() {
    if (g.y_selection < 0) {
        g.y_selection = 0;
    }
    if (g.y_selection >= kMaxYEquations) {
        g.y_selection = kMaxYEquations - 1;
    }
    if (g.y_first_row > g.y_selection) {
        g.y_first_row = g.y_selection;
    }
    if (g.y_first_row < 0) {
        g.y_first_row = 0;
    }
    const int available_h = kLcdHeight - 22 - 24;
    while (g.y_first_row < g.y_selection) {
        int y = 0;
        bool visible = false;
        for (int row = g.y_first_row; row <= g.y_selection; ++row) {
            const int h = y_row_height(row);
            if (row == g.y_selection) {
                visible = y + h <= available_h;
                break;
            }
            y += h;
        }
        if (visible) {
            break;
        }
        ++g.y_first_row;
    }
}

int history_row_height(const HistoryEntry& entry) {
    const ExprBox expr_box = measure_expression_range(entry.expression, 0, static_cast<int>(std::strlen(entry.expression)));
    const ExprBox ans_box = measure_expression_range(entry.result, 0, static_cast<int>(std::strlen(entry.result)));
    return expr_box.h + 3 + ans_box.h + 7;
}

void render_home(Display& display) {
    clear(display, kWhite);
    const int input_h = input_height_for_expression(g.home_expr);
    const int input_y = kLcdHeight - input_h;
    ensure_history_selection_visible();
    int first = g.history_first_entry;
    int last = history_visible_last_from_first(first, input_y - 2);
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
    draw_input_line(display, ">", g.home_expr, g.home_cursor, g.home_expr_scroll_x, input_y, input_h);
}

bool graph_to_plot_point(const GraphWindow& window, CalcReal x, CalcReal y, int& sx, int& sy) {
    const CalcReal w = window.xmax - window.xmin;
    const CalcReal h = window.ymax - window.ymin;
    if (w == real(0.0) || h == real(0.0) || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    constexpr int plot_h = kPlotBottom - kPlotTop;
    const CalcReal px = (x - window.xmin) * static_cast<CalcReal>(kLcdWidth - 1) / w;
    const CalcReal py = static_cast<CalcReal>(kPlotTop) +
                        (window.ymax - y) * static_cast<CalcReal>(plot_h) / h;
    if (px < real(-32768.0) || px > real(32767.0) || py < real(-32768.0) || py > real(32767.0)) {
        return false;
    }
    sx = static_cast<int>(std::lround(px));
    sy = static_cast<int>(std::lround(py));
    return true;
}

int clip_code(int x, int y) {
    int code = 0;
    if (x < 0) {
        code |= 1;
    } else if (x >= kLcdWidth) {
        code |= 2;
    }
    if (y < kPlotTop) {
        code |= 4;
    } else if (y > kPlotBottom) {
        code |= 8;
    }
    return code;
}

bool clip_line_to_plot(int& x0, int& y0, int& x1, int& y1) {
    int c0 = clip_code(x0, y0);
    int c1 = clip_code(x1, y1);
    while (true) {
        if ((c0 | c1) == 0) {
            return true;
        }
        if ((c0 & c1) != 0) {
            return false;
        }
        const int out = c0 != 0 ? c0 : c1;
        int x = 0;
        int y = 0;
        if ((out & 4) != 0) {
            if (y1 == y0) {
                return false;
            }
            x = x0 + (x1 - x0) * (kPlotTop - y0) / (y1 - y0);
            y = kPlotTop;
        } else if ((out & 8) != 0) {
            if (y1 == y0) {
                return false;
            }
            x = x0 + (x1 - x0) * (kPlotBottom - y0) / (y1 - y0);
            y = kPlotBottom;
        } else if ((out & 2) != 0) {
            if (x1 == x0) {
                return false;
            }
            y = y0 + (y1 - y0) * (kLcdWidth - 1 - x0) / (x1 - x0);
            x = kLcdWidth - 1;
        } else {
            if (x1 == x0) {
                return false;
            }
            y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
            x = 0;
        }
        if (out == c0) {
            x0 = x;
            y0 = y;
            c0 = clip_code(x0, y0);
        } else {
            x1 = x;
            y1 = y;
            c1 = clip_code(x1, y1);
        }
    }
}

void draw_clipped_plot_line(Display& display, int x0, int y0, int x1, int y1, Color color) {
    if (clip_line_to_plot(x0, y0, x1, y1)) {
        draw_line(display, x0, y0, x1, y1, color);
    }
}

bool evaluate_y_at(int eq, CalcReal x, CalcReal& y) {
    if (eq < 0 || eq >= kMaxYEquations || g.y_len[eq] == 0) {
        return false;
    }
    EvalResult result = evaluate_expression_with_x_readonly(g.y_expr[eq], g.eval, x);
    if (!result.ok || !near_zero(result.imag) || !std::isfinite(result.value)) {
        return false;
    }
    y = result.value;
    return true;
}

bool should_connect_graph_points(int eq, CalcReal x0, CalcReal y0, CalcReal x1, CalcReal y1) {
    const bool crosses_entire_window = (y0 < g.window.ymin && y1 > g.window.ymax) ||
                                       (y1 < g.window.ymin && y0 > g.window.ymax);
    if (!crosses_entire_window) {
        return true;
    }
    const CalcReal mid_x = (x0 + x1) * real(0.5);
    CalcReal mid_y = real(0.0);
    if (!evaluate_y_at(eq, mid_x, mid_y) || mid_y < g.window.ymin || mid_y > g.window.ymax) {
        return false;
    }
    return true;
}

struct TracePoi {
    char label[32];
    CalcReal x;
    CalcReal y;
};

CalcReal trace_step() {
    CalcReal step = (g.window.xmax - g.window.xmin) / real(200.0);
    if (!std::isfinite(step) || step <= real(0.0)) {
        step = real(0.1);
    }
    return step;
}

CalcReal clamp_trace_x(CalcReal x) {
    if (x < g.window.xmin) {
        return g.window.xmin;
    }
    if (x > g.window.xmax) {
        return g.window.xmax;
    }
    return x;
}

bool refine_intersection(int a, int b, CalcReal left, CalcReal right, CalcReal& out_x, CalcReal& out_y) {
    CalcReal fa_left = real(0.0);
    CalcReal fb_left = real(0.0);
    CalcReal fa_right = real(0.0);
    CalcReal fb_right = real(0.0);
    if (!evaluate_y_at(a, left, fa_left) || !evaluate_y_at(b, left, fb_left) ||
        !evaluate_y_at(a, right, fa_right) || !evaluate_y_at(b, right, fb_right)) {
        return false;
    }
    CalcReal d_left = fa_left - fb_left;
    CalcReal d_right = fa_right - fb_right;
    if (d_left == real(0.0)) {
        out_x = left;
        out_y = fa_left;
        return true;
    }
    if (d_right == real(0.0)) {
        out_x = right;
        out_y = fa_right;
        return true;
    }
    if ((d_left < real(0.0) && d_right < real(0.0)) || (d_left > real(0.0) && d_right > real(0.0))) {
        return false;
    }
    for (int i = 0; i < 18; ++i) {
        const CalcReal mid = (left + right) * real(0.5);
        CalcReal fa_mid = real(0.0);
        CalcReal fb_mid = real(0.0);
        if (!evaluate_y_at(a, mid, fa_mid) || !evaluate_y_at(b, mid, fb_mid)) {
            break;
        }
        const CalcReal d_mid = fa_mid - fb_mid;
        if (d_mid == real(0.0)) {
            left = mid;
            right = mid;
            fa_left = fa_mid;
            break;
        }
        if ((d_left < real(0.0) && d_mid > real(0.0)) || (d_left > real(0.0) && d_mid < real(0.0))) {
            right = mid;
            d_right = d_mid;
        } else {
            left = mid;
            d_left = d_mid;
            fa_left = fa_mid;
        }
    }
    out_x = (left + right) * real(0.5);
    return evaluate_y_at(a, out_x, out_y);
}

bool refine_extremum(int eq, bool maximum, CalcReal left, CalcReal right, CalcReal& out_x, CalcReal& out_y) {
    for (int i = 0; i < 18; ++i) {
        const CalcReal span = right - left;
        const CalcReal x1 = left + span / real(3.0);
        const CalcReal x2 = right - span / real(3.0);
        CalcReal y1 = real(0.0);
        CalcReal y2 = real(0.0);
        if (!evaluate_y_at(eq, x1, y1) || !evaluate_y_at(eq, x2, y2)) {
            break;
        }
        if (maximum ? (y1 < y2) : (y1 > y2)) {
            left = x1;
        } else {
            right = x2;
        }
    }
    out_x = (left + right) * real(0.5);
    return evaluate_y_at(eq, out_x, out_y);
}

bool x_inside_interval(CalcReal x, CalcReal a, CalcReal b) {
    const CalcReal step = trace_step();
    const CalcReal low = a < b ? a : b;
    const CalcReal high = a < b ? b : a;
    const CalcReal epsilon = step * real(0.00001);
    return x > low + epsilon && x < high - epsilon;
}

bool better_poi_candidate(const TracePoi& candidate, const TracePoi& best, bool have_best, CalcReal source_x, int direction) {
    if (!have_best) {
        return true;
    }
    const CalcReal candidate_distance = std::fabs(candidate.x - source_x);
    const CalcReal best_distance = std::fabs(best.x - source_x);
    const CalcReal epsilon = trace_step() * real(0.00001);
    if (candidate_distance + epsilon < best_distance) {
        return true;
    }
    if (std::fabs(candidate_distance - best_distance) <= epsilon) {
        return direction > 0 ? candidate.x < best.x : candidate.x > best.x;
    }
    return false;
}

bool find_intersection_poi_between(int other_eq, CalcReal from_x, CalcReal to_x, TracePoi& poi) {
    const CalcReal left = from_x < to_x ? from_x : to_x;
    const CalcReal right = from_x < to_x ? to_x : from_x;
    CalcReal selected_left = real(0.0);
    CalcReal selected_right = real(0.0);
    CalcReal other_left = real(0.0);
    CalcReal other_right = real(0.0);
    if (!evaluate_y_at(g.trace_eq, left, selected_left) || !evaluate_y_at(g.trace_eq, right, selected_right) ||
        !evaluate_y_at(other_eq, left, other_left) || !evaluate_y_at(other_eq, right, other_right)) {
        return false;
    }
    const CalcReal d_left = selected_left - other_left;
    const CalcReal d_right = selected_right - other_right;
    CalcReal poi_x = left;
    CalcReal poi_y = selected_left;
    if ((d_left < real(0.0) && d_right < real(0.0)) || (d_left > real(0.0) && d_right > real(0.0))) {
        return false;
    }
    if (!refine_intersection(g.trace_eq, other_eq, left, right, poi_x, poi_y)) {
        return false;
    }
    if (!x_inside_interval(poi_x, from_x, to_x)) {
        return false;
    }
    std::snprintf(poi.label, sizeof(poi.label), "Y%d~Y%d", g.trace_eq + 1, other_eq + 1);
    poi.x = poi_x;
    poi.y = poi_y;
    return true;
}

bool find_x_intercept_poi_between(CalcReal from_x, CalcReal to_x, TracePoi& poi) {
    const CalcReal left = from_x < to_x ? from_x : to_x;
    const CalcReal right = from_x < to_x ? to_x : from_x;
    CalcReal y_left = real(0.0);
    CalcReal y_right = real(0.0);
    if (!evaluate_y_at(g.trace_eq, left, y_left) || !evaluate_y_at(g.trace_eq, right, y_right)) {
        return false;
    }
    if ((y_left < real(0.0) && y_right < real(0.0)) || (y_left > real(0.0) && y_right > real(0.0))) {
        return false;
    }
    CalcReal lo = left;
    CalcReal hi = right;
    CalcReal y_lo = y_left;
    for (int i = 0; i < 18; ++i) {
        const CalcReal mid = (lo + hi) * real(0.5);
        CalcReal y_mid = real(0.0);
        if (!evaluate_y_at(g.trace_eq, mid, y_mid)) {
            break;
        }
        if (y_mid == real(0.0)) {
            lo = mid;
            hi = mid;
            y_lo = y_mid;
            break;
        }
        if ((y_lo < real(0.0) && y_mid > real(0.0)) || (y_lo > real(0.0) && y_mid < real(0.0))) {
            hi = mid;
        } else {
            lo = mid;
            y_lo = y_mid;
        }
    }
    const CalcReal poi_x = (lo + hi) * real(0.5);
    if (!x_inside_interval(poi_x, from_x, to_x)) {
        return false;
    }
    CalcReal poi_y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, poi_x, poi_y)) {
        return false;
    }
    std::snprintf(poi.label, sizeof(poi.label), "Y%d X-INT", g.trace_eq + 1);
    poi.x = poi_x;
    poi.y = poi_y;
    return true;
}

bool find_y_intercept_poi_between(CalcReal from_x, CalcReal to_x, TracePoi& poi) {
    if (!x_inside_interval(real(0.0), from_x, to_x)) {
        return false;
    }
    CalcReal poi_y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, real(0.0), poi_y)) {
        return false;
    }
    std::snprintf(poi.label, sizeof(poi.label), "Y%d Y-INT", g.trace_eq + 1);
    poi.x = real(0.0);
    poi.y = poi_y;
    return true;
}

void clear_trace_poi_labels() {
    g.trace_poi_active = false;
    g.trace_poi_special = false;
    g.trace_poi_count = 0;
    for (int i = 0; i < kMaxTracePoiLabels; ++i) {
        g.trace_poi_labels[i][0] = '\0';
    }
}

void add_trace_poi_label(const char* label) {
    if (label == nullptr || label[0] == '\0') {
        return;
    }
    for (int i = 0; i < g.trace_poi_count; ++i) {
        if (std::strcmp(g.trace_poi_labels[i], label) == 0) {
            return;
        }
    }
    if (g.trace_poi_count >= kMaxTracePoiLabels) {
        return;
    }
    copy_string(g.trace_poi_labels[g.trace_poi_count], sizeof(g.trace_poi_labels[g.trace_poi_count]), label);
    ++g.trace_poi_count;
    g.trace_poi_active = true;
}

bool trace_current_intercept_poi(TracePoi& poi) {
    if (!g.trace_active || g.trace_eq < 0) {
        return false;
    }
    const CalcReal x_epsilon = trace_step() * real(0.00001);
    const CalcReal y_epsilon = std::fabs(g.window.ymax - g.window.ymin) * real(0.000001);
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, y)) {
        return false;
    }
    const bool at_y_axis = std::fabs(g.trace_x) <= x_epsilon;
    const bool at_x_axis = std::fabs(y) <= y_epsilon;
    if (at_x_axis) {
        std::snprintf(poi.label, sizeof(poi.label), "Y%d X-INT", g.trace_eq + 1);
    } else if (at_y_axis) {
        std::snprintf(poi.label, sizeof(poi.label), "Y%d Y-INT", g.trace_eq + 1);
    } else {
        return false;
    }
    poi.x = g.trace_x;
    poi.y = y;
    return true;
}

void update_trace_current_poi_labels(bool special_stop) {
    const bool preserve_special = special_stop;
    clear_trace_poi_labels();
    g.trace_poi_special = preserve_special;
    if (!g.trace_active || g.trace_eq < 0) {
        return;
    }
    const CalcReal x_epsilon = trace_step() * real(0.0005);
    const CalcReal y_epsilon = std::fabs(g.window.ymax - g.window.ymin) * real(0.000001);
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, y)) {
        return;
    }

    const CalcReal left_x = clamp_trace_x(g.trace_x - trace_step());
    const CalcReal right_x = clamp_trace_x(g.trace_x + trace_step());
    if (left_x != right_x && g.trace_x > g.window.xmin && g.trace_x < g.window.xmax) {
        for (int mode = 0; mode < 2; ++mode) {
            const bool local_max = mode == 1;
            CalcReal poi_x = g.trace_x;
            CalcReal poi_y = y;
            if (refine_extremum(g.trace_eq, local_max, left_x, right_x, poi_x, poi_y) &&
                std::fabs(poi_x - g.trace_x) <= x_epsilon) {
                CalcReal y_left = real(0.0);
                CalcReal y_right = real(0.0);
                if (evaluate_y_at(g.trace_eq, left_x, y_left) && evaluate_y_at(g.trace_eq, right_x, y_right)) {
                    if ((local_max && poi_y >= y_left && poi_y >= y_right && (poi_y > y_left || poi_y > y_right)) ||
                        (!local_max && poi_y <= y_left && poi_y <= y_right && (poi_y < y_left || poi_y < y_right))) {
                        char label[32]{};
                        std::snprintf(label, sizeof(label), "Y%d LOCAL %s", g.trace_eq + 1, local_max ? "MAX" : "MIN");
                        add_trace_poi_label(label);
                    }
                }
            }
        }
    }

    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        if (eq == g.trace_eq || g.y_len[eq] == 0) {
            continue;
        }
        CalcReal other_y = real(0.0);
        if (evaluate_y_at(eq, g.trace_x, other_y) && std::fabs(other_y - y) <= y_epsilon) {
            char label[32]{};
            std::snprintf(label, sizeof(label), "Y%d~Y%d", g.trace_eq + 1, eq + 1);
            add_trace_poi_label(label);
        }
    }

    if (std::fabs(y) <= y_epsilon) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "Y%d X-INT", g.trace_eq + 1);
        add_trace_poi_label(label);
    }
    if (std::fabs(g.trace_x) <= x_epsilon) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "Y%d Y-INT", g.trace_eq + 1);
        add_trace_poi_label(label);
    }
}

bool find_extremum_poi_between(CalcReal from_x, CalcReal to_x, TracePoi& poi) {
    const CalcReal left = from_x < to_x ? from_x : to_x;
    const CalcReal right = from_x < to_x ? to_x : from_x;
    if (left == right) {
        return false;
    }
    CalcReal y_left = real(0.0);
    CalcReal y_right = real(0.0);
    if (!evaluate_y_at(g.trace_eq, left, y_left) || !evaluate_y_at(g.trace_eq, right, y_right)) {
        return false;
    }
    bool have_best = false;
    TracePoi best{};
    for (int mode = 0; mode < 2; ++mode) {
        const bool local_max = mode == 1;
        CalcReal poi_x = (left + right) * real(0.5);
        CalcReal poi_y = real(0.0);
        if (!refine_extremum(g.trace_eq, local_max, left, right, poi_x, poi_y) ||
            !x_inside_interval(poi_x, from_x, to_x)) {
            continue;
        }
        if (local_max) {
            if (poi_y < y_left || poi_y < y_right || (poi_y == y_left && poi_y == y_right)) {
                continue;
            }
        } else if (poi_y > y_left || poi_y > y_right || (poi_y == y_left && poi_y == y_right)) {
            continue;
        }
        TracePoi candidate{};
        std::snprintf(candidate.label, sizeof(candidate.label), "Y%d LOCAL %s", g.trace_eq + 1, local_max ? "MAX" : "MIN");
        candidate.x = poi_x;
        candidate.y = poi_y;
        if (better_poi_candidate(candidate, best, have_best, from_x, from_x < to_x ? 1 : -1)) {
            best = candidate;
            have_best = true;
        }
    }
    if (!have_best) {
        return false;
    }
    poi = best;
    return true;
}

bool find_trace_poi_between(CalcReal from_x, CalcReal to_x, int direction, TracePoi& poi) {
    if (!g.trace_active || g.trace_eq < 0 || from_x == to_x) {
        return false;
    }
    bool have_best = false;
    TracePoi best{};
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        if (eq == g.trace_eq || g.y_len[eq] == 0) {
            continue;
        }
        TracePoi candidate{};
        if (find_intersection_poi_between(eq, from_x, to_x, candidate) &&
            better_poi_candidate(candidate, best, have_best, from_x, direction)) {
            best = candidate;
            have_best = true;
        }
    }
    TracePoi extremum{};
    if (find_extremum_poi_between(from_x, to_x, extremum) &&
        better_poi_candidate(extremum, best, have_best, from_x, direction)) {
        best = extremum;
        have_best = true;
    }
    TracePoi x_intercept{};
    if (find_x_intercept_poi_between(from_x, to_x, x_intercept) &&
        better_poi_candidate(x_intercept, best, have_best, from_x, direction)) {
        best = x_intercept;
        have_best = true;
    }
    TracePoi y_intercept{};
    if (find_y_intercept_poi_between(from_x, to_x, y_intercept) &&
        better_poi_candidate(y_intercept, best, have_best, from_x, direction)) {
        best = y_intercept;
        have_best = true;
    }
    if (!have_best) {
        return false;
    }
    poi = best;
    return true;
}

int first_trace_equation(CalcReal x) {
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        CalcReal y = real(0.0);
        if (evaluate_y_at(eq, x, y)) {
            return eq;
        }
    }
    return -1;
}

constexpr int kTraceGridSteps = 200;

int clamp_trace_grid_index(int index) {
    if (index < 0) {
        return 0;
    }
    if (index > kTraceGridSteps) {
        return kTraceGridSteps;
    }
    return index;
}

CalcReal trace_grid_x_at(int index) {
    const CalcReal step = trace_step();
    return clamp_trace_x(g.window.xmin + static_cast<CalcReal>(clamp_trace_grid_index(index)) * step);
}

int trace_grid_index_for_x(CalcReal x) {
    const CalcReal step = trace_step();
    const CalcReal relative = (x - g.window.xmin) / step;
    return clamp_trace_grid_index(static_cast<int>(std::round(static_cast<double>(relative))));
}

int first_valid_trace_grid_index(int preferred_index, int& eq_out) {
    for (int radius = 0; radius <= kTraceGridSteps; ++radius) {
        for (int side = 0; side < 2; ++side) {
            if (radius == 0 && side == 1) {
                continue;
            }
            const int index = side == 0 ? preferred_index + radius : preferred_index - radius;
            if (index < 0 || index > kTraceGridSteps) {
                continue;
            }
            const CalcReal x = trace_grid_x_at(index);
            const int eq = first_trace_equation(x);
            if (eq >= 0) {
                eq_out = eq;
                return index;
            }
        }
    }
    eq_out = -1;
    return preferred_index;
}

bool trace_y_visible(int eq, CalcReal x) {
    CalcReal y = real(0.0);
    return evaluate_y_at(eq, x, y) && y >= g.window.ymin && y <= g.window.ymax;
}

void ensure_trace_cursor_visible() {
    if (!g.trace_active || g.trace_eq < 0) {
        return;
    }
    if (g.trace_x < g.window.xmin) {
        g.trace_grid_index = 0;
        g.trace_x = trace_grid_x_at(g.trace_grid_index);
        g.trace_poi_special = false;
    } else if (g.trace_x > g.window.xmax) {
        g.trace_grid_index = kTraceGridSteps;
        g.trace_x = trace_grid_x_at(g.trace_grid_index);
        g.trace_poi_special = false;
    } else if (!g.trace_poi_special) {
        g.trace_grid_index = trace_grid_index_for_x(g.trace_x);
    }

    if (trace_y_visible(g.trace_eq, g.trace_x)) {
        update_trace_current_poi_labels(g.trace_poi_special);
        return;
    }

    const int preferred = clamp_trace_grid_index(g.trace_grid_index);
    for (int radius = 0; radius <= kTraceGridSteps; ++radius) {
        for (int side = 0; side < 2; ++side) {
            if (radius == 0 && side == 1) {
                continue;
            }
            const int index = side == 0 ? preferred + radius : preferred - radius;
            if (index < 0 || index > kTraceGridSteps) {
                continue;
            }
            const CalcReal x = trace_grid_x_at(index);
            if (trace_y_visible(g.trace_eq, x)) {
                g.trace_grid_index = index;
                g.trace_x = x;
                g.trace_poi_special = false;
                update_trace_current_poi_labels(false);
                return;
            }
        }
    }

    for (int index = 0; index <= kTraceGridSteps; ++index) {
        const CalcReal x = trace_grid_x_at(index);
        for (int eq = 0; eq < kMaxYEquations; ++eq) {
            if (trace_y_visible(eq, x)) {
                g.trace_eq = eq;
                g.trace_grid_index = index;
                g.trace_x = x;
                g.trace_poi_special = false;
                update_trace_current_poi_labels(false);
                return;
            }
        }
    }
}

void clear_trace_entry() {
    g.trace_entry[0] = '\0';
    g.trace_entry_len = 0;
    g.trace_entry_cursor = 0;
}

bool insert_trace_entry_key(Key key) {
    return insert_window_value_key(key, g.trace_entry, g.trace_entry_len, g.trace_entry_cursor);
}

bool parse_trace_entry(CalcReal& value) {
    if (g.trace_entry_len == 0) {
        return false;
    }
    EvalContext copy = g.eval;
    EvalResult result = evaluate_expression(g.trace_entry, copy);
    if (!result.ok || !near_zero(result.imag) || !std::isfinite(result.value)) {
        return false;
    }
    value = result.value;
    return true;
}

void expand_axis_for_margin(CalcReal value, CalcReal& min_value, CalcReal& max_value) {
    CalcReal range = max_value - min_value;
    if (!std::isfinite(range) || range <= real(0.0)) {
        range = real(1.0);
        min_value = value - range * real(0.5);
        max_value = value + range * real(0.5);
    }
    constexpr CalcReal margin = static_cast<CalcReal>(0.2);
    const CalcReal low_margin = min_value + range * margin;
    const CalcReal high_margin = max_value - range * margin;
    if (value < low_margin) {
        const CalcReal new_range = (max_value - value) / (real(1.0) - margin);
        min_value = max_value - new_range;
    } else if (value > high_margin) {
        const CalcReal new_range = (value - min_value) / (real(1.0) - margin);
        max_value = min_value + new_range;
    }
}

void jump_trace_to_x(CalcReal x) {
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, x, y)) {
        const int eq = first_trace_equation(x);
        if (eq < 0 || !evaluate_y_at(eq, x, y)) {
            return;
        }
        g.trace_eq = eq;
    }
    const bool offscreen = x < g.window.xmin || x > g.window.xmax || y < g.window.ymin || y > g.window.ymax;
    if (offscreen) {
        expand_axis_for_margin(x, g.window.xmin, g.window.xmax);
        expand_axis_for_margin(y, g.window.ymin, g.window.ymax);
        sync_window_edit_from_values();
    }
    g.trace_x = x;
    g.trace_grid_index = trace_grid_index_for_x(x);
    update_trace_current_poi_labels(false);
    ensure_trace_cursor_visible();
    clear_trace_entry();
}

void start_trace() {
    g.trace_x = (g.window.xmin + g.window.xmax) * real(0.5);
    const int preferred_index = trace_grid_index_for_x(g.trace_x);
    g.trace_grid_index = first_valid_trace_grid_index(preferred_index, g.trace_eq);
    g.trace_x = trace_grid_x_at(g.trace_grid_index);
    g.trace_active = g.trace_eq >= 0;
    update_trace_current_poi_labels(false);
    clear_trace_entry();
}

void move_trace_horizontal(int direction) {
    clear_trace_entry();
    int interval_index = g.trace_grid_index;
    int target_index = g.trace_grid_index;
    if (g.trace_poi_special) {
        target_index = direction > 0 ? g.trace_grid_index + 1 : g.trace_grid_index;
    } else {
        target_index = g.trace_grid_index + direction;
        interval_index = direction > 0 ? g.trace_grid_index : g.trace_grid_index - 1;
    }
    target_index = clamp_trace_grid_index(target_index);
    interval_index = clamp_trace_grid_index(interval_index);
    const CalcReal next_grid = trace_grid_x_at(target_index);
    TracePoi poi{};
    CalcReal scan_from = g.trace_x;
    if (g.trace_poi_special) {
        const CalcReal skip = trace_step() * real(0.001);
        scan_from = clamp_trace_x(g.trace_x + skip * static_cast<CalcReal>(direction));
        if ((direction > 0 && scan_from > next_grid) || (direction < 0 && scan_from < next_grid)) {
            scan_from = next_grid;
        }
    }
    if (next_grid != g.trace_x && scan_from != next_grid && find_trace_poi_between(scan_from, next_grid, direction, poi)) {
        g.trace_x = poi.x;
        g.trace_grid_index = interval_index;
        update_trace_current_poi_labels(true);
        if (g.trace_poi_count == 0) {
            add_trace_poi_label(poi.label);
            g.trace_poi_special = true;
        }
    } else {
        g.trace_x = next_grid;
        g.trace_grid_index = target_index;
        update_trace_current_poi_labels(false);
    }
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, y)) {
        g.trace_eq = first_trace_equation(g.trace_x);
        g.trace_active = g.trace_eq >= 0;
        clear_trace_poi_labels();
    }
}

void move_trace_vertical(int direction) {
    clear_trace_entry();
    CalcReal current_y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, current_y)) {
        g.trace_eq = first_trace_equation(g.trace_x);
        g.trace_active = g.trace_eq >= 0;
        return;
    }
    int best_eq = -1;
    CalcReal best_delta = real(0.0);
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        if (eq == g.trace_eq) {
            continue;
        }
        CalcReal candidate_y = real(0.0);
        if (!evaluate_y_at(eq, g.trace_x, candidate_y)) {
            continue;
        }
        const CalcReal delta = candidate_y - current_y;
        if ((direction > 0 && delta <= real(0.0)) || (direction < 0 && delta >= real(0.0))) {
            continue;
        }
        const CalcReal distance = delta < real(0.0) ? -delta : delta;
        if (best_eq < 0 || distance < best_delta) {
            best_eq = eq;
            best_delta = distance;
        }
    }
    if (best_eq >= 0) {
        g.trace_eq = best_eq;
        update_trace_current_poi_labels(false);
    }
}

void draw_trace_cursor(Display& display) {
    if (!g.trace_active) {
        return;
    }
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, y)) {
        return;
    }
    int sx = 0;
    int sy = 0;
    if (!graph_to_plot_point(g.window, g.trace_x, y, sx, sy)) {
        return;
    }
    static constexpr const char* kTraceCursorOn[9] = {
        "###...###",
        "####.####",
        "#########",
        ".###.###.",
        "..#...#..",
        ".###.###.",
        "#########",
        "####.####",
        "###...###",
    };
    static constexpr const char* kTraceCursorOff[9] = {
        "###...###",
        "#..#.#..#",
        "#..###..#",
        ".#.#.#.#.",
        "..#...#..",
        ".#.#.#.#.",
        "#..###..#",
        "#..#.#..#",
        "###...###",
    };
    const char* const* glyph = g.cursor_on ? kTraceCursorOn : kTraceCursorOff;
    for (int row = 0; row < 9; ++row) {
        const int py = sy + row - 4;
        if (py < kPlotTop || py > kPlotBottom) {
            continue;
        }
        for (int col = 0; col < 9; ++col) {
            const int px = sx + col - 4;
            if (px < 0 || px >= kLcdWidth || glyph[row][col] != '#') {
                continue;
            }
            set_pixel(display, px, py, kBlack);
        }
    }
}

void draw_trace_readouts(Display& display) {
    if (!g.trace_active) {
        return;
    }
    CalcReal y = real(0.0);
    if (!evaluate_y_at(g.trace_eq, g.trace_x, y)) {
        return;
    }
    char value[24]{};
    char text[32]{};
    if (g.trace_entry_len > 0) {
        std::snprintf(text, sizeof(text), "X=%s", g.trace_entry);
    } else {
        format_table_value(g.trace_x, value, sizeof(value), 10);
        std::snprintf(text, sizeof(text), "X=%s", value);
    }
    const int x_text_w = static_cast<int>(std::strlen(text)) * 8;
    fill_rect(display, 25, kLcdHeight - 16, x_text_w + 4, 14, kWhite);
    draw_text_scaled(display, 25, kLcdHeight - 14, text, 1, kBlack, kWhite);
    format_table_value(y, value, sizeof(value), 10);
    std::snprintf(text, sizeof(text), "Y=%s", value);
    const int y_text_w = static_cast<int>(std::strlen(text)) * 8;
    fill_rect(display, 170, kLcdHeight - 16, y_text_w + 4, 14, kWhite);
    draw_text_scaled(display, 170, kLcdHeight - 14, text, 1, graph_color(g.trace_eq), kWhite);

    for (int i = 0; i < g.trace_poi_count; ++i) {
        const int row_y = kLcdHeight - 28 - (g.trace_poi_count - 1 - i) * 12;
        const int poi_text_w = static_cast<int>(std::strlen(g.trace_poi_labels[i])) * 8;
        fill_rect(display, 25, row_y - 2, poi_text_w + 4, 12, kWhite);
        draw_text_scaled(display, 25, row_y, g.trace_poi_labels[i], 1, graph_color(g.trace_eq), kWhite);
    }

    char label[6]{};
    std::snprintf(label, sizeof(label), "Y%d=", g.trace_eq + 1);
    const ExprBox box = measure_expression_range(g.y_expr[g.trace_eq], 0, g.y_len[g.trace_eq]);
    int expr_y = kTitleH + 2;
    if (expr_y + box.h > kLcdHeight - 18) {
        expr_y = kLcdHeight - 18 - box.h;
    }
    if (expr_y < kTitleH) {
        expr_y = kTitleH;
    }
    constexpr int label_x = 25;
    constexpr int expr_x = 55;
    const int visible_expr_w = box.w > kLcdWidth - expr_x - 2 ? kLcdWidth - expr_x - 2 : box.w;
    const int bg_w = (expr_x - label_x) + visible_expr_w + 2;
    fill_rect(display, label_x, expr_y - 2, bg_w, box.h + 4, kWhite);
    draw_text_scaled(display, label_x, expr_y + (box.h > 14 ? (box.h - 14) / 2 : 0), label, 1, graph_color(g.trace_eq), kWhite);
    int expr_scroll = 0;
    if (box.w > kLcdWidth - expr_x - 2) {
        expr_scroll = box.w - (kLcdWidth - expr_x - 2);
    }
    draw_expression(display, expr_x - expr_scroll, expr_y, g.y_expr[g.trace_eq], -1, graph_color(g.trace_eq), kWhite);
}

CalcReal graph_grid_step(CalcReal min_value, CalcReal max_value) {
    CalcReal range = max_value - min_value;
    if (!std::isfinite(range) || range <= real(0.0)) {
        return real(1.0);
    }
    CalcReal target = range / real(20.0);
    if (target < real(1.0)) {
        return real(1.0);
    }
    CalcReal base = real(1.0);
    while (base * real(10.0) <= target) {
        base *= real(10.0);
    }
    CalcReal step = base;
    if (target > base * real(5.0)) {
        step = base * real(10.0);
    } else if (target > base * real(2.0)) {
        step = base * real(5.0);
    } else if (target > base) {
        step = base * real(2.0);
    }
    if (step < real(1.0)) {
        return real(1.0);
    }
    return std::floor(step + real(0.5));
}

void format_grid_label(CalcReal value, char* out, std::size_t size) {
    if (out == nullptr || size == 0u) {
        return;
    }
    std::snprintf(out, size, "%.0f", static_cast<double>(value));
}

bool grid_tick_value(CalcReal step, CalcReal min_value, CalcReal max_value, CalcReal& tick) {
    if (step >= min_value && step <= max_value) {
        tick = step;
        return true;
    }
    if (-step >= min_value && -step <= max_value) {
        tick = -step;
        return true;
    }
    return false;
}

void draw_grid_step_labels(Display& display, CalcReal x_step, CalcReal y_step) {
    const Color tick_color = kBlack;
    CalcReal tick = real(0.0);
    int sx = 0;
    int sy = 0;
    if (real(0.0) >= g.window.ymin && real(0.0) <= g.window.ymax &&
        grid_tick_value(x_step, g.window.xmin, g.window.xmax, tick) &&
        graph_to_plot_point(g.window, tick, real(0.0), sx, sy)) {
        draw_line(display, sx, sy - 3, sx, sy + 3, tick_color);
        char label[16]{};
        format_grid_label(tick, label, sizeof(label));
        int label_y = sy + 5;
        if (label_y + 10 > kPlotBottom) {
            label_y = sy - 13;
        }
        const int label_w = static_cast<int>(std::strlen(label)) * 8;
        int label_x = sx - label_w / 2;
        if (label_x < 0) {
            label_x = 0;
        } else if (label_x + label_w > kLcdWidth) {
            label_x = kLcdWidth - label_w;
        }
        fill_rect(display, label_x, label_y, label_w + 2, 10, kWhite);
        draw_text_scaled(display, label_x, label_y, label, 1, tick_color, kWhite);
    }
    if (real(0.0) >= g.window.xmin && real(0.0) <= g.window.xmax &&
        grid_tick_value(y_step, g.window.ymin, g.window.ymax, tick) &&
        graph_to_plot_point(g.window, real(0.0), tick, sx, sy)) {
        draw_line(display, sx - 3, sy, sx + 3, sy, tick_color);
        char label[16]{};
        format_grid_label(tick, label, sizeof(label));
        const int label_w = static_cast<int>(std::strlen(label)) * 8;
        int label_x = sx - label_w - 5;
        if (label_x < 0) {
            label_x = sx + 5;
        }
        if (label_x + label_w > kLcdWidth) {
            label_x = kLcdWidth - label_w;
        }
        int label_y = sy - 5;
        if (label_y < kPlotTop) {
            label_y = kPlotTop;
        } else if (label_y + 10 > kPlotBottom) {
            label_y = kPlotBottom - 10;
        }
        fill_rect(display, label_x, label_y, label_w + 2, 10, kWhite);
        draw_text_scaled(display, label_x, label_y, label, 1, tick_color, kWhite);
    }
}

void draw_axes(Display& display) {
    int sx = 0;
    int sy = 0;
    const Color grid = rgb565(210, 210, 210);
    const CalcReal x_step = graph_grid_step(g.window.xmin, g.window.xmax);
    const CalcReal y_step = graph_grid_step(g.window.ymin, g.window.ymax);
    const CalcReal first_x = std::ceil(g.window.xmin / x_step) * x_step;
    for (CalcReal x = first_x; x <= g.window.xmax + x_step * real(0.001); x += x_step) {
        if (std::fabs(x) < x_step * real(0.0001)) {
            x = real(0.0);
        }
        if (graph_to_plot_point(g.window, x, g.window.ymin, sx, sy) && sx >= 0 && sx < kLcdWidth) {
            draw_line(display, sx, kPlotTop, sx, kPlotBottom, grid);
        }
    }
    const CalcReal first_y = std::ceil(g.window.ymin / y_step) * y_step;
    for (CalcReal y = first_y; y <= g.window.ymax + y_step * real(0.001); y += y_step) {
        if (std::fabs(y) < y_step * real(0.0001)) {
            y = real(0.0);
        }
        if (graph_to_plot_point(g.window, g.window.xmin, y, sx, sy) && sy >= kPlotTop && sy <= kPlotBottom) {
            draw_line(display, 0, sy, kLcdWidth - 1, sy, grid);
        }
    }
    int ax0 = 0;
    int ay0 = 0;
    int ax1 = 0;
    int ay1 = 0;
    if (graph_to_plot_point(g.window, 0.0, g.window.ymin, ax0, ay0) &&
        graph_to_plot_point(g.window, 0.0, g.window.ymax, ax1, ay1) && ax0 >= 0 && ax0 < kLcdWidth) {
        draw_line(display, ax0, kPlotTop, ax0, kPlotBottom, kBlack);
    }
    if (graph_to_plot_point(g.window, g.window.xmin, 0.0, ax0, ay0) &&
        graph_to_plot_point(g.window, g.window.xmax, 0.0, ax1, ay1) && ay0 >= kPlotTop && ay0 <= kPlotBottom) {
        draw_line(display, 0, ay0, kLcdWidth - 1, ay0, kBlack);
    }
}

std::uint32_t graph_sample_fingerprint() {
    std::uint32_t hash = 2166136261u;
    auto add_bytes = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    };
    add_bytes(&g.window, sizeof(g.window));
    add_bytes(g.y_expr, sizeof(g.y_expr));
    add_bytes(g.y_len, sizeof(g.y_len));
    add_bytes(g.eval.variables, sizeof(g.eval.variables));
    add_bytes(g.eval.variable_imag, sizeof(g.eval.variable_imag));
    add_bytes(g.eval.variable_valid, sizeof(g.eval.variable_valid));
    add_bytes(&g.eval.degree_mode, sizeof(g.eval.degree_mode));
    return hash;
}

void prepare_graph_sample_cache() {
    const std::uint32_t fingerprint = graph_sample_fingerprint();
    if (g_graph_sample_cache_valid && fingerprint == g_graph_sample_fingerprint) {
        return;
    }
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        for (int px = 0; px < kLcdWidth; ++px) {
            g_graph_sample_y[eq][px] = kGraphSampleMissing;
        }
    }
    g_graph_sample_fingerprint = fingerprint;
    g_graph_sample_cache_valid = true;
}

struct GraphPixelSample {
    bool valid;
    int y;
};

GraphPixelSample sample_graph_pixel(int eq, const CompiledExpression* compiled, int px) {
    const std::int16_t cached = g_graph_sample_y[eq][px];
    if (cached != kGraphSampleMissing) {
        return {cached != kGraphSampleInvalid, cached == kGraphSampleInvalid ? 0 : cached};
    }

    ++g_last_graph_evaluations;
    const CalcReal x = screen_to_graph_x(g.window, px);
    EvalResult result = compiled != nullptr
                            ? evaluate_compiled_with_x_readonly(*compiled, g.eval, x)
                            : evaluate_expression_with_x_readonly(g.y_expr[eq], g.eval, x);
    int sx = 0;
    int sy = 0;
    if (!result.ok || !near_zero(result.imag) || !std::isfinite(result.value) ||
        !graph_to_plot_point(g.window, x, result.value, sx, sy)) {
        g_graph_sample_y[eq][px] = kGraphSampleInvalid;
        return {false, 0};
    }
    if (sy < -32760) sy = -32760;
    if (sy > 32760) sy = 32760;
    g_graph_sample_y[eq][px] = static_cast<std::int16_t>(sy);
    return {true, sy};
}

bool graph_segment_crosses_window(const GraphPixelSample& lhs, const GraphPixelSample& rhs) {
    return lhs.valid && rhs.valid &&
           ((lhs.y < kPlotTop && rhs.y > kPlotBottom) || (rhs.y < kPlotTop && lhs.y > kPlotBottom));
}

void draw_adaptive_graph_segment(Display& display,
                                 int eq,
                                 const CompiledExpression* compiled,
                                 int x0,
                                 GraphPixelSample y0,
                                 int x1,
                                 GraphPixelSample y1,
                                 Color color) {
    const int span = x1 - x0;
    if (span <= 1) {
        if (y0.valid && y1.valid && !graph_segment_crosses_window(y0, y1)) {
            draw_clipped_plot_line(display, x0, y0.y, x1, y1.y, color);
        }
        return;
    }

    const int mid_x = x0 + span / 2;
    const GraphPixelSample mid = sample_graph_pixel(eq, compiled, mid_x);
    const int linear_mid = y0.valid && y1.valid ? (y0.y + y1.y) / 2 : 0;
    const bool smooth = y0.valid && mid.valid && y1.valid &&
                        std::abs(mid.y - linear_mid) <= 1 &&
                        !(graph_segment_crosses_window(y0, y1) &&
                          (mid.y < kPlotTop || mid.y > kPlotBottom));
    if (smooth) {
        draw_clipped_plot_line(display, x0, y0.y, mid_x, mid.y, color);
        draw_clipped_plot_line(display, mid_x, mid.y, x1, y1.y, color);
        return;
    }
    draw_adaptive_graph_segment(display, eq, compiled, x0, y0, mid_x, mid, color);
    draw_adaptive_graph_segment(display, eq, compiled, mid_x, mid, x1, y1, color);
}

void render_graph(Display& display) {
    clear(display, kWhite);
    title(display, "GRAPH");
    commit_window_edits();
    ensure_trace_cursor_visible();
    draw_axes(display);

    bool have_any = false;
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        if (g.y_len[eq] > 0) {
            have_any = true;
            break;
        }
    }
    if (!have_any) {
        draw_text_scaled(display, 8, 22, "Y EMPTY - PRESS Y=", kTextScale, kRed, kWhite);
        return;
    }

    prepare_graph_sample_cache();
    g_last_graph_evaluations = 0;
    for (int eq = 0; eq < kMaxYEquations; ++eq) {
        if (g.y_len[eq] == 0) {
            continue;
        }
        CompiledExpression compiled{};
        const bool needs_samples = g_graph_sample_y[eq][0] == kGraphSampleMissing;
        const CompiledExpression* compiled_ptr =
            needs_samples && compile_expression(g.y_expr[eq], compiled) ? &compiled : nullptr;
        const Color color = graph_color(eq);
        int left_x = 0;
        GraphPixelSample left = sample_graph_pixel(eq, compiled_ptr, left_x);
        while (left_x < kLcdWidth - 1) {
            int right_x = left_x + 8;
            if (right_x >= kLcdWidth) right_x = kLcdWidth - 1;
            const GraphPixelSample right = sample_graph_pixel(eq, compiled_ptr, right_x);
            draw_adaptive_graph_segment(display, eq, compiled_ptr, left_x, left, right_x, right, color);
            left_x = right_x;
            left = right;
        }
    }
    draw_grid_step_labels(display,
                          graph_grid_step(g.window.xmin, g.window.xmax),
                          graph_grid_step(g.window.ymin, g.window.ymax));
    draw_trace_readouts(display);
    draw_trace_cursor(display);
}

void render_y(Display& display) {
    clear(display, kWhite);
    title(display, "Y=");
    constexpr int expr_x = 66;
    ensure_y_selection_visible();
    int y = 22;
    for (int i = g.y_first_row; i < kMaxYEquations; ++i) {
        const int row_h = y_row_height(i);
        if (y + row_h > 210) {
            break;
        }
        const Color bg = g.y_selection == i ? kLightGray : kWhite;
        if (g.y_selection == i) {
            fill_rect(display, 0, y - 2, kLcdWidth, row_h, kLightGray);
        }
        const int label_y = y + (row_h > 18 ? (row_h - 18) / 2 : 0);
        fill_rect(display, 5, label_y + 3, 10, 10, graph_color(i));
        char label[6]{};
        std::snprintf(label, sizeof(label), "Y%d=", i + 1);
        draw_text_scaled(display, 20, label_y + 2, label, 1, kBlack, bg);
        if (g.y_selection == i) {
            ensure_cursor_visible(g.y_expr[i], g.y_len[i], g.y_cursor[i], kLcdWidth - expr_x - 4, g.y_expr_scroll_x[i]);
        }
        const ExprBox expr_box = measure_expression_range(g.y_expr[i], 0, g.y_len[i]);
        int expr_y = y + (row_h - expr_box.h) / 2;
        if (expr_y < y) {
            expr_y = y;
        }
        draw_expression(display,
                        expr_x - g.y_expr_scroll_x[i],
                        expr_y,
                        g.y_expr[i],
                        g.y_selection == i ? g.y_cursor[i] : -1,
                        kBlack,
                        bg);
        fill_rect(display, 0, y - 2, expr_x, row_h, bg);
        fill_rect(display, 5, label_y + 3, 10, 10, graph_color(i));
        draw_text_scaled(display, 20, label_y + 2, label, 1, kBlack, bg);
        y += row_h;
    }
    draw_text_scaled(display, 4, 210, "ENTER OR GRAPH TO PLOT", kTextScale, kBlue, kWhite);
}

void render_window(Display& display) {
    clear(display, kWhite);
    title(display, "WINDOW");
    const char* labels[kWindowNumericRows] = {"XMIN", "XMAX", "YMIN", "YMAX", "TBLSTRT", "TBLSTEP"};
    for (int i = 0; i < kWindowNumericRows; ++i) {
        const int y = 22 + i * 18;
        const Color bg = g.window_selection == i ? kLightGray : kWhite;
        if (g.window_selection == i) {
            fill_rect(display, 2, y - 2, kLcdWidth - 4, 15, kLightGray);
        }
        char label[10]{};
        std::size_t pos = 0;
        append_string(label, sizeof(label), pos, labels[i]);
        append_char(label, sizeof(label), pos, '=');
        draw_text_scaled(display, 8, y, label, kTextScale, kBlack, bg);
        draw_expression(display,
                        104,
                        y,
                        g.window_edit[i],
                        g.window_selection == i ? g.window_edit_cursor[i] : -1,
                        kBlack,
                        bg);
    }
    const int mode_y = 22 + kWindowNumericRows * 18;
    draw_text_scaled(display, 8, mode_y, "TBLMODE=", kTextScale, kBlack, kWhite);
    const bool auto_mode = g.table_auto;
    const bool mode_cursor = g.window_selection == kWindowNumericRows;
    const Color auto_bg = mode_cursor && auto_mode ? kBlue : (auto_mode ? kLightGray : kWhite);
    const Color manual_bg = mode_cursor && !auto_mode ? kBlue : (!auto_mode ? kLightGray : kWhite);
    draw_text_scaled(display, 104, mode_y, "AUTO", kTextScale, auto_bg == kBlue ? kWhite : kBlack, auto_bg);
    draw_text_scaled(display, 170, mode_y, "MANUAL", kTextScale, manual_bg == kBlue ? kWhite : kBlack, manual_bg);
    draw_text_scaled(display, 4, 210, "UP/DOWN SELECT  TYPE VALUE", kTextScale, kBlue, kWhite);
}

void table_header_label(int col, char* out, std::size_t size) {
    if (col == 0) {
        copy_string(out, size, "X");
    } else {
        std::snprintf(out, size, "Y%d", col);
    }
}

void table_cell_text(int row, int col, char* out, std::size_t size, int max_chars) {
    out[0] = '\0';
    CalcReal x = real(0.0);
    if (g.table_auto) {
        x = round_table_value(table_x_for_row(row));
    } else {
        if (!table_manual_x_value(row, x)) {
            return;
        }
        x = round_table_value(x);
    }
    if (col == 0) {
        format_table_value(x, out, size, max_chars);
        return;
    }
    const int eq = col - 1;
    if (eq < 0 || eq >= kMaxYEquations || g.y_len[eq] == 0) {
        return;
    }
    EvalResult result = evaluate_expression_with_x_readonly(g.y_expr[eq], g.eval, x);
    if (!result.ok) {
        copy_string(out, size, "ERR");
        return;
    }
    if (near_zero(result.imag) && !g.fraction_output) {
        format_table_value(result.value, out, size, max_chars);
    } else {
        format_result_value(result, out, size);
    }
}

void render_table(Display& display) {
    clear(display, kWhite);
    title(display, g.table_auto ? "TABLE AUTO" : "TABLE MANUAL");
    ensure_table_selection_visible();
    constexpr int x_col_w = 64;
    constexpr int y_col_w = (kLcdWidth - x_col_w) / kTableVisibleYColumns;
    constexpr int header_y = kTitleH;
    constexpr int header_h = 18;
    constexpr int row_y0 = kTitleH + header_h;
    constexpr int row_h = (kLcdHeight - row_y0) / kTableVisibleRows;

    fill_rect(display, 0, header_y, x_col_w, header_h, kLightGray);
    draw_rect(display, 0, header_y, x_col_w, header_h, kGray);
    draw_text_scaled(display, 4, header_y + 4, "X", 1, kBlack, kLightGray);
    for (int visible_col = 0; visible_col < kTableVisibleYColumns; ++visible_col) {
        const int col = g.table_first_col + visible_col;
        const int x = x_col_w + visible_col * y_col_w;
        fill_rect(display, x, header_y, y_col_w, header_h, kLightGray);
        draw_rect(display, x, header_y, y_col_w, header_h, kGray);
        char label[8]{};
        table_header_label(col, label, sizeof(label));
        draw_text_scaled(display, x + 4, header_y + 4, label, 1, graph_color(col - 1), kLightGray);
    }

    for (int visible_row = 0; visible_row < kTableVisibleRows; ++visible_row) {
        const int row = g.table_first_row + visible_row;
        const int y = row_y0 + visible_row * row_h;
        const bool x_selected = row == g.table_row && g.table_col == 0;
        const Color x_bg = x_selected ? kLightGray : kWhite;
        fill_rect(display, 0, y, x_col_w, row_h, x_bg);
        draw_rect(display, 0, y, x_col_w, row_h, kGray);
        if (!g.table_auto) {
            ManualTableEntry* entry = find_manual_table_entry(row);
            const char* text = entry == nullptr ? "" : entry->text;
            const int cursor = x_selected && entry != nullptr ? entry->cursor : -1;
            draw_expression(display, 4, y + 2, text, cursor, kBlack, x_bg);
        } else {
            char text[32]{};
            table_cell_text(row, 0, text, sizeof(text), (x_col_w - 6) / 8);
            draw_text_scaled(display, 4, y + 4, text, 1, kBlack, x_bg);
        }
        for (int visible_col = 0; visible_col < kTableVisibleYColumns; ++visible_col) {
            const int col = g.table_first_col + visible_col;
            const int x = x_col_w + visible_col * y_col_w;
            const bool selected = row == g.table_row && col == g.table_col;
            const Color bg = selected ? kLightGray : kWhite;
            fill_rect(display, x, y, y_col_w, row_h, bg);
            draw_rect(display, x, y, y_col_w, row_h, kGray);
            char text[32]{};
            table_cell_text(row, col, text, sizeof(text), (y_col_w - 6) / 8);
            draw_text_scaled(display, x + 4, y + 4, text, 1, graph_color(col - 1), bg);
        }
    }
}

void render_math_menu(Display& display) {
    clear(display, kWhite);
    title(display, "MATH");
    const int tab_y = kTitleH;
    const int tab_h = 16;
    const int tab_w = kLcdWidth / kMathTabCount;
    for (int i = 0; i < kMathTabCount; ++i) {
        const int x = i * tab_w;
        const bool active = i == g.math_tab;
        fill_rect(display, x, tab_y, tab_w, tab_h, active ? kBlue : kLightGray);
        draw_rect(display, x, tab_y, tab_w, tab_h, kGray);
        draw_text_scaled(display, x + 4, tab_y + 3, kMathTabs[i].title, 1, active ? kWhite : kBlack, active ? kBlue : kLightGray);
    }

    ensure_math_selection_visible();
    const MathMenuTab& tab = current_math_tab();
    const int row_h = 16;
    const int list_y = tab_y + tab_h + 2;
    for (int row = 0; row < kMathVisibleRows; ++row) {
        const int item_index = g.math_first_row + row;
        if (item_index >= tab.count) {
            break;
        }
        const MathMenuItem& item = tab.items[item_index];
        const int y = list_y + row * row_h;
        const bool selected = item_index == g.math_row;
        const Color bg = selected ? kBlue : kWhite;
        const Color fg = selected ? kWhite : kBlack;
        fill_rect(display, 0, y, kLcdWidth, row_h, bg);
        char direct[3] = {item.direct, ':', '\0'};
        draw_text_scaled(display, 8, y + 1, direct, kTextScale, fg, bg);
        draw_text_scaled(display, 36, y + 1, item.label, kTextScale, fg, bg);
    }
    if (g.math_first_row > 0) {
        draw_text_scaled(display, kLcdWidth - 12, list_y, "^", 1, kBlack, kWhite);
    }
    if (g.math_first_row + kMathVisibleRows < tab.count) {
        draw_text_scaled(display, kLcdWidth - 12, kLcdHeight - 12, "v", 1, kBlack, kWhite);
    }
}

void render_solver(Display& display) {
    clear(display, kWhite);
    title(display, "SOLVER");
    const int row_y[5] = {24, 62, 84, 106, 128};
    const char* labels[5] = {"EQ=", "VAR=", "LOW=", "HIGH=", "GUESS="};
    for (int row = 0; row < 5; ++row) {
        const bool selected = g.solver_selection == row;
        const Color bg = selected ? kLightGray : kWhite;
        fill_rect(display, 0, row_y[row] - 2, kLcdWidth, row == 0 ? 34 : 18, bg);
        draw_text_scaled(display, 8, row_y[row], labels[row], kTextScale, kBlack, bg);
        if (row == 0) {
            const int expr_x = 52 - g.solver_scroll_x;
            draw_expression(display, expr_x, row_y[row] - 2, g.solver_expr, selected ? g.solver_cursor : -1, kBlack, bg);
        } else if (row == 1) {
            char text[2] = {g.solver_variable, '\0'};
            draw_text_scaled(display, 68, row_y[row], text, kTextScale, kBlack, bg);
        } else {
            const int value_row = row - 2;
            draw_text_scaled(display, 92, row_y[row], g.solver_value[value_row], kTextScale, kBlack, bg);
            if (selected && g.cursor_on) {
                const int cx = 92 + g.solver_value_cursor[value_row] * kTextW;
                draw_line(display, cx, row_y[row], cx, row_y[row] + kTextH, kBlack);
            }
        }
    }
    if (g.solver_status[0] != '\0') {
        draw_text_scaled(display, 8, 166, g.solver_status, kTextScale, kBlue, kWhite);
    }
    draw_text_scaled(display, 8, 210, "ENTER SOLVES", kTextScale, kBlue, kWhite);
}

void render_settings(Display& display) {
    clear(display, kWhite);
    title(display, "SETTINGS");
    const int row_y[2] = {24, 42};

    draw_text_scaled(display, 8, row_y[0], "ANGLE:", kTextScale, kBlack, kWhite);
    const bool radians = !g.eval.degree_mode;
    const Color radians_bg = g.settings_selection == 0 && radians ? kBlue : (radians ? kLightGray : kWhite);
    const Color degrees_bg = g.settings_selection == 0 && !radians ? kBlue : (!radians ? kLightGray : kWhite);
    draw_text_scaled(display, 78, row_y[0], "RADIANS", kTextScale, radians_bg == kBlue ? kWhite : kBlack, radians_bg);
    draw_text_scaled(display, 166, row_y[0], "DEGREES", kTextScale, degrees_bg == kBlue ? kWhite : kBlack, degrees_bg);

    draw_text_scaled(display, 8, row_y[1], "OUTPUT:", kTextScale, kBlack, kWhite);
    const bool decimal = !g.fraction_output;
    const Color decimal_bg = g.settings_selection == 1 && decimal ? kBlue : (decimal ? kLightGray : kWhite);
    const Color fraction_bg = g.settings_selection == 1 && !decimal ? kBlue : (!decimal ? kLightGray : kWhite);
    draw_text_scaled(display, 86, row_y[1], "DECIMAL", kTextScale, decimal_bg == kBlue ? kWhite : kBlack, decimal_bg);
    draw_text_scaled(display, 174, row_y[1], "FRACTION", kTextScale, fraction_bg == kBlue ? kWhite : kBlack, fraction_bg);
#if defined(CALC_USE_FLOAT) && CALC_USE_FLOAT
    draw_text_scaled(display, 8, 104, "REAL: FLOAT", kTextScale, kBlack, kWhite);
#else
    draw_text_scaled(display, 8, 104, "REAL: DOUBLE", kTextScale, kBlack, kWhite);
#endif
    draw_text_scaled(display, 8, 120, "DISPLAY: RGB565 320X240", kTextScale, kBlack, kWhite);
    draw_text_scaled(display, 8, 144, "FIXED BUFFERS ENABLED", kTextScale, kGreen, kWhite);
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
    g.window = {real(-10.0), real(10.0), real(-10.0), real(10.0)};
    g.table_auto = true;
    g.table_start = real(0.0);
    g.table_step = real(1.0);
    g.table_row = 0;
    g.table_col = 0;
    g.table_first_row = 0;
    g.table_first_col = 1;
    g.y_selection = 0;
    g.y_first_row = 0;
    g.trace_active = false;
    g.trace_eq = 0;
    g.trace_x = real(0.0);
    g.trace_grid_index = 0;
    clear_trace_poi_labels();
    clear_trace_entry();
    sync_window_edit_from_values();
    reset_home_expression();
    g.cursor_on = true;
    g.history_selection = -1;
    g.edit_cursor_before_history = 0;
    g.settings_selection = 0;
    g.zoom_pending = false;
    g.render_dirty = true;
    g.fraction_output = false;
    g.math_return_screen = Screen::Home;
    g.math_tab = 0;
    g.math_row = 0;
    g.math_first_row = 0;
    init_solver_fields();
    g.last_tick_ms = millis();
}

void calc_tick() {
    const std::uint32_t now = millis();
    if (now - g.last_tick_ms >= 500u) {
        g.cursor_on = !g.cursor_on;
        g.last_tick_ms = now;
        if (g.screen == Screen::Home || g.screen == Screen::YEquals || g.screen == Screen::Window ||
            g.screen == Screen::Solver || (g.screen == Screen::Graph && g.trace_active)) {
            g.render_dirty = true;
        }
    }
}

void calc_key_down(Key key) {
    g.render_dirty = true;
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
        case Screen::Table: handle_table_key(key); break;
        case Screen::Settings: handle_settings_key(key); break;
        case Screen::MathMenu: handle_math_menu_key(key); break;
        case Screen::Solver: handle_solver_key(key); break;
        case Screen::About:
            if (key == Key::Enter || key == Key::Clear) {
                g.screen = Screen::Home;
            }
            break;
    }
}

void calc_key_up(Key) {}

bool calc_needs_render() {
    return g.render_dirty;
}

int calc_debug_last_graph_evaluations() {
    return g_last_graph_evaluations;
}

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
        case Screen::Table: render_table(display); break;
        case Screen::Settings: render_settings(display); break;
        case Screen::MathMenu: render_math_menu(display); break;
        case Screen::Solver: render_solver(display); break;
        case Screen::About: render_about(display); break;
    }
    g.render_dirty = false;
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
    info.large_text_count = 0;
    info.small_text_count = 0;
    info.fraction_count = 0;
    info.sqrt_count = 0;
    info.superscript_count = 0;
    for (int i = 0; i < layout.node_count; ++i) {
        const LayoutNode& node = layout.nodes[i];
        switch (node.kind) {
            case LayoutKind::TextRun:
                if (node.small) {
                    ++info.small_text_count;
                } else {
                    ++info.large_text_count;
                }
                break;
            case LayoutKind::Fraction: ++info.fraction_count; break;
            case LayoutKind::Sqrt: ++info.sqrt_count; break;
            case LayoutKind::Superscript: ++info.superscript_count; break;
            default: break;
        }
    }
    info.overflow = layout.overflow;
    return !layout.overflow;
}

void calc_debug_set_home_expression(const char* expression, int cursor) {
    if (expression == nullptr) {
        expression = "";
    }
    copy_string(g.home_expr, sizeof(g.home_expr), expression);
    g.home_len = static_cast<int>(std::strlen(g.home_expr));
    g.home_cursor = cursor;
    normalize_cursor(g.home_expr, g.home_len, g.home_cursor);
    ensure_cursor_visible(g.home_expr, g.home_len, g.home_cursor, kLcdWidth - 26, g.home_expr_scroll_x);
    clear_history_selection();
}

int calc_debug_home_cursor() {
    return g.home_cursor;
}

const char* calc_debug_home_expression() {
    static char visible[kExpressionCapacity]{};
    sanitize_expression(g.home_expr, visible, sizeof(visible));
    return visible;
}

int calc_debug_home_scroll_x() {
    return g.home_expr_scroll_x;
}

int calc_debug_history_selection() {
    return g.history_selection;
}

int calc_debug_history_first_entry() {
    return g.history_first_entry;
}

bool calc_debug_angle_degrees() {
    return g.eval.degree_mode;
}

bool calc_debug_fraction_output() {
    return g.fraction_output;
}

}  // namespace calc
