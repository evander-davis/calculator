#include "calc/calculator.hpp"

#include <cstdio>
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
    char y_expr[kExpressionCapacity];
    int y_len;
    int y_cursor;
    HistoryEntry history[kHistoryCapacity];
    int history_count;
    int window_selection;
    std::uint32_t last_tick_ms;
    bool cursor_on;
    Key last_key;
};

CalculatorState g{};

std::uint32_t millis() {
    if (g.platform != nullptr && g.platform->clock.millis != nullptr) {
        return g.platform->clock.millis(g.platform->clock.context);
    }
    return 0;
}

char key_letter(Key key) {
    if (key >= Key::LetterA && key <= Key::LetterZ) {
        return static_cast<char>('A' + static_cast<int>(key) - static_cast<int>(Key::LetterA));
    }
    return '\0';
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

void reset_home_expression() {
    g.home_expr[0] = '\0';
    g.home_len = 0;
    g.home_cursor = 0;
}

void push_history(const char* expression, const char* result, bool error) {
    for (int i = kHistoryCapacity - 1; i > 0; --i) {
        g.history[i] = g.history[i - 1];
    }
    std::snprintf(g.history[0].expression, sizeof(g.history[0].expression), "%s", expression);
    std::snprintf(g.history[0].result, sizeof(g.history[0].result), "%s", result);
    g.history[0].error = error;
    if (g.history_count < kHistoryCapacity) {
        ++g.history_count;
    }
}

void format_value(CalcReal value, char* out, std::size_t size) {
    std::snprintf(out, size, "%.10g", value);
}

void evaluate_home() {
    if (g.home_len == 0) {
        return;
    }
    EvalResult result = evaluate_expression(g.home_expr, g.eval);
    char formatted[32]{};
    if (result.ok) {
        format_value(result.value, formatted, sizeof(formatted));
        push_history(g.home_expr, formatted, false);
        reset_home_expression();
    } else {
        std::snprintf(formatted, sizeof(formatted), "%s", eval_error_text(result.error));
        push_history(g.home_expr, formatted, true);
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
        case Key::Add: return insert_text(buffer, len, cursor, "+");
        case Key::Subtract: return insert_text(buffer, len, cursor, "-");
        case Key::Multiply: return insert_text(buffer, len, cursor, "*");
        case Key::Divide: return insert_text(buffer, len, cursor, "/");
        case Key::Power: return insert_text(buffer, len, cursor, "^");
        case Key::LParen: return insert_text(buffer, len, cursor, "(");
        case Key::RParen: return insert_text(buffer, len, cursor, ")");
        case Key::Equal: return insert_text(buffer, len, cursor, "=");
        case Key::Sin: return insert_text(buffer, len, cursor, "sin(");
        case Key::Cos: return insert_text(buffer, len, cursor, "cos(");
        case Key::Tan: return insert_text(buffer, len, cursor, "tan(");
        case Key::ASin: return insert_text(buffer, len, cursor, "asin(");
        case Key::ACos: return insert_text(buffer, len, cursor, "acos(");
        case Key::ATan: return insert_text(buffer, len, cursor, "atan(");
        case Key::Sqrt: return insert_text(buffer, len, cursor, "sqrt(");
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
    switch (key) {
        case Key::Left:
            if (cursor > 0) {
                --cursor;
            }
            break;
        case Key::Right:
            if (cursor < len) {
                ++cursor;
            }
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
    const CalcReal cx = (g.window.xmin + g.window.xmax) * 0.5;
    const CalcReal cy = (g.window.ymin + g.window.ymax) * 0.5;
    const CalcReal hx = (g.window.xmax - g.window.xmin) * 0.5 * factor;
    const CalcReal hy = (g.window.ymax - g.window.ymin) * 0.5 * factor;
    g.window.xmin = cx - hx;
    g.window.xmax = cx + hx;
    g.window.ymin = cy - hy;
    g.window.ymax = cy + hy;
}

void handle_global_key(Key key) {
    switch (key) {
        case Key::Home: g.screen = Screen::Home; break;
        case Key::Graph: g.screen = Screen::Graph; break;
        case Key::YEquals: g.screen = Screen::YEquals; break;
        case Key::Window: g.screen = Screen::Window; break;
        case Key::Settings: g.screen = Screen::Settings; break;
        case Key::About: g.screen = Screen::About; break;
        default: break;
    }
}

void handle_home_key(Key key) {
    if (key == Key::Enter) {
        evaluate_home();
        return;
    }
    if (key == Key::Clear && g.home_len == 0) {
        g.history_count = 0;
        return;
    }
    edit_expression_key(key, g.home_expr, g.home_len, g.home_cursor);
}

void handle_y_key(Key key) {
    if (key == Key::Enter || key == Key::Graph) {
        g.screen = Screen::Graph;
        return;
    }
    edit_expression_key(key, g.y_expr, g.y_len, g.y_cursor);
}

void handle_graph_key(Key key) {
    switch (key) {
        case Key::Left: pan_graph(-0.1, 0.0); break;
        case Key::Right: pan_graph(0.1, 0.0); break;
        case Key::Up: pan_graph(0.0, 0.1); break;
        case Key::Down: pan_graph(0.0, -0.1); break;
        case Key::Add: zoom_graph(0.75); break;
        case Key::Subtract: zoom_graph(1.25); break;
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
            *values[g.window_selection] += 1.0;
            break;
        case Key::Subtract:
        case Key::Left:
            *values[g.window_selection] -= 1.0;
            break;
        case Key::Enter:
        case Key::Graph:
            g.screen = Screen::Graph;
            break;
        default:
            break;
    }
    if (g.window.xmax <= g.window.xmin + 0.1) {
        g.window.xmax = g.window.xmin + 0.1;
    }
    if (g.window.ymax <= g.window.ymin + 0.1) {
        g.window.ymax = g.window.ymin + 0.1;
    }
}

void title(Display& display, const char* text) {
    fill_rect(display, 0, 0, kLcdWidth, 12, kBlue);
    draw_text(display, 3, 2, text, kWhite, kBlue);
}

void draw_input_line(Display& display, const char* prompt, const char* expr, int cursor) {
    fill_rect(display, 0, kLcdHeight - 22, kLcdWidth, 22, kWhite);
    draw_text(display, 4, kLcdHeight - 17, prompt, kBlack, kWhite);
    draw_text(display, 22, kLcdHeight - 17, expr, kBlack, kWhite);
    if (g.cursor_on) {
        const int cx = 22 + cursor * 6;
        draw_line(display, cx, kLcdHeight - 19, cx, kLcdHeight - 9, kBlack);
    }
}

void render_home(Display& display) {
    clear(display, kWhite);
    title(display, "HOME");
    int y = 18;
    for (int i = g.history_count - 1; i >= 0; --i) {
        draw_text(display, 4, y, g.history[i].expression, kBlack, kWhite);
        y += 9;
        draw_text(display, 18, y, g.history[i].result, g.history[i].error ? kRed : kBlue, kWhite);
        y += 11;
        if (y > kLcdHeight - 34) {
            break;
        }
    }
    draw_input_line(display, ">", g.home_expr, g.home_cursor);
}

void draw_axes(Display& display) {
    int sx = 0;
    int sy = 0;
    const Color grid = rgb565(210, 210, 210);
    for (int i = -10; i <= 10; ++i) {
        if (graph_to_screen(g.window, static_cast<CalcReal>(i), g.window.ymin, sx, sy)) {
            draw_line(display, sx, 12, sx, kLcdHeight - 1, grid);
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
        draw_line(display, ax0, 12, ax0, kLcdHeight - 1, kBlack);
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
        draw_text(display, 8, 26, "Y1 EMPTY - PRESS Y=", kRed, kWhite);
        return;
    }

    bool have_prev = false;
    int prev_x = 0;
    int prev_y = 0;
    EvalContext graph_context = g.eval;
    for (int px = 0; px < kLcdWidth; ++px) {
        const CalcReal x = screen_to_graph_x(g.window, px);
        EvalResult result = evaluate_expression_with_x(g.y_expr, graph_context, x);
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
    draw_text(display, 4, 224, g.y_expr, kBlue, kWhite);
}

void render_y(Display& display) {
    clear(display, kWhite);
    title(display, "Y=");
    draw_text(display, 4, 28, "Y1=", kBlack, kWhite);
    draw_text(display, 28, 28, g.y_expr, kBlack, kWhite);
    if (g.cursor_on) {
        const int cx = 28 + g.y_cursor * 6;
        draw_line(display, cx, 26, cx, 36, kBlack);
    }
    draw_text(display, 4, 210, "ENTER OR GRAPH TO PLOT", kBlue, kWhite);
}

void render_window(Display& display) {
    clear(display, kWhite);
    title(display, "WINDOW");
    const char* labels[4] = {"XMIN", "XMAX", "YMIN", "YMAX"};
    const CalcReal values[4] = {g.window.xmin, g.window.xmax, g.window.ymin, g.window.ymax};
    for (int i = 0; i < 4; ++i) {
        const int y = 30 + i * 24;
        if (g.window_selection == i) {
            fill_rect(display, 2, y - 3, kLcdWidth - 4, 16, kLightGray);
        }
        char line[48]{};
        char value[24]{};
        format_value(values[i], value, sizeof(value));
        std::snprintf(line, sizeof(line), "%s=%s", labels[i], value);
        draw_text(display, 8, y, line, kBlack, g.window_selection == i ? kLightGray : kWhite);
    }
    draw_text(display, 4, 210, "ARROWS SELECT  +/- EDIT", kBlue, kWhite);
}

void render_settings(Display& display) {
    clear(display, kWhite);
    title(display, "SETTINGS");
    draw_text(display, 8, 30, "ANGLE: RADIANS", kBlack, kWhite);
    draw_text(display, 8, 45, "REAL: DOUBLE", kBlack, kWhite);
    draw_text(display, 8, 60, "DISPLAY: RGB565 320X240", kBlack, kWhite);
    draw_text(display, 8, 84, "FIXED BUFFERS ENABLED", kGreen, kWhite);
}

void render_about(Display& display) {
    clear(display, kWhite);
    title(display, "ABOUT");
    draw_text(display, 8, 30, "RP2350 CALC MVP", kBlack, kWhite);
    draw_text(display, 8, 45, "PORTABLE CORE", kBlack, kWhite);
    draw_text(display, 8, 60, "DESKTOP EMULATOR HOST", kBlack, kWhite);
}

}  // namespace

void calc_init(Platform& platform) {
    g = CalculatorState{};
    g.platform = &platform;
    g.screen = Screen::Home;
    eval_context_init(g.eval);
    g.window = {-10.0, 10.0, -6.55, 6.55};
    std::snprintf(g.y_expr, sizeof(g.y_expr), "sin(X)");
    g.y_len = static_cast<int>(std::strlen(g.y_expr));
    g.y_cursor = g.y_len;
    reset_home_expression();
    g.cursor_on = true;
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

}  // namespace calc
