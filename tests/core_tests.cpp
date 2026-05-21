#include "calc/calculator.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::printf("FAIL: %s\n", name);
        ++g_failures;
    }
}

bool nearly(calc::CalcReal a, calc::CalcReal b, calc::CalcReal eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

struct TestClock {
    std::uint32_t now;
};

std::uint32_t clock_millis(void* context) {
    return static_cast<TestClock*>(context)->now;
}

void press(calc::Key key) {
    calc::calc_key_down(key);
    calc::calc_key_up(key);
}

}  // namespace

int main() {
    calc::EvalContext context{};
    calc::eval_context_init(context);

    calc::EvalResult result = calc::evaluate_expression("2+3*4", context);
    check(result.ok && nearly(result.value, 14.0), "operator precedence");

    result = calc::evaluate_expression("(2+3)*4", context);
    check(result.ok && nearly(result.value, 20.0), "parentheses");

    result = calc::evaluate_expression("-2^2", context);
    check(result.ok && nearly(result.value, 4.0), "unary minus precedence");

    result = calc::evaluate_expression("sin(pi/2)", context);
    check(result.ok && nearly(result.value, 1.0), "sin pi over 2");

    result = calc::evaluate_expression("A=5", context);
    check(result.ok && nearly(result.value, 5.0), "assignment");
    result = calc::evaluate_expression("A*3", context);
    check(result.ok && nearly(result.value, 15.0), "variable reuse");

    result = calc::evaluate_expression("1/0", context);
    check(!result.ok && result.error == calc::EvalError::DivideByZero, "divide by zero");

    result = calc::evaluate_expression("sqrt(-1)", context);
    check(!result.ok && result.error == calc::EvalError::Domain, "domain error");

    result = calc::evaluate_expression_with_x("X^2", context, 3.0);
    check(result.ok && nearly(result.value, 9.0), "x override");

    calc::GraphWindow window{-10.0, 10.0, -5.0, 5.0};
    int sx = -1;
    int sy = -1;
    check(calc::graph_to_screen(window, 0.0, 0.0, sx, sy), "graph origin visible");
    check(sx == 160 || sx == 159, "graph origin x");
    check(sy == 120 || sy == 119, "graph origin y");
    check(nearly(calc::screen_to_graph_x(window, 0), -10.0), "screen x min");
    check(nearly(calc::screen_to_graph_y(window, 0), 5.0), "screen y max");

    calc::Color pixels[calc::kLcdWidth * calc::kLcdHeight]{};
    TestClock clock{0};
    calc::Platform platform{};
    platform.display = {pixels, calc::kLcdWidth, calc::kLcdHeight};
    platform.clock = {&clock, clock_millis};
    calc::calc_init(platform);
    check(calc::calc_screen() == calc::Screen::Home, "initial screen");

    press(calc::Key::Digit2);
    press(calc::Key::Add);
    press(calc::Key::Digit2);
    press(calc::Key::Enter);
    calc::calc_render();
    std::uint32_t nonzero = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0) {
            ++nonzero;
        }
    }
    check(nonzero > 1000, "home render non-empty");

    press(calc::Key::YEquals);
    check(calc::calc_screen() == calc::Screen::YEquals, "y= navigation");
    press(calc::Key::Graph);
    check(calc::calc_screen() == calc::Screen::Graph, "graph navigation");

    calc::calc_render();
    std::uint32_t graph_nonwhite = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0xffff) {
            ++graph_nonwhite;
        }
    }
    check(graph_nonwhite > 1000, "graph render non-empty");

    if (g_failures != 0) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All core tests passed\n");
    return 0;
}
