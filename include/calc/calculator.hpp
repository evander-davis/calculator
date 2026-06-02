#pragma once

#include <cstddef>
#include <cstdint>

namespace calc {

#if defined(CALC_USE_FLOAT) && CALC_USE_FLOAT
using CalcReal = float;
#else
using CalcReal = double;
#endif
using Color = std::uint16_t;

constexpr int kLcdWidth = 320;
constexpr int kLcdHeight = 240;
constexpr int kExpressionCapacity = 96;
constexpr int kHistoryCapacity = 8;

enum class Key : std::uint8_t {
    None,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Dot,
    Add,
    Subtract,
    Multiply,
    Divide,
    Power,
    LParen,
    RParen,
    Equal,
    Enter,
    Clear,
    Delete,
    Back,
    Left,
    Right,
    Up,
    Down,
    Home,
    Graph,
    YEquals,
    Window,
    Settings,
    About,
    Vars,
    Sin,
    Cos,
    Tan,
    ASin,
    ACos,
    ATan,
    Sqrt,
    Log,
    Ln,
    Ans,
    Pi,
    ConstE,
    X,
    LetterA,
    LetterB,
    LetterC,
    LetterD,
    LetterE,
    LetterF,
    LetterG,
    LetterH,
    LetterI,
    LetterJ,
    LetterK,
    LetterL,
    LetterM,
    LetterN,
    LetterO,
    LetterP,
    LetterQ,
    LetterR,
    LetterS,
    LetterT,
    LetterU,
    LetterV,
    LetterW,
    LetterX,
    LetterY,
    LetterZ,
    Second,
    Alpha,
    Mode,
    Zoom,
    Trace,
    Stat,
    Math,
    Apps,
    Program,
    NthRoot,
    FracDecimal,
    Square,
    Comma,
    Store,
    On,
    Negate,
    Fraction,
    Imaginary
};

enum class Screen : std::uint8_t {
    Home,
    Graph,
    YEquals,
    Window,
    Settings,
    About
};

struct Display {
    Color* pixels;
    int width;
    int height;
};

using StorageReadFn = bool (*)(void* context, const char* key, void* out, std::size_t size);
using StorageWriteFn = bool (*)(void* context, const char* key, const void* data, std::size_t size);
using ClockMillisFn = std::uint32_t (*)(void* context);
using LogFn = void (*)(void* context, const char* message);

struct Storage {
    void* context;
    StorageReadFn read;
    StorageWriteFn write;
};

struct Clock {
    void* context;
    ClockMillisFn millis;
};

struct Logger {
    void* context;
    LogFn log;
};

struct Platform {
    Display display;
    Storage storage;
    Clock clock;
    Logger logger;
};

struct GraphWindow {
    CalcReal xmin;
    CalcReal xmax;
    CalcReal ymin;
    CalcReal ymax;
};

enum class EvalError : std::uint8_t {
    None,
    EmptyExpression,
    InvalidToken,
    UnexpectedToken,
    TooManyTokens,
    StackOverflow,
    MismatchedParentheses,
    UnknownIdentifier,
    DivideByZero,
    Domain,
    Overflow,
    Assignment
};

struct EvalContext {
    CalcReal variables[26];
    CalcReal variable_imag[26];
    bool variable_valid[26];
    CalcReal ans;
    CalcReal ans_imag;
    bool degree_mode;
};

struct EvalResult {
    bool ok;
    CalcReal value;
    CalcReal imag;
    EvalError error;
    int error_pos;
};

struct LayoutDebugInfo {
    int width;
    int ascent;
    int descent;
    int height;
    int anchor_count;
    int large_text_count;
    int small_text_count;
    int fraction_count;
    int sqrt_count;
    int superscript_count;
    bool overflow;
};

Color rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b);
void clear(Display& display, Color color);
void set_pixel(Display& display, int x, int y, Color color);
void draw_line(Display& display, int x0, int y0, int x1, int y1, Color color);
void draw_rect(Display& display, int x, int y, int w, int h, Color color);
void fill_rect(Display& display, int x, int y, int w, int h, Color color);
void draw_text(Display& display, int x, int y, const char* text, Color fg, Color bg);
void draw_text_scaled(Display& display, int x, int y, const char* text, int scale, Color fg, Color bg);

void eval_context_init(EvalContext& context);
EvalResult evaluate_expression(const char* expression, EvalContext& context);
EvalResult evaluate_expression_with_x(const char* expression, EvalContext& context, CalcReal x_value);
EvalResult evaluate_expression_with_x_readonly(const char* expression, const EvalContext& context, CalcReal x_value);
const char* eval_error_text(EvalError error);

bool graph_to_screen(const GraphWindow& window, CalcReal x, CalcReal y, int& sx, int& sy);
CalcReal screen_to_graph_x(const GraphWindow& window, int sx);
CalcReal screen_to_graph_y(const GraphWindow& window, int sy);

void calc_init(Platform& platform);
void calc_tick();
void calc_key_down(Key key);
void calc_key_up(Key key);
void calc_render();
Screen calc_screen();
bool calc_debug_layout_expression(const char* expression, LayoutDebugInfo& info);
void calc_debug_set_home_expression(const char* expression, int cursor);
const char* calc_debug_home_expression();
int calc_debug_home_cursor();
int calc_debug_home_scroll_x();
int calc_debug_history_selection();
int calc_debug_history_first_entry();
bool calc_debug_angle_degrees();
bool calc_debug_fraction_output();

}  // namespace calc
