#include "calc/calculator.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::printf("FAIL: %s\n", name);
        ++g_failures;
    }
}

bool nearly(calc::CalcReal a, calc::CalcReal b, calc::CalcReal eps = sizeof(calc::CalcReal) == sizeof(float) ? 1e-4 : 1e-9) {
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

    context.degree_mode = true;
    result = calc::evaluate_expression("sin(90)", context);
    check(result.ok && nearly(result.value, 1.0), "degree mode sin converts to radians");
    result = calc::evaluate_expression("asin(1)", context);
    check(result.ok && nearly(result.value, 90.0), "degree mode inverse trig converts from radians");
    context.degree_mode = false;

    result = calc::evaluate_expression("1.25e2", context);
    check(result.ok && nearly(result.value, 125.0), "decimal exponent parsing");

    result = calc::evaluate_expression("A=5", context);
    check(result.ok && nearly(result.value, 5.0), "assignment");
    result = calc::evaluate_expression("A*3", context);
    check(result.ok && nearly(result.value, 15.0), "variable reuse");

    result = calc::evaluate_expression("2+2", context);
    check(result.ok && nearly(result.value, 4.0), "ans source expression");
    result = calc::evaluate_expression("Ans*3", context);
    check(result.ok && nearly(result.value, 12.0), "ans variable reuse");

    result = calc::evaluate_expression("42->B", context);
    check(result.ok && nearly(result.value, 42.0), "store arrow assignment");
    result = calc::evaluate_expression("B+1", context);
    check(result.ok && nearly(result.value, 43.0), "store arrow variable reuse");

    result = calc::evaluate_expression("1/0", context);
    check(!result.ok && result.error == calc::EvalError::DivideByZero, "divide by zero");

    result = calc::evaluate_expression("sqrt(-1)", context);
    check(result.ok && nearly(result.value, 0.0) && nearly(result.imag, 1.0), "sqrt negative returns imaginary");

    result = calc::evaluate_expression("i^2", context);
    check(result.ok && nearly(result.value, -1.0) && nearly(result.imag, 0.0), "imaginary unit squares to negative one");

    result = calc::evaluate_expression("(-1)^0.5", context);
    check(result.ok && nearly(result.value, 0.0) && nearly(result.imag, 1.0), "negative fractional power returns imaginary");

    result = calc::evaluate_expression("(1+2i)*(3-i)", context);
    check(result.ok && nearly(result.value, 5.0) && nearly(result.imag, 5.0), "complex multiply");

    result = calc::evaluate_expression("A=2+i", context);
    check(result.ok && nearly(context.variables['A' - 'A'], 2.0) && nearly(context.variable_imag['A' - 'A'], 1.0),
          "complex assignment");
    result = calc::evaluate_expression("A*i", context);
    check(result.ok && nearly(result.value, -1.0) && nearly(result.imag, 2.0), "complex variable reuse");

    result = calc::evaluate_expression("root(3)(8)", context);
    check(result.ok && nearly(result.value, 2.0) && nearly(result.imag, 0.0), "nth root evaluation");

    result = calc::evaluate_expression("abs(3+4i)", context);
    check(result.ok && nearly(result.value, 5.0), "complex abs function");
    result = calc::evaluate_expression("real(2+3i)+imag(2+3i)", context);
    check(result.ok && nearly(result.value, 5.0), "complex real and imag functions");
    result = calc::evaluate_expression("conj(2+3i)", context);
    check(result.ok && nearly(result.value, 2.0) && nearly(result.imag, -3.0), "complex conjugate function");
    result = calc::evaluate_expression("round(1.234,2)", context);
    check(result.ok && nearly(result.value, 1.23), "round with digits");
    result = calc::evaluate_expression("iPart(-1.7)+fPart(1.25)+int(-1.2)", context);
    check(result.ok && nearly(result.value, -2.75), "numeric part functions");
    result = calc::evaluate_expression("min(3,2,5)+max(3,2,5)", context);
    check(result.ok && nearly(result.value, 7.0), "min max multi argument functions");
    result = calc::evaluate_expression("gcd(24,18)+lcm(4,6)+remainder(17,5)", context);
    check(result.ok && nearly(result.value, 20.0), "integer numeric functions");
    result = calc::evaluate_expression("logBASE(8,2)", context);
    check(result.ok && nearly(result.value, 3.0), "log base function");
    result = calc::evaluate_expression("5!+nPr(5,2)+nCr(5,2)", context);
    check(result.ok && nearly(result.value, 150.0), "probability operators and functions");
    result = calc::evaluate_expression("randInt(4,4)", context);
    check(result.ok && nearly(result.value, 4.0), "deterministic randInt degenerate range");
    result = calc::evaluate_expression("nDeriv(X^2,X,3)", context);
    check(result.ok && nearly(result.value, 6.0, 1e-4), "numeric derivative function");
    result = calc::evaluate_expression("fnInt(X,X,0,1)", context);
    check(result.ok && nearly(result.value, 0.5, 1e-4), "numeric integral function");
    result = calc::evaluate_expression("sum(X,X,1,4)", context);
    check(result.ok && nearly(result.value, 10.0), "summation function");
    result = calc::evaluate_expression("fMin((X-2)^2,X,-5,5)", context);
    check(result.ok && nearly(result.value, 2.0, 1e-4), "fMin returns minimizing x");
    result = calc::evaluate_expression("fMax(0-(X-1)^2,X,-5,5)", context);
    check(result.ok && nearly(result.value, 1.0, 1e-4), "fMax returns maximizing x");

    result = calc::evaluate_expression_with_x("X^2", context, 3.0);
    check(result.ok && nearly(result.value, 9.0), "x override");
    const calc::CalcReal ans_before_readonly = context.ans;
    result = calc::evaluate_expression_with_x_readonly("X+10", context, 5.0);
    check(result.ok && nearly(result.value, 15.0), "readonly x evaluation");
    check(nearly(context.ans, ans_before_readonly), "readonly evaluation leaves ans unchanged");

    calc::CompiledExpression compiled{};
    check(calc::compile_expression("sin(X)+X^2", compiled) && compiled.real_fast_path,
          "compile real graph expression");
    result = calc::evaluate_compiled_with_x_readonly(compiled, context, 2.0);
    check(result.ok && nearly(result.value, std::sin(calc::CalcReal(2.0)) + 4.0),
          "compiled real graph expression evaluates");
    check(calc::compile_expression("X+i", compiled) && !compiled.real_fast_path,
          "complex expression uses compiled complex path");
    result = calc::evaluate_compiled_with_x_readonly(compiled, context, 2.0);
    check(result.ok && nearly(result.value, 2.0) && nearly(result.imag, 1.0),
          "compiled complex graph expression evaluates");

    constexpr int evaluator_benchmark_iterations = 20000;
    volatile calc::CalcReal evaluator_sink = 0;
    const auto generic_eval_start = std::chrono::steady_clock::now();
    for (int i = 0; i < evaluator_benchmark_iterations; ++i) {
        result = calc::evaluate_expression_with_x_readonly("sin(X)+X^2", context, calc::CalcReal(i % 100) / 10);
        evaluator_sink += result.value;
    }
    const auto generic_eval_elapsed = std::chrono::steady_clock::now() - generic_eval_start;
    check(calc::compile_expression("sin(X)+X^2", compiled), "recompile evaluator benchmark expression");
    const auto compiled_eval_start = std::chrono::steady_clock::now();
    for (int i = 0; i < evaluator_benchmark_iterations; ++i) {
        result = calc::evaluate_compiled_with_x_readonly(compiled, context, calc::CalcReal(i % 100) / 10);
        evaluator_sink += result.value;
    }
    const auto compiled_eval_elapsed = std::chrono::steady_clock::now() - compiled_eval_start;
    std::printf("Evaluator benchmark (%d): generic=%lld us compiled=%lld us\n",
                evaluator_benchmark_iterations,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(generic_eval_elapsed).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(compiled_eval_elapsed).count()));
    check(evaluator_sink != calc::CalcReal(0), "evaluator benchmark result consumed");

    char deeply_nested[96]{};
    constexpr int nested_pairs = 40;
    for (int i = 0; i < nested_pairs; ++i) {
        deeply_nested[i] = '(';
        deeply_nested[nested_pairs + 1 + i] = ')';
    }
    deeply_nested[nested_pairs] = '1';
    result = calc::evaluate_expression(deeply_nested, context);
    check(result.ok && nearly(result.value, 1.0), "deep parentheses stay within fixed evaluator bounds");

    calc::GraphWindow window{-10.0, 10.0, -5.0, 5.0};
    int sx = -1;
    int sy = -1;
    check(calc::graph_to_screen(window, 0.0, 0.0, sx, sy), "graph origin visible");
    check(sx == 160 || sx == 159, "graph origin x");
    check(sy == 120 || sy == 119, "graph origin y");
    check(nearly(calc::screen_to_graph_x(window, 0), -10.0), "screen x min");
    check(nearly(calc::screen_to_graph_y(window, 0), 5.0), "screen y max");

    calc::LayoutDebugInfo layout{};
    check(calc::calc_debug_layout_expression("sqrt(3)", layout) && layout.width > 0 && layout.height >= 14,
          "layout sqrt measurement");
    const int sqrt_height = layout.height;
    check(calc::calc_debug_layout_expression("1^2", layout) && layout.anchor_count >= 3 && layout.ascent > 11,
          "layout exponent anchors");
    check(calc::calc_debug_layout_expression("()/()", layout) && layout.anchor_count >= 4 && layout.height > 20,
          "layout empty fraction anchors");
    check(calc::calc_debug_layout_expression("(1)/(sqrt(2^3))", layout) && layout.height > sqrt_height,
          "layout nested height grows");

    calc::Color pixels[calc::kLcdWidth * calc::kLcdHeight]{};
    TestClock clock{0};
    calc::Platform platform{};
    platform.display = {pixels, calc::kLcdWidth, calc::kLcdHeight};
    platform.clock = {&clock, clock_millis};
    calc::calc_init(platform);
    check(calc::calc_screen() == calc::Screen::Home, "initial screen");
    check(calc::calc_needs_render(), "initial state requests render");
    calc::calc_render();
    check(!calc::calc_needs_render(), "render clears dirty state");

    press(calc::Key::Math);
    check(calc::calc_screen() == calc::Screen::MathMenu, "math key opens math menu");
    calc::calc_render();
    int math_menu_nonwhite = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0xffff) {
            ++math_menu_nonwhite;
        }
    }
    check(math_menu_nonwhite > 1000, "math menu render non-empty");
    press(calc::Key::Digit5);
    check(calc::calc_screen() == calc::Screen::Home &&
              std::strcmp(calc::calc_debug_home_expression(), "root()()") == 0 &&
              calc::calc_debug_home_cursor() == 5,
          "math menu direct number inserts nth root");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Math);
    press(calc::Key::Right);
    press(calc::Key::Digit1);
    check(std::strcmp(calc::calc_debug_home_expression(), "abs()") == 0 && calc::calc_debug_home_cursor() == 4,
          "math num tab direct selection inserts function");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Math);
    press(calc::Key::Right);
    press(calc::Key::LetterD);
    check(std::strcmp(calc::calc_debug_home_expression(), "()/()") == 0 && calc::calc_debug_home_cursor() == 1,
          "math num tab letter selection inserts fraction template");

    press(calc::Key::Math);
    press(calc::Key::LetterC);
    check(calc::calc_screen() == calc::Screen::Solver, "math numeric solver opens solver screen");
    calc::calc_render();
    int solver_nonwhite = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0xffff) {
            ++solver_nonwhite;
        }
    }
    check(solver_nonwhite > 1000, "solver render non-empty");
    press(calc::Key::Clear);
    check(calc::calc_screen() == calc::Screen::Home, "clear exits solver");

    press(calc::Key::Second);
    press(calc::Key::Fraction);
    check(calc::calc_screen() == calc::Screen::Settings, "second fraction opens settings");
    press(calc::Key::Right);
    check(calc::calc_debug_angle_degrees(), "settings right selects degrees");
    press(calc::Key::Left);
    check(!calc::calc_debug_angle_degrees(), "settings left selects radians");
    press(calc::Key::Right);
    press(calc::Key::Down);
    press(calc::Key::Right);
    check(calc::calc_debug_fraction_output(), "settings right selects fraction output");
    press(calc::Key::Left);
    check(!calc::calc_debug_fraction_output(), "settings left selects decimal output");
    press(calc::Key::Right);
    press(calc::Key::Clear);
    check(calc::calc_screen() == calc::Screen::Home, "clear exits settings");

    calc::calc_debug_set_home_expression("sin(90)", 7);
    press(calc::Key::Enter);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "degree mode expression evaluates from home");

    calc::calc_debug_set_home_expression("1.5+0.5i", 8);
    press(calc::Key::Enter);
    press(calc::Key::Up);
    press(calc::Key::Enter);
    check(std::strcmp(calc::calc_debug_home_expression(), "(3)/(2)+(1)/(2)i") == 0,
          "fraction output formats real and imaginary coefficients separately");

    calc::calc_init(platform);
    press(calc::Key::Alpha);
    press(calc::Key::Square);
    check(std::strcmp(calc::calc_debug_home_expression(), "I") == 0,
          "alpha x squared inserts I");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Alpha);
    press(calc::Key::Dot);
    check(std::strcmp(calc::calc_debug_home_expression(), "i") == 0,
          "alpha dot inserts imaginary i");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Second);
    press(calc::Key::Log);
    check(std::strcmp(calc::calc_debug_home_expression(), "10^()") == 0 && calc::calc_debug_home_cursor() == 4,
          "second log inserts ten-to-the-x template");
    press(calc::Key::Digit2);
    result = calc::evaluate_expression(calc::calc_debug_home_expression(), context);
    check(result.ok && nearly(result.value, 100.0), "ten-to-the-x template evaluates");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Second);
    press(calc::Key::Ln);
    check(std::strcmp(calc::calc_debug_home_expression(), "e^()") == 0 && calc::calc_debug_home_cursor() == 3,
          "second ln inserts e-to-the-x template");
    press(calc::Key::Digit1);
    result = calc::evaluate_expression(calc::calc_debug_home_expression(), context);
    check(result.ok && nearly(result.value, std::exp(calc::CalcReal(1.0))), "e-to-the-x template evaluates");

    calc::calc_debug_set_home_expression("12", 2);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "1") == 0 && calc::calc_debug_home_cursor() == 1,
          "delete key behaves as backspace");

    calc::calc_debug_set_home_expression("()/(2)", 1);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "delete key preserves blank fraction shortcut");

    calc::calc_debug_set_home_expression("asin(2)", 5);
    press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 0,
          "left treats inverse trig prefix as one cursor step");
    press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 5,
          "right treats inverse trig prefix as one cursor step");

    calc::calc_debug_set_home_expression("sin(2)", 4);
    press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 0,
          "left treats trig prefix as one cursor step");
    press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 4,
          "right treats trig prefix as one cursor step");

    calc::calc_debug_set_home_expression("Ans+1", 3);
    press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 0,
          "left treats Ans as one cursor step");
    press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 3,
          "right treats Ans as one cursor step");
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "+1") == 0 && calc::calc_debug_home_cursor() == 0,
          "delete removes Ans as one token");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Sin);
    check(std::strcmp(calc::calc_debug_home_expression(), "sin()") == 0 && calc::calc_debug_home_cursor() == 4,
          "function key inserts grey closing parenthesis placeholder");
    press(calc::Key::Digit2);
    press(calc::Key::RParen);
    check(std::strcmp(calc::calc_debug_home_expression(), "sin(2)") == 0 && calc::calc_debug_home_cursor() == 6,
          "right parenthesis directly before placeholder replaces it");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::LParen);
    press(calc::Key::Digit2);
    press(calc::Key::Right);
    press(calc::Key::Add);
    press(calc::Key::Digit3);
    press(calc::Key::RParen);
    check(std::strcmp(calc::calc_debug_home_expression(), "(2+3)") == 0,
          "right parenthesis away from placeholder removes furthest left placeholder");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::LParen);
    press(calc::Key::Digit2);
    press(calc::Key::Right);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "(2)") == 0 && calc::calc_debug_home_cursor() == 2,
          "delete to right of grey parenthesis moves left without deleting");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::LParen);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "deleting explicit opening parenthesis removes matching grey close");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Digit2);
    press(calc::Key::RParen);
    check(std::strcmp(calc::calc_debug_home_expression(), "(2)") == 0,
          "unmatched right parenthesis creates grey opening parenthesis");
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "2") == 0,
          "deleting explicit closing parenthesis removes matching grey open");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::LParen);
    press(calc::Key::Digit2);
    press(calc::Key::RParen);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "(2") == 0,
          "deleting explicit closing parenthesis leaves explicit opening parenthesis");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Sin);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "deleting function opening parenthesis removes matching grey close");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Digit2);
    press(calc::Key::RParen);
    press(calc::Key::Enter);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "parser evaluates grey opening parenthesis as normal parenthesis");

    calc::calc_debug_set_home_expression("2^3", 3);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "2") == 0 && calc::calc_debug_home_cursor() == 1,
          "delete to right of exponent removes whole exponent");

    calc::calc_debug_set_home_expression("2^()", 3);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "2") == 0 && calc::calc_debug_home_cursor() == 1,
          "delete inside blank exponent removes whole exponent");

    calc::calc_debug_set_home_expression("(1)/(2)", 7);
    press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0 && calc::calc_debug_home_cursor() == 0,
          "delete to right of fraction removes whole fraction");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Digit2);
    press(calc::Key::Square);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^(2)") == 0 && calc::calc_debug_home_cursor() == 5,
          "square key leaves cursor after exponent");
    press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 4,
          "left after square key lands on exponent right edge");
    press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 5,
          "right exits square key exponent");

    calc::calc_debug_set_home_expression("", 0);
    press(calc::Key::Second);
    press(calc::Key::Power);
    check(std::strcmp(calc::calc_debug_home_expression(), "root()()") == 0 && calc::calc_debug_home_cursor() == 5,
          "second power inserts nth root with cursor in index");
    press(calc::Key::Digit3);
    press(calc::Key::Down);
    press(calc::Key::Digit8);
    check(std::strcmp(calc::calc_debug_home_expression(), "root(3)(8)") == 0 && calc::calc_debug_home_cursor() == 9,
          "nth root supports index and radicand cursor slots");

    calc::calc_debug_set_home_expression("0.5", 3);
    press(calc::Key::FracDecimal);
    check(std::strcmp(calc::calc_debug_home_expression(), "(1)/(2)") == 0,
          "frac decimal key converts decimal to fraction approximation");
    press(calc::Key::FracDecimal);
    check(std::strcmp(calc::calc_debug_home_expression(), "0.5") == 0,
          "frac decimal key converts fraction to decimal");

    calc::calc_debug_set_home_expression("sin(pi/4)", 9);
    press(calc::Key::FracDecimal);
    check(std::strcmp(calc::calc_debug_home_expression(), "(sqrt(2))/(2)") == 0,
          "frac decimal key converts common trig radical");

    calc::calc_init(platform);
    for (int i = 1; i <= 4; ++i) {
        char expr[8]{};
        std::snprintf(expr, sizeof(expr), "%d+1", i);
        calc::calc_debug_set_home_expression(expr, static_cast<int>(std::strlen(expr)));
        press(calc::Key::Enter);
    }
    press(calc::Key::Up);
    check(calc::calc_debug_history_selection() == 0 && calc::calc_debug_history_first_entry() == 0,
          "first history up selects latest answer without scrolling");
    press(calc::Key::Up);
    check(calc::calc_debug_history_selection() == 1 && calc::calc_debug_history_first_entry() == 0,
          "second history up moves highlight within visible latest entry");
    press(calc::Key::Up);
    check(calc::calc_debug_history_selection() == 2 && calc::calc_debug_history_first_entry() == 0,
          "third history up moves highlight to visible older answer before scrolling");

    calc::calc_debug_set_home_expression("", 0);
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

    calc::calc_init(platform);
    press(calc::Key::Fraction);
    press(calc::Key::Sqrt);
    press(calc::Key::Digit3);
    press(calc::Key::Down);
    press(calc::Key::Digit2);
    press(calc::Key::Power);
    press(calc::Key::Digit3);
    calc::calc_render();
    std::uint32_t nested_nonwhite = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0xffff) {
            ++nested_nonwhite;
        }
    }
    check(nested_nonwhite > 100, "nested layout render non-empty");

    press(calc::Key::YEquals);
    check(calc::calc_screen() == calc::Screen::YEquals, "y= navigation");
    press(calc::Key::Graph);
    check(calc::calc_screen() == calc::Screen::Graph, "graph navigation");
    press(calc::Key::Second);
    press(calc::Key::Graph);
    check(calc::calc_screen() == calc::Screen::Table, "second graph opens table");
    calc::calc_render();
    int table_nonwhite = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != 0xffff) {
            ++table_nonwhite;
        }
    }
    check(table_nonwhite > 1000, "table render non-empty");
    press(calc::Key::Window);
    check(calc::calc_screen() == calc::Screen::Window, "window navigation from table");
    press(calc::Key::Graph);
    check(calc::calc_screen() == calc::Screen::Graph, "graph navigation from window");

    press(calc::Key::YEquals);
    press(calc::Key::Sin);
    press(calc::Key::X);
    press(calc::Key::Graph);
    const auto graph_start = std::chrono::steady_clock::now();
    calc::calc_render();
    const auto graph_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - graph_start);
    std::printf("Graph render benchmark: %lld ms\n", static_cast<long long>(graph_elapsed.count()));
    check(graph_elapsed.count() < 1000, "graph render completes within host performance guardrail");
    const int initial_graph_evaluations = calc::calc_debug_last_graph_evaluations();
    std::printf("Adaptive graph evaluations: %d/320\n", initial_graph_evaluations);
    check(initial_graph_evaluations > 0 && initial_graph_evaluations < 240,
          "adaptive smooth graph uses fewer than one evaluation per pixel");
    calc::calc_render();
    check(calc::calc_debug_last_graph_evaluations() == 0,
          "unchanged graph reuses cached curve samples");

    press(calc::Key::YEquals);
    press(calc::Key::Clear);
    press(calc::Key::Digit1);
    press(calc::Key::Divide);
    press(calc::Key::X);
    press(calc::Key::Graph);
    calc::calc_render();
    const int reciprocal_evaluations = calc::calc_debug_last_graph_evaluations();
    std::printf("Reciprocal adaptive evaluations: %d/320\n", reciprocal_evaluations);
    check(reciprocal_evaluations > 0 && reciprocal_evaluations <= 320,
          "adaptive reciprocal graph stays within one evaluation per pixel");
    int max_red_column = 0;
    int max_red_column_x = 0;
    for (int x = 0; x < calc::kLcdWidth; ++x) {
        int red_pixels = 0;
        for (int y = 18; y < calc::kLcdHeight; ++y) {
            if (pixels[y * calc::kLcdWidth + x] == 0xf800) ++red_pixels;
        }
        if (red_pixels > max_red_column) {
            max_red_column = red_pixels;
            max_red_column_x = x;
        }
    }
    std::printf("Reciprocal max red column: %d at x=%d\n", max_red_column, max_red_column_x);
    check(max_red_column < 100, "adaptive reciprocal graph does not bridge its vertical asymptote");

    press(calc::Key::YEquals);
    press(calc::Key::Clear);
    press(calc::Key::Tan);
    press(calc::Key::X);
    press(calc::Key::Graph);
    calc::calc_render();
    const int tangent_evaluations = calc::calc_debug_last_graph_evaluations();
    std::printf("Tangent adaptive evaluations: %d/320\n", tangent_evaluations);
    check(tangent_evaluations > 0 && tangent_evaluations <= 320,
          "adaptive tangent graph bounds high-curvature sampling");
    check(!calc::calc_needs_render(), "graph render clears dirty state");
    clock.now += 500;
    calc::calc_tick();
    check(!calc::calc_needs_render(), "static graph skips cursor-only redraw");
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
