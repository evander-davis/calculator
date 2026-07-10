#include "calc/calculator.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace calc {
namespace {

constexpr int kMaxTokens = 96;
constexpr int kMaxStack = 32;
constexpr char kAutoOpenParen = '\x1c';
constexpr char kAutoCloseParen = '\x1d';

constexpr CalcReal real(double value) {
    return static_cast<CalcReal>(value);
}

constexpr CalcReal kPi = real(3.1415926535897932384626433832795);
constexpr CalcReal kE = real(2.7182818284590452353602874713527);

enum class TokenKind : std::uint8_t {
    Number,
    Variable,
    Operator,
    Function,
    LParen
};

enum class Function : std::uint8_t {
    Sin,
    Cos,
    Tan,
    ASin,
    ACos,
    ATan,
    Sqrt,
    Log,
    Ln,
    Abs,
    Round,
    IPart,
    FPart,
    Int,
    Min,
    Max,
    Lcm,
    Gcd,
    Remainder,
    LogBase,
    Conj,
    Real,
    Imag,
    Angle,
    Rand,
    RandInt,
    RandNorm,
    RandBin,
    RandIntNoRep,
    NPr,
    NCr,
    Factorial
};

struct Token {
    TokenKind kind;
    CalcReal number;
    CalcReal imag;
    char op;
    char variable;
    Function function;
    int arity;
    int pos;
};

struct ParseState {
    Token output[kMaxTokens];
    int output_count;
    Token operators[kMaxTokens];
    int operator_count;
    EvalError error;
    int error_pos;
};

// Evaluation is single-threaded. Keep the large fixed workspaces out of the
// RP2350 core stack and reuse compiled RPN for graph samples.
ParseState g_cached_parse{};
char g_cached_source[kExpressionCapacity]{};
bool g_cached_parse_valid = false;

bool same_word(const char* text, int len, const char* word) {
    const int word_len = static_cast<int>(std::strlen(word));
    if (len != word_len) {
        return false;
    }
    for (int i = 0; i < len; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != word[i]) {
            return false;
        }
    }
    return true;
}

bool is_open_paren(char ch) {
    return ch == '(' || ch == kAutoOpenParen;
}

bool is_close_paren(char ch) {
    return ch == ')' || ch == kAutoCloseParen;
}

bool is_right_assoc(char op) {
    return op == '^' || op == '~';
}

int precedence(char op) {
    switch (op) {
        case '~': return 4;
        case '^': return 3;
        case '*':
        case '/': return 2;
        case '+':
        case '-': return 1;
        default: return 0;
    }
}

void set_error(ParseState& state, EvalError error, int pos) {
    if (state.error == EvalError::None) {
        state.error = error;
        state.error_pos = pos;
    }
}

bool push_output(ParseState& state, const Token& token) {
    if (state.output_count >= kMaxTokens) {
        set_error(state, EvalError::TooManyTokens, token.pos);
        return false;
    }
    state.output[state.output_count++] = token;
    return true;
}

bool push_operator(ParseState& state, const Token& token) {
    if (state.operator_count >= kMaxTokens) {
        set_error(state, EvalError::TooManyTokens, token.pos);
        return false;
    }
    state.operators[state.operator_count++] = token;
    return true;
}

bool pop_operator_to_output(ParseState& state) {
    if (state.operator_count <= 0) {
        set_error(state, EvalError::UnexpectedToken, 0);
        return false;
    }
    return push_output(state, state.operators[--state.operator_count]);
}

bool push_binary_operator(ParseState& state, char op, int pos) {
    Token token{};
    token.kind = TokenKind::Operator;
    token.op = op;
    token.pos = pos;

    while (state.operator_count > 0) {
        const Token top = state.operators[state.operator_count - 1];
        if (top.kind != TokenKind::Operator && top.kind != TokenKind::Function) {
            break;
        }
        if (top.kind == TokenKind::Function ||
            precedence(top.op) > precedence(op) ||
            (precedence(top.op) == precedence(op) && !is_right_assoc(op))) {
            if (!pop_operator_to_output(state)) {
                return false;
            }
        } else {
            break;
        }
    }

    return push_operator(state, token);
}

bool parse_function(const char* name, int len, Function& function) {
    if (same_word(name, len, "sin")) {
        function = Function::Sin;
        return true;
    }
    if (same_word(name, len, "cos")) {
        function = Function::Cos;
        return true;
    }
    if (same_word(name, len, "tan")) {
        function = Function::Tan;
        return true;
    }
    if (same_word(name, len, "asin")) {
        function = Function::ASin;
        return true;
    }
    if (same_word(name, len, "acos")) {
        function = Function::ACos;
        return true;
    }
    if (same_word(name, len, "atan")) {
        function = Function::ATan;
        return true;
    }
    if (same_word(name, len, "sqrt")) {
        function = Function::Sqrt;
        return true;
    }
    if (same_word(name, len, "log")) {
        function = Function::Log;
        return true;
    }
    if (same_word(name, len, "ln")) {
        function = Function::Ln;
        return true;
    }
    if (same_word(name, len, "abs")) {
        function = Function::Abs;
        return true;
    }
    if (same_word(name, len, "round")) {
        function = Function::Round;
        return true;
    }
    if (same_word(name, len, "ipart")) {
        function = Function::IPart;
        return true;
    }
    if (same_word(name, len, "fpart")) {
        function = Function::FPart;
        return true;
    }
    if (same_word(name, len, "int")) {
        function = Function::Int;
        return true;
    }
    if (same_word(name, len, "min")) {
        function = Function::Min;
        return true;
    }
    if (same_word(name, len, "max")) {
        function = Function::Max;
        return true;
    }
    if (same_word(name, len, "lcm")) {
        function = Function::Lcm;
        return true;
    }
    if (same_word(name, len, "gcd")) {
        function = Function::Gcd;
        return true;
    }
    if (same_word(name, len, "remainder")) {
        function = Function::Remainder;
        return true;
    }
    if (same_word(name, len, "logbase")) {
        function = Function::LogBase;
        return true;
    }
    if (same_word(name, len, "conj")) {
        function = Function::Conj;
        return true;
    }
    if (same_word(name, len, "real")) {
        function = Function::Real;
        return true;
    }
    if (same_word(name, len, "imag")) {
        function = Function::Imag;
        return true;
    }
    if (same_word(name, len, "angle")) {
        function = Function::Angle;
        return true;
    }
    if (same_word(name, len, "rand")) {
        function = Function::Rand;
        return true;
    }
    if (same_word(name, len, "randint")) {
        function = Function::RandInt;
        return true;
    }
    if (same_word(name, len, "randnorm")) {
        function = Function::RandNorm;
        return true;
    }
    if (same_word(name, len, "randbin")) {
        function = Function::RandBin;
        return true;
    }
    if (same_word(name, len, "randintnorep")) {
        function = Function::RandIntNoRep;
        return true;
    }
    if (same_word(name, len, "npr")) {
        function = Function::NPr;
        return true;
    }
    if (same_word(name, len, "ncr")) {
        function = Function::NCr;
        return true;
    }
    return false;
}

bool parse_number(const char* text, int& pos, CalcReal& value) {
    const int start = pos;
    CalcReal whole = real(0.0);
    bool saw_digit = false;

    while (std::isdigit(static_cast<unsigned char>(text[pos]))) {
        saw_digit = true;
        whole = whole * real(10.0) + static_cast<CalcReal>(text[pos] - '0');
        ++pos;
    }

    CalcReal fraction = real(0.0);
    CalcReal scale = real(1.0);
    if (text[pos] == '.') {
        ++pos;
        while (std::isdigit(static_cast<unsigned char>(text[pos]))) {
            saw_digit = true;
            fraction = fraction * real(10.0) + static_cast<CalcReal>(text[pos] - '0');
            scale *= real(10.0);
            ++pos;
        }
    }

    if (!saw_digit) {
        pos = start;
        return false;
    }

    int exponent = 0;
    int exponent_sign = 1;
    if (text[pos] == 'e' || text[pos] == 'E') {
        const int exponent_start = pos;
        ++pos;
        if (text[pos] == '+' || text[pos] == '-') {
            exponent_sign = text[pos] == '-' ? -1 : 1;
            ++pos;
        }
        bool saw_exponent_digit = false;
        while (std::isdigit(static_cast<unsigned char>(text[pos]))) {
            saw_exponent_digit = true;
            if (exponent < 1000) {
                exponent = exponent * 10 + (text[pos] - '0');
            }
            ++pos;
        }
        if (!saw_exponent_digit) {
            pos = exponent_start;
        }
    }

    value = whole + fraction / scale;
    const int signed_exponent = exponent * exponent_sign;
    if (signed_exponent != 0) {
        int remaining = signed_exponent < 0 ? -signed_exponent : signed_exponent;
        CalcReal scale10 = real(1.0);
        while (remaining > 0) {
            scale10 *= real(10.0);
            --remaining;
        }
        value = signed_exponent < 0 ? value / scale10 : value * scale10;
    }

    return std::isfinite(value);
}

bool append_char(char* out, int& pos, int cap, char ch) {
    if (pos + 1 >= cap) {
        return false;
    }
    out[pos++] = ch;
    out[pos] = '\0';
    return true;
}

bool append_text(char* out, int& pos, int cap, const char* text) {
    for (int i = 0; text[i] != '\0'; ++i) {
        if (!append_char(out, pos, cap, text[i])) {
            return false;
        }
    }
    return true;
}

int matching_paren_local(const char* expr, int open, int end) {
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

bool expand_roots_range(const char* expr, int start, int end, char* out, int& pos, int cap) {
    for (int i = start; i < end;) {
        if (i + 6 <= end && std::strncmp(expr + i, "root(", 5) == 0) {
            const int index_open = i + 4;
            const int index_close = matching_paren_local(expr, index_open, end);
            if (index_close > 0 && index_close + 1 < end && is_open_paren(expr[index_close + 1])) {
                const int rad_open = index_close + 1;
                const int rad_close = matching_paren_local(expr, rad_open, end);
                if (rad_close > 0) {
                    if (!append_text(out, pos, cap, "((") ||
                        !expand_roots_range(expr, rad_open + 1, rad_close, out, pos, cap) ||
                        !append_text(out, pos, cap, ")^(1/(") ||
                        !expand_roots_range(expr, index_open + 1, index_close, out, pos, cap) ||
                        !append_text(out, pos, cap, ")))")) {
                        return false;
                    }
                    i = rad_close + 1;
                    continue;
                }
            }
        }
        if (!append_char(out, pos, cap, expr[i])) {
            return false;
        }
        ++i;
    }
    return true;
}

bool expand_roots(const char* expression, char* out, int cap) {
    out[0] = '\0';
    const int len = static_cast<int>(std::strlen(expression));
    int pos = 0;
    return expand_roots_range(expression, 0, len, out, pos, cap);
}

struct ComplexValue {
    CalcReal real;
    CalcReal imag;
};

ComplexValue g_eval_stack[kMaxStack]{};

EvalResult fail(EvalError error, int pos) {
    EvalResult result{};
    result.ok = false;
    result.value = real(0.0);
    result.imag = real(0.0);
    result.error = error;
    result.error_pos = pos;
    return result;
}

EvalResult ok(ComplexValue value) {
    EvalResult result{};
    result.ok = true;
    result.value = value.real;
    result.imag = value.imag;
    result.error = EvalError::None;
    result.error_pos = -1;
    return result;
}

EvalResult evaluate_impl(const char* expression,
                         EvalContext& context,
                         char override_variable,
                         CalcReal override_value,
                         bool update_ans);

ComplexValue make_complex(CalcReal real_part, CalcReal imag_part) {
    return {real_part, imag_part};
}

bool finite_complex(ComplexValue value) {
    return std::isfinite(value.real) && std::isfinite(value.imag);
}

bool zero_complex(ComplexValue value) {
    return value.real == real(0.0) && value.imag == real(0.0);
}

CalcReal clean_zero(CalcReal value) {
    return value == real(0.0) ? real(0.0) : value;
}

ComplexValue add_complex(ComplexValue lhs, ComplexValue rhs) {
    return {lhs.real + rhs.real, lhs.imag + rhs.imag};
}

ComplexValue sub_complex(ComplexValue lhs, ComplexValue rhs) {
    return {lhs.real - rhs.real, lhs.imag - rhs.imag};
}

ComplexValue mul_complex(ComplexValue lhs, ComplexValue rhs) {
    return {lhs.real * rhs.real - lhs.imag * rhs.imag, lhs.real * rhs.imag + lhs.imag * rhs.real};
}

ComplexValue scale_complex(ComplexValue value, CalcReal scale) {
    return {value.real * scale, value.imag * scale};
}

ComplexValue div_complex(ComplexValue lhs, ComplexValue rhs) {
    const CalcReal denom = rhs.real * rhs.real + rhs.imag * rhs.imag;
    return {(lhs.real * rhs.real + lhs.imag * rhs.imag) / denom,
            (lhs.imag * rhs.real - lhs.real * rhs.imag) / denom};
}

ComplexValue neg_complex(ComplexValue value) {
    return {clean_zero(-value.real), clean_zero(-value.imag)};
}

ComplexValue sqrt_complex(ComplexValue value) {
    if (value.imag == real(0.0) && value.real >= real(0.0)) {
        return {std::sqrt(value.real), real(0.0)};
    }
    const CalcReal magnitude = std::sqrt(value.real * value.real + value.imag * value.imag);
    CalcReal real_part = std::sqrt((magnitude + value.real) / real(2.0));
    CalcReal imag_part = std::sqrt((magnitude - value.real) / real(2.0));
    if (value.imag < real(0.0)) {
        imag_part = -imag_part;
    }
    return {real_part, imag_part};
}

ComplexValue exp_complex(ComplexValue value) {
    const CalcReal scale = std::exp(value.real);
    return {scale * std::cos(value.imag), scale * std::sin(value.imag)};
}

ComplexValue log_complex(ComplexValue value) {
    return {std::log(std::sqrt(value.real * value.real + value.imag * value.imag)),
            std::atan2(clean_zero(value.imag), clean_zero(value.real))};
}

ComplexValue pow_complex(ComplexValue lhs, ComplexValue rhs) {
    if (zero_complex(lhs)) {
        return zero_complex(rhs) ? make_complex(real(1.0), real(0.0)) : make_complex(real(0.0), real(0.0));
    }
    return exp_complex(mul_complex(rhs, log_complex(lhs)));
}

ComplexValue sin_complex(ComplexValue value) {
    return {std::sin(value.real) * std::cosh(value.imag), std::cos(value.real) * std::sinh(value.imag)};
}

ComplexValue cos_complex(ComplexValue value) {
    return {std::cos(value.real) * std::cosh(value.imag), -std::sin(value.real) * std::sinh(value.imag)};
}

ComplexValue tan_complex(ComplexValue value) {
    return div_complex(sin_complex(value), cos_complex(value));
}

ComplexValue asin_complex(ComplexValue value) {
    const ComplexValue iz = {-value.imag, value.real};
    const ComplexValue root = sqrt_complex(sub_complex(make_complex(real(1.0), real(0.0)), mul_complex(value, value)));
    const ComplexValue logged = log_complex(add_complex(iz, root));
    return {logged.imag, -logged.real};
}

ComplexValue acos_complex(ComplexValue value) {
    const ComplexValue asin_value = asin_complex(value);
    return {kPi / real(2.0) - asin_value.real, -asin_value.imag};
}

ComplexValue atan_complex(ComplexValue value) {
    const ComplexValue iz = {-value.imag, value.real};
    const ComplexValue one = make_complex(real(1.0), real(0.0));
    const ComplexValue logged = log_complex(div_complex(sub_complex(one, iz), add_complex(one, iz)));
    return {logged.imag / real(2.0), -logged.real / real(2.0)};
}

bool is_near_integer(CalcReal value, int& integer) {
    if (value < real(-2147483647.0) || value > real(2147483647.0)) {
        return false;
    }
    const CalcReal rounded = value >= real(0.0) ? std::floor(value + real(0.5)) : std::ceil(value - real(0.5));
    if (std::fabs(value - rounded) > real(0.000000001)) {
        return false;
    }
    integer = static_cast<int>(rounded);
    return true;
}

CalcReal pow_integer(CalcReal base, int exponent) {
    if (exponent == 0) {
        return real(1.0);
    }
    bool negative = exponent < 0;
    unsigned int count = negative ? static_cast<unsigned int>(-exponent) : static_cast<unsigned int>(exponent);
    CalcReal result = real(1.0);
    CalcReal factor = base;
    while (count > 0u) {
        if ((count & 1u) != 0u) {
            result *= factor;
        }
        factor *= factor;
        count >>= 1u;
    }
    return negative ? real(1.0) / result : result;
}

CalcReal pow_fast(CalcReal lhs, CalcReal rhs) {
    int exponent = 0;
    if (is_near_integer(rhs, exponent) && exponent >= -16 && exponent <= 16) {
        return pow_integer(lhs, exponent);
    }
    return std::pow(lhs, rhs);
}

bool near_zero(CalcReal value) {
    return std::fabs(value) <= real(0.000000000001);
}

bool scalar_value(ComplexValue value, CalcReal& out) {
    if (!near_zero(value.imag)) {
        return false;
    }
    out = value.real;
    return true;
}

bool integer_value(ComplexValue value, int& out) {
    CalcReal scalar = real(0.0);
    return scalar_value(value, scalar) && is_near_integer(scalar, out);
}

int abs_int(int value) {
    return value < 0 ? -value : value;
}

int gcd_int(int lhs, int rhs) {
    lhs = abs_int(lhs);
    rhs = abs_int(rhs);
    while (rhs != 0) {
        const int rem = lhs % rhs;
        lhs = rhs;
        rhs = rem;
    }
    return lhs;
}

bool valid_arity(Function function, int arity) {
    switch (function) {
        case Function::Sin:
        case Function::Cos:
        case Function::Tan:
        case Function::ASin:
        case Function::ACos:
        case Function::ATan:
        case Function::Sqrt:
        case Function::Log:
        case Function::Ln:
        case Function::Abs:
        case Function::IPart:
        case Function::FPart:
        case Function::Int:
        case Function::Conj:
        case Function::Real:
        case Function::Imag:
        case Function::Angle:
        case Function::Factorial:
            return arity == 1;
        case Function::Round:
            return arity == 1 || arity == 2;
        case Function::Min:
        case Function::Max:
            return arity >= 1;
        case Function::Lcm:
        case Function::Gcd:
        case Function::Remainder:
        case Function::LogBase:
        case Function::RandInt:
        case Function::RandNorm:
        case Function::NPr:
        case Function::NCr:
            return arity == 2;
        case Function::Rand:
            return arity == 0;
        case Function::RandBin:
            return arity == 2 || arity == 3;
        case Function::RandIntNoRep:
            return arity == 2;
    }
    return false;
}

std::uint32_t next_random_u32(EvalContext& context) {
    if (context.rng_state == 0u) {
        context.rng_state = 0x1234abcdU;
    }
    context.rng_state = context.rng_state * 1664525u + 1013904223u;
    return context.rng_state;
}

CalcReal random_unit(EvalContext& context) {
    return static_cast<CalcReal>((next_random_u32(context) >> 8) & 0x00ffffffu) / real(16777216.0);
}

CalcReal factorial_value(int n) {
    CalcReal result = real(1.0);
    for (int i = 2; i <= n; ++i) {
        result *= static_cast<CalcReal>(i);
    }
    return result;
}

bool permutation_value(int n, int r, CalcReal& out) {
    if (n < 0 || r < 0 || r > n) {
        return false;
    }
    out = real(1.0);
    for (int i = 0; i < r; ++i) {
        out *= static_cast<CalcReal>(n - i);
    }
    return std::isfinite(out);
}

bool combination_value(int n, int r, CalcReal& out) {
    if (n < 0 || r < 0 || r > n) {
        return false;
    }
    if (r > n - r) {
        r = n - r;
    }
    out = real(1.0);
    for (int i = 1; i <= r; ++i) {
        out = out * static_cast<CalcReal>(n - r + i) / static_cast<CalcReal>(i);
    }
    return std::isfinite(out);
}

enum class SpecialFunction : std::uint8_t {
    None,
    FMin,
    FMax,
    NDeriv,
    FnInt,
    Sum
};

struct ArgRange {
    int start;
    int end;
};

bool same_word_at(const char* text, int pos, const char* word) {
    for (int i = 0; word[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[pos + i])) != word[i]) {
            return false;
        }
    }
    return true;
}

SpecialFunction special_function_at(const char* expr, int pos, int end, int& name_len) {
    struct SpecialName {
        const char* name;
        SpecialFunction function;
    };
    constexpr SpecialName names[] = {
        {"fmin", SpecialFunction::FMin},
        {"fmax", SpecialFunction::FMax},
        {"nderiv", SpecialFunction::NDeriv},
        {"fnint", SpecialFunction::FnInt},
        {"sum", SpecialFunction::Sum},
    };
    for (const SpecialName& entry : names) {
        const int len = static_cast<int>(std::strlen(entry.name));
        if (pos + len < end && same_word_at(expr, pos, entry.name) && is_open_paren(expr[pos + len])) {
            name_len = len;
            return entry.function;
        }
    }
    name_len = 0;
    return SpecialFunction::None;
}

bool contains_special_function(const char* expression) {
    const int len = static_cast<int>(std::strlen(expression));
    for (int pos = 0; pos < len; ++pos) {
        int name_len = 0;
        if (special_function_at(expression, pos, len, name_len) != SpecialFunction::None) {
            return true;
        }
    }
    return false;
}

bool split_args(const char* expr, int start, int end, ArgRange* args, int max_args, int& count) {
    count = 0;
    int depth = 0;
    int arg_start = start;
    for (int i = start; i <= end; ++i) {
        const bool at_end = i == end;
        const char ch = at_end ? ',' : expr[i];
        if (!at_end && is_open_paren(ch)) {
            ++depth;
        } else if (!at_end && is_close_paren(ch)) {
            if (depth <= 0) {
                return false;
            }
            --depth;
        } else if ((at_end || ch == ',') && depth == 0) {
            if (count >= max_args) {
                return false;
            }
            int arg_end = i;
            while (arg_start < arg_end && std::isspace(static_cast<unsigned char>(expr[arg_start]))) {
                ++arg_start;
            }
            while (arg_end > arg_start && std::isspace(static_cast<unsigned char>(expr[arg_end - 1]))) {
                --arg_end;
            }
            if (arg_start == arg_end) {
                return false;
            }
            args[count++] = {arg_start, arg_end};
            arg_start = i + 1;
        }
    }
    return depth == 0;
}

bool copy_range(const char* expr, ArgRange range, char* out, int cap) {
    const int len = range.end - range.start;
    if (len <= 0 || len >= cap) {
        return false;
    }
    for (int i = 0; i < len; ++i) {
        out[i] = expr[range.start + i];
    }
    out[len] = '\0';
    return true;
}

bool parse_variable_arg(const char* expr, ArgRange range, char& variable) {
    int pos = range.start;
    while (pos < range.end && std::isspace(static_cast<unsigned char>(expr[pos]))) {
        ++pos;
    }
    if (pos >= range.end || !std::isalpha(static_cast<unsigned char>(expr[pos]))) {
        return false;
    }
    variable = static_cast<char>(std::toupper(static_cast<unsigned char>(expr[pos])));
    ++pos;
    while (pos < range.end && std::isspace(static_cast<unsigned char>(expr[pos]))) {
        ++pos;
    }
    return pos == range.end && variable >= 'A' && variable <= 'Z';
}

bool evaluate_real_range(const char* expr,
                         ArgRange range,
                         EvalContext& context,
                         char override_variable,
                         CalcReal override_value,
                         CalcReal& out) {
    char buffer[kExpressionCapacity]{};
    if (!copy_range(expr, range, buffer, kExpressionCapacity)) {
        return false;
    }
    EvalResult result = evaluate_impl(buffer, context, override_variable, override_value, false);
    if (!result.ok || !near_zero(result.imag)) {
        return false;
    }
    out = result.value;
    return std::isfinite(out);
}

bool evaluate_real_expression_at(const char* expr,
                                 ArgRange range,
                                 EvalContext& context,
                                 char variable,
                                 CalcReal x,
                                 CalcReal& out) {
    char buffer[kExpressionCapacity]{};
    if (!copy_range(expr, range, buffer, kExpressionCapacity)) {
        return false;
    }
    EvalResult result = evaluate_impl(buffer, context, variable, x, false);
    if (!result.ok || !near_zero(result.imag)) {
        return false;
    }
    out = result.value;
    return std::isfinite(out);
}

bool compute_special_function(const char* expr,
                              SpecialFunction function,
                              const ArgRange* args,
                              int arg_count,
                              EvalContext& context,
                              char override_variable,
                              CalcReal override_value,
                              CalcReal& out) {
    if (function == SpecialFunction::NDeriv) {
        if (arg_count != 3) {
            return false;
        }
        char variable = '\0';
        CalcReal x = real(0.0);
        if (!parse_variable_arg(expr, args[1], variable) ||
            !evaluate_real_range(expr, args[2], context, override_variable, override_value, x)) {
            return false;
        }
        const CalcReal relative_step = sizeof(CalcReal) == sizeof(float) ? real(0.001) : real(0.00001);
        CalcReal h = std::fabs(x) * relative_step;
        if (h < relative_step) {
            h = relative_step;
        }
        CalcReal lhs = real(0.0);
        CalcReal rhs = real(0.0);
        if (!evaluate_real_expression_at(expr, args[0], context, variable, x - h, lhs) ||
            !evaluate_real_expression_at(expr, args[0], context, variable, x + h, rhs)) {
            return false;
        }
        out = (rhs - lhs) / (real(2.0) * h);
        return std::isfinite(out);
    }

    if (function == SpecialFunction::FnInt) {
        if (arg_count != 4) {
            return false;
        }
        char variable = '\0';
        CalcReal lo = real(0.0);
        CalcReal hi = real(0.0);
        if (!parse_variable_arg(expr, args[1], variable) ||
            !evaluate_real_range(expr, args[2], context, override_variable, override_value, lo) ||
            !evaluate_real_range(expr, args[3], context, override_variable, override_value, hi)) {
            return false;
        }
        constexpr int steps = 128;
        const CalcReal dx = (hi - lo) / static_cast<CalcReal>(steps);
        CalcReal total = real(0.0);
        for (int i = 0; i <= steps; ++i) {
            CalcReal y = real(0.0);
            if (!evaluate_real_expression_at(expr, args[0], context, variable, lo + dx * static_cast<CalcReal>(i), y)) {
                return false;
            }
            total += (i == 0 || i == steps) ? y : y * real(2.0);
        }
        out = total * dx / real(2.0);
        return std::isfinite(out);
    }

    if (function == SpecialFunction::FMin || function == SpecialFunction::FMax) {
        if (arg_count != 4) {
            return false;
        }
        char variable = '\0';
        CalcReal lo = real(0.0);
        CalcReal hi = real(0.0);
        if (!parse_variable_arg(expr, args[1], variable) ||
            !evaluate_real_range(expr, args[2], context, override_variable, override_value, lo) ||
            !evaluate_real_range(expr, args[3], context, override_variable, override_value, hi) ||
            !(hi > lo)) {
            return false;
        }
        const CalcReal inv_phi = real(0.6180339887498948482);
        CalcReal c = hi - (hi - lo) * inv_phi;
        CalcReal d = lo + (hi - lo) * inv_phi;
        CalcReal fc = real(0.0);
        CalcReal fd = real(0.0);
        if (!evaluate_real_expression_at(expr, args[0], context, variable, c, fc) ||
            !evaluate_real_expression_at(expr, args[0], context, variable, d, fd)) {
            return false;
        }
        for (int i = 0; i < 64; ++i) {
            const bool choose_left = function == SpecialFunction::FMin ? fc < fd : fc > fd;
            if (choose_left) {
                hi = d;
                d = c;
                fd = fc;
                c = hi - (hi - lo) * inv_phi;
                if (!evaluate_real_expression_at(expr, args[0], context, variable, c, fc)) {
                    return false;
                }
            } else {
                lo = c;
                c = d;
                fc = fd;
                d = lo + (hi - lo) * inv_phi;
                if (!evaluate_real_expression_at(expr, args[0], context, variable, d, fd)) {
                    return false;
                }
            }
        }
        out = (lo + hi) / real(2.0);
        return std::isfinite(out);
    }

    if (function == SpecialFunction::Sum) {
        if (arg_count != 4) {
            return false;
        }
        char variable = '\0';
        CalcReal start_value = real(0.0);
        CalcReal end_value = real(0.0);
        int start_int = 0;
        int end_int = 0;
        if (!parse_variable_arg(expr, args[1], variable) ||
            !evaluate_real_range(expr, args[2], context, override_variable, override_value, start_value) ||
            !evaluate_real_range(expr, args[3], context, override_variable, override_value, end_value) ||
            !is_near_integer(start_value, start_int) || !is_near_integer(end_value, end_int)) {
            return false;
        }
        const int direction = end_int >= start_int ? 1 : -1;
        const int terms = direction > 0 ? end_int - start_int + 1 : start_int - end_int + 1;
        if (terms > 10000) {
            return false;
        }
        out = real(0.0);
        for (int i = start_int;; i += direction) {
            CalcReal term = real(0.0);
            if (!evaluate_real_expression_at(expr, args[0], context, variable, static_cast<CalcReal>(i), term)) {
                return false;
            }
            out += term;
            if (i == end_int) {
                break;
            }
        }
        return std::isfinite(out);
    }

    return false;
}

bool append_number_text(char* out, int& pos, int cap, CalcReal value) {
    char text[32]{};
    const char* format = sizeof(CalcReal) == sizeof(float) ? "%.9g" : "%.17g";
    const int written = std::snprintf(text, sizeof(text), format, static_cast<double>(value));
    if (written <= 0 || written >= static_cast<int>(sizeof(text))) {
        return false;
    }
    return append_text(out, pos, cap, text);
}

bool expand_special_functions_range(const char* expr,
                                    int start,
                                    int end,
                                    EvalContext& context,
                                    char override_variable,
                                    CalcReal override_value,
                                    char* out,
                                    int& pos,
                                    int cap) {
    for (int i = start; i < end;) {
        int name_len = 0;
        const SpecialFunction function = special_function_at(expr, i, end, name_len);
        if (function != SpecialFunction::None) {
            const int open = i + name_len;
            const int close = matching_paren_local(expr, open, end);
            if (close > open) {
                ArgRange args[5]{};
                int arg_count = 0;
                CalcReal value = real(0.0);
                if (!split_args(expr, open + 1, close, args, 5, arg_count) ||
                    !compute_special_function(expr,
                                              function,
                                              args,
                                              arg_count,
                                              context,
                                              override_variable,
                                              override_value,
                                              value) ||
                    !append_number_text(out, pos, cap, value)) {
                    return false;
                }
                i = close + 1;
                continue;
            }
        }
        if (!append_char(out, pos, cap, expr[i])) {
            return false;
        }
        ++i;
    }
    return true;
}

bool expand_special_functions(const char* expression,
                              EvalContext& context,
                              char override_variable,
                              CalcReal override_value,
                              char* out,
                              int cap) {
    out[0] = '\0';
    int pos = 0;
    const int len = static_cast<int>(std::strlen(expression));
    return expand_special_functions_range(expression, 0, len, context, override_variable, override_value, out, pos, cap);
}

bool parse_to_rpn(const char* expression, ParseState& state) {
    bool expect_operand = true;
    int i = 0;
    bool saw_any = false;

    while (expression[i] != '\0') {
        const unsigned char ch = static_cast<unsigned char>(expression[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }

        saw_any = true;

        if (std::isdigit(ch) || expression[i] == '.') {
            if (!expect_operand && !push_binary_operator(state, '*', i)) {
                return false;
            }
            CalcReal value = 0.0;
            const int start = i;
            if (!parse_number(expression, i, value)) {
                set_error(state, EvalError::InvalidToken, i);
                return false;
            }
            Token token{};
            token.kind = TokenKind::Number;
            token.number = value;
            token.imag = real(0.0);
            token.pos = start;
            if (!push_output(state, token)) {
                return false;
            }
            expect_operand = false;
            continue;
        }

        if (std::isalpha(ch)) {
            if (!expect_operand && !push_binary_operator(state, '*', i)) {
                return false;
            }
            const int start = i;
            while (std::isalpha(static_cast<unsigned char>(expression[i]))) {
                ++i;
            }
            const int len = i - start;

            Function fn{};
            if (same_word(expression + start, len, "pi")) {
                Token token{};
                token.kind = TokenKind::Number;
                token.number = kPi;
                token.imag = real(0.0);
                token.pos = start;
                if (!push_output(state, token)) {
                    return false;
                }
                expect_operand = false;
                continue;
            }
            if (same_word(expression + start, len, "e")) {
                Token token{};
                token.kind = TokenKind::Number;
                token.number = kE;
                token.imag = real(0.0);
                token.pos = start;
                if (!push_output(state, token)) {
                    return false;
                }
                expect_operand = false;
                continue;
            }
            if (len == 1 && expression[start] == 'i') {
                Token token{};
                token.kind = TokenKind::Number;
                token.number = real(0.0);
                token.imag = real(1.0);
                token.pos = start;
                if (!push_output(state, token)) {
                    return false;
                }
                expect_operand = false;
                continue;
            }
            if (same_word(expression + start, len, "ans")) {
                Token token{};
                token.kind = TokenKind::Variable;
                token.variable = '@';
                token.pos = start;
                if (!push_output(state, token)) {
                    return false;
                }
                expect_operand = false;
                continue;
            }
            if (parse_function(expression + start, len, fn)) {
                Token token{};
                token.kind = TokenKind::Function;
                token.function = fn;
                token.arity = 1;
                token.pos = start;
                int next = i;
                while (std::isspace(static_cast<unsigned char>(expression[next]))) {
                    ++next;
                }
                if (fn == Function::Rand && !is_open_paren(expression[next])) {
                    token.arity = 0;
                    if (!push_output(state, token)) {
                        return false;
                    }
                    expect_operand = false;
                    continue;
                }
                if (!push_operator(state, token)) {
                    return false;
                }
                expect_operand = true;
                continue;
            }
            if (len == 1) {
                Token token{};
                token.kind = TokenKind::Variable;
                token.variable = static_cast<char>(std::toupper(static_cast<unsigned char>(expression[start])));
                token.pos = start;
                if (!push_output(state, token)) {
                    return false;
                }
                expect_operand = false;
                continue;
            }

            set_error(state, EvalError::UnknownIdentifier, start);
            return false;
        }

        if (is_open_paren(expression[i])) {
            if (!expect_operand && !push_binary_operator(state, '*', i)) {
                return false;
            }
            Token token{};
            token.kind = TokenKind::LParen;
            token.pos = i;
            if (!push_operator(state, token)) {
                return false;
            }
            ++i;
            expect_operand = true;
            continue;
        }

        if (expression[i] == ',') {
            if (expect_operand) {
                set_error(state, EvalError::UnexpectedToken, i);
                return false;
            }
            bool found_lparen = false;
            while (state.operator_count > 0) {
                const Token top = state.operators[state.operator_count - 1];
                if (top.kind == TokenKind::LParen) {
                    found_lparen = true;
                    break;
                }
                if (!pop_operator_to_output(state)) {
                    return false;
                }
            }
            if (!found_lparen || state.operator_count < 2 ||
                state.operators[state.operator_count - 2].kind != TokenKind::Function) {
                set_error(state, EvalError::UnexpectedToken, i);
                return false;
            }
            ++state.operators[state.operator_count - 2].arity;
            ++i;
            expect_operand = true;
            continue;
        }

        if (is_close_paren(expression[i])) {
            const bool empty_argument = expect_operand;
            bool found_lparen = false;
            while (state.operator_count > 0) {
                const Token top = state.operators[state.operator_count - 1];
                if (top.kind == TokenKind::LParen) {
                    --state.operator_count;
                    found_lparen = true;
                    break;
                }
                if (!pop_operator_to_output(state)) {
                    return false;
                }
            }
            if (!found_lparen) {
                set_error(state, EvalError::MismatchedParentheses, i);
                return false;
            }
            if (state.operator_count > 0 && state.operators[state.operator_count - 1].kind == TokenKind::Function) {
                if (empty_argument) {
                    if (state.operators[state.operator_count - 1].arity == 1) {
                        state.operators[state.operator_count - 1].arity = 0;
                    } else {
                        set_error(state, EvalError::UnexpectedToken, i);
                        return false;
                    }
                }
                if (!pop_operator_to_output(state)) {
                    return false;
                }
            } else if (empty_argument) {
                set_error(state, EvalError::UnexpectedToken, i);
                return false;
            }
            ++i;
            expect_operand = false;
            continue;
        }

        if (expression[i] == '!') {
            if (expect_operand) {
                set_error(state, EvalError::UnexpectedToken, i);
                return false;
            }
            Token token{};
            token.kind = TokenKind::Function;
            token.function = Function::Factorial;
            token.arity = 1;
            token.pos = i;
            if (!push_output(state, token)) {
                return false;
            }
            ++i;
            expect_operand = false;
            continue;
        }

        if (expression[i] == '+' || expression[i] == '-' || expression[i] == '*' || expression[i] == '/' ||
            expression[i] == '^') {
            char op = expression[i];
            if (expect_operand) {
                if (op == '+') {
                    ++i;
                    continue;
                }
                if (op == '-') {
                    op = '~';
                } else {
                    set_error(state, EvalError::UnexpectedToken, i);
                    return false;
                }
            }

            if (!push_binary_operator(state, op, i)) {
                return false;
            }
            ++i;
            expect_operand = true;
            continue;
        }

        set_error(state, EvalError::InvalidToken, i);
        return false;
    }

    if (!saw_any) {
        set_error(state, EvalError::EmptyExpression, 0);
        return false;
    }
    if (expect_operand) {
        set_error(state, EvalError::UnexpectedToken, i);
        return false;
    }

    while (state.operator_count > 0) {
        if (state.operators[state.operator_count - 1].kind == TokenKind::LParen) {
            set_error(state, EvalError::MismatchedParentheses, state.operators[state.operator_count - 1].pos);
            return false;
        }
        if (!pop_operator_to_output(state)) {
            return false;
        }
    }

    return true;
}

EvalResult evaluate_rpn(const ParseState& state, EvalContext& context, char override_variable, CalcReal override_value) {
    ComplexValue* stack = g_eval_stack;
    int stack_count = 0;

    for (int i = 0; i < state.output_count; ++i) {
        const Token& token = state.output[i];
        if (token.kind == TokenKind::Number) {
            if (stack_count >= kMaxStack) {
                return fail(EvalError::StackOverflow, token.pos);
            }
            stack[stack_count++] = make_complex(token.number, token.imag);
            continue;
        }

        if (token.kind == TokenKind::Variable) {
            ComplexValue value{};
            if (token.variable == '@') {
                value = make_complex(context.ans, context.ans_imag);
            } else {
                const int idx = token.variable - 'A';
                if (idx < 0 || idx >= 26) {
                    return fail(EvalError::UnknownIdentifier, token.pos);
                }
                if (override_variable != '\0' && token.variable == override_variable) {
                    value = make_complex(override_value, real(0.0));
                } else if (context.variable_valid[idx]) {
                    value = make_complex(context.variables[idx], context.variable_imag[idx]);
                } else {
                    return fail(EvalError::UnknownIdentifier, token.pos);
                }
            }
            if (stack_count >= kMaxStack) {
                return fail(EvalError::StackOverflow, token.pos);
            }
            stack[stack_count++] = value;
            continue;
        }

        if (token.kind == TokenKind::Operator) {
            if (token.op == '~') {
                if (stack_count < 1) {
                    return fail(EvalError::UnexpectedToken, token.pos);
                }
                stack[stack_count - 1] = neg_complex(stack[stack_count - 1]);
            } else {
                if (stack_count < 2) {
                    return fail(EvalError::UnexpectedToken, token.pos);
                }
                const ComplexValue rhs = stack[--stack_count];
                const ComplexValue lhs = stack[--stack_count];
                ComplexValue value{};
                switch (token.op) {
                    case '+': value = add_complex(lhs, rhs); break;
                    case '-': value = sub_complex(lhs, rhs); break;
                    case '*': value = mul_complex(lhs, rhs); break;
                    case '/':
                        if (zero_complex(rhs)) {
                            return fail(EvalError::DivideByZero, token.pos);
                        }
                        value = div_complex(lhs, rhs);
                        break;
                    case '^':
                        if (lhs.imag == real(0.0) && rhs.imag == real(0.0)) {
                            int integer = 0;
                            if (lhs.real >= real(0.0) || is_near_integer(rhs.real, integer)) {
                                value = make_complex(pow_fast(lhs.real, rhs.real), real(0.0));
                            } else {
                                value = pow_complex(lhs, rhs);
                            }
                        } else {
                            value = pow_complex(lhs, rhs);
                        }
                        break;
                    default: return fail(EvalError::InvalidToken, token.pos);
                }
                stack[stack_count++] = value;
            }
            if (!finite_complex(stack[stack_count - 1])) {
                return fail(EvalError::Overflow, token.pos);
            }
            continue;
        }

        if (token.kind == TokenKind::Function) {
            if (!valid_arity(token.function, token.arity) || stack_count < token.arity) {
                return fail(EvalError::UnexpectedToken, token.pos);
            }
            ComplexValue* args = stack + stack_count - token.arity;
            ComplexValue value{};
            switch (token.function) {
                case Function::Sin: {
                    ComplexValue arg = args[0];
                    if (context.degree_mode) {
                        arg = scale_complex(arg, kPi / real(180.0));
                    }
                    value = sin_complex(arg);
                    break;
                }
                case Function::Cos: {
                    ComplexValue arg = args[0];
                    if (context.degree_mode) {
                        arg = scale_complex(arg, kPi / real(180.0));
                    }
                    value = cos_complex(arg);
                    break;
                }
                case Function::Tan: {
                    ComplexValue arg = args[0];
                    if (context.degree_mode) {
                        arg = scale_complex(arg, kPi / real(180.0));
                    }
                    value = tan_complex(arg);
                    break;
                }
                case Function::ASin:
                    value = asin_complex(args[0]);
                    if (context.degree_mode) {
                        value = scale_complex(value, real(180.0) / kPi);
                    }
                    break;
                case Function::ACos:
                    value = acos_complex(args[0]);
                    if (context.degree_mode) {
                        value = scale_complex(value, real(180.0) / kPi);
                    }
                    break;
                case Function::ATan:
                    value = atan_complex(args[0]);
                    if (context.degree_mode) {
                        value = scale_complex(value, real(180.0) / kPi);
                    }
                    break;
                case Function::Sqrt:
                    value = sqrt_complex(args[0]);
                    break;
                case Function::Log:
                    value = div_complex(log_complex(args[0]), make_complex(std::log(real(10.0)), real(0.0)));
                    break;
                case Function::Ln:
                    value = log_complex(args[0]);
                    break;
                case Function::Abs:
                    value = make_complex(std::sqrt(args[0].real * args[0].real + args[0].imag * args[0].imag), real(0.0));
                    break;
                case Function::Round: {
                    int digits = 0;
                    if (token.arity == 2 && !integer_value(args[1], digits)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    if (digits < -9 || digits > 9) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    const CalcReal scale = pow_integer(real(10.0), digits);
                    value = make_complex(std::round(args[0].real * scale) / scale,
                                         std::round(args[0].imag * scale) / scale);
                    break;
                }
                case Function::IPart: {
                    CalcReal scalar = real(0.0);
                    if (!scalar_value(args[0], scalar)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    value = make_complex(scalar >= real(0.0) ? std::floor(scalar) : std::ceil(scalar), real(0.0));
                    break;
                }
                case Function::FPart: {
                    CalcReal scalar = real(0.0);
                    if (!scalar_value(args[0], scalar)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    const CalcReal whole = scalar >= real(0.0) ? std::floor(scalar) : std::ceil(scalar);
                    value = make_complex(scalar - whole, real(0.0));
                    break;
                }
                case Function::Int: {
                    CalcReal scalar = real(0.0);
                    if (!scalar_value(args[0], scalar)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    value = make_complex(std::floor(scalar), real(0.0));
                    break;
                }
                case Function::Min:
                case Function::Max: {
                    CalcReal best = real(0.0);
                    if (!scalar_value(args[0], best)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    for (int a = 1; a < token.arity; ++a) {
                        CalcReal scalar = real(0.0);
                        if (!scalar_value(args[a], scalar)) {
                            return fail(EvalError::Domain, token.pos);
                        }
                        if ((token.function == Function::Min && scalar < best) ||
                            (token.function == Function::Max && scalar > best)) {
                            best = scalar;
                        }
                    }
                    value = make_complex(best, real(0.0));
                    break;
                }
                case Function::Lcm:
                case Function::Gcd:
                case Function::Remainder: {
                    int lhs = 0;
                    int rhs = 0;
                    if (!integer_value(args[0], lhs) || !integer_value(args[1], rhs)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    if (token.function == Function::Gcd) {
                        value = make_complex(static_cast<CalcReal>(gcd_int(lhs, rhs)), real(0.0));
                    } else if (token.function == Function::Lcm) {
                        const int gcd = gcd_int(lhs, rhs);
                        if (gcd == 0) {
                            value = make_complex(real(0.0), real(0.0));
                        } else {
                            value = make_complex(static_cast<CalcReal>(abs_int(lhs / gcd * rhs)), real(0.0));
                        }
                    } else {
                        if (rhs == 0) {
                            return fail(EvalError::DivideByZero, token.pos);
                        }
                        value = make_complex(static_cast<CalcReal>(lhs % rhs), real(0.0));
                    }
                    break;
                }
                case Function::LogBase: {
                    CalcReal value_arg = real(0.0);
                    CalcReal base = real(0.0);
                    if (!scalar_value(args[0], value_arg) || !scalar_value(args[1], base) ||
                        value_arg <= real(0.0) || base <= real(0.0) || base == real(1.0)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    value = make_complex(std::log(value_arg) / std::log(base), real(0.0));
                    break;
                }
                case Function::Conj:
                    value = make_complex(args[0].real, -args[0].imag);
                    break;
                case Function::Real:
                    value = make_complex(args[0].real, real(0.0));
                    break;
                case Function::Imag:
                    value = make_complex(args[0].imag, real(0.0));
                    break;
                case Function::Angle: {
                    CalcReal angle = std::atan2(clean_zero(args[0].imag), clean_zero(args[0].real));
                    if (context.degree_mode) {
                        angle *= real(180.0) / kPi;
                    }
                    value = make_complex(angle, real(0.0));
                    break;
                }
                case Function::Rand:
                    value = make_complex(random_unit(context), real(0.0));
                    break;
                case Function::RandInt:
                case Function::RandIntNoRep: {
                    int lo = 0;
                    int hi = 0;
                    if (!integer_value(args[0], lo) || !integer_value(args[1], hi)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    if (hi < lo) {
                        const int tmp = hi;
                        hi = lo;
                        lo = tmp;
                    }
                    const std::uint32_t span = static_cast<std::uint32_t>(hi - lo + 1);
                    value = make_complex(static_cast<CalcReal>(lo + static_cast<int>(next_random_u32(context) % span)),
                                         real(0.0));
                    break;
                }
                case Function::RandNorm: {
                    CalcReal mean = real(0.0);
                    CalcReal sd = real(0.0);
                    if (!scalar_value(args[0], mean) || !scalar_value(args[1], sd) || sd < real(0.0)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    CalcReal u1 = random_unit(context);
                    const CalcReal u2 = random_unit(context);
                    if (u1 <= real(0.0)) {
                        u1 = real(1.0) / real(16777216.0);
                    }
                    const CalcReal z = std::sqrt(real(-2.0) * std::log(u1)) * std::cos(real(2.0) * kPi * u2);
                    value = make_complex(mean + sd * z, real(0.0));
                    break;
                }
                case Function::RandBin: {
                    int trials = 0;
                    CalcReal p = real(0.0);
                    if (!integer_value(args[0], trials) || !scalar_value(args[1], p) ||
                        trials < 0 || trials > 10000 || p < real(0.0) || p > real(1.0)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    int count = 0;
                    for (int trial = 0; trial < trials; ++trial) {
                        if (random_unit(context) < p) {
                            ++count;
                        }
                    }
                    value = make_complex(static_cast<CalcReal>(count), real(0.0));
                    break;
                }
                case Function::NPr:
                case Function::NCr: {
                    int n = 0;
                    int r = 0;
                    if (!integer_value(args[0], n) || !integer_value(args[1], r)) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    CalcReal computed = real(0.0);
                    const bool ok = token.function == Function::NPr ? permutation_value(n, r, computed)
                                                                     : combination_value(n, r, computed);
                    if (!ok) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    value = make_complex(computed, real(0.0));
                    break;
                }
                case Function::Factorial: {
                    int n = 0;
                    if (!integer_value(args[0], n) || n < 0 || n > 170) {
                        return fail(EvalError::Domain, token.pos);
                    }
                    value = make_complex(factorial_value(n), real(0.0));
                    break;
                }
            }
            if (!finite_complex(value)) {
                return fail(EvalError::Domain, token.pos);
            }
            stack_count -= token.arity;
            if (stack_count >= kMaxStack) {
                return fail(EvalError::StackOverflow, token.pos);
            }
            stack[stack_count++] = value;
            continue;
        }
    }

    if (stack_count != 1) {
        return fail(EvalError::UnexpectedToken, 0);
    }
    return ok(stack[0]);
}

const char* skip_spaces(const char* text) {
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) {
        ++text;
    }
    return text;
}

int find_store_arrow(const char* expression) {
    int depth = 0;
    for (int i = 0; expression[i] != '\0'; ++i) {
        if (is_open_paren(expression[i])) {
            ++depth;
        } else if (is_close_paren(expression[i]) && depth > 0) {
            --depth;
        } else if (depth == 0 && expression[i] == '-' && expression[i + 1] == '>') {
            return i;
        }
    }
    return -1;
}

EvalResult evaluate_impl(const char* expression,
                         EvalContext& context,
                         char override_variable,
                         CalcReal override_value,
                         bool update_ans) {
    if (expression == nullptr) {
        return fail(EvalError::EmptyExpression, 0);
    }

    const char* start = skip_spaces(expression);
    const int store_pos = find_store_arrow(start);
    if (store_pos >= 0) {
        const char* rhs = skip_spaces(start + store_pos + 2);
        if (!std::isalpha(static_cast<unsigned char>(rhs[0])) || skip_spaces(rhs + 1)[0] != '\0') {
            return fail(EvalError::Assignment, static_cast<int>(rhs - expression));
        }
        const char variable = static_cast<char>(std::toupper(static_cast<unsigned char>(rhs[0])));
        if (variable < 'A' || variable > 'Z') {
            return fail(EvalError::Assignment, static_cast<int>(rhs - expression));
        }
        char lhs[kExpressionCapacity]{};
        if (store_pos <= 0 || store_pos >= kExpressionCapacity) {
            return fail(EvalError::Assignment, static_cast<int>(start - expression + store_pos));
        }
        for (int i = 0; i < store_pos; ++i) {
            lhs[i] = start[i];
        }
        lhs[store_pos] = '\0';
        EvalResult assigned = evaluate_impl(lhs, context, override_variable, override_value, update_ans);
        if (!assigned.ok) {
            return assigned;
        }
        const int idx = variable - 'A';
        context.variables[idx] = assigned.value;
        context.variable_imag[idx] = assigned.imag;
        context.variable_valid[idx] = true;
        if (update_ans) {
            context.ans = assigned.value;
            context.ans_imag = assigned.imag;
        }
        return assigned;
    }

    if (std::isalpha(static_cast<unsigned char>(start[0]))) {
        const char variable = static_cast<char>(std::toupper(static_cast<unsigned char>(start[0])));
        const char* after_var = skip_spaces(start + 1);
        if (*after_var == '=') {
            if (variable < 'A' || variable > 'Z') {
                return fail(EvalError::Assignment, static_cast<int>(start - expression));
            }
            EvalResult assigned = evaluate_impl(after_var + 1, context, override_variable, override_value, update_ans);
            if (!assigned.ok) {
                return assigned;
            }
            const int idx = variable - 'A';
            context.variables[idx] = assigned.value;
            context.variable_imag[idx] = assigned.imag;
            context.variable_valid[idx] = true;
            if (update_ans) {
                context.ans = assigned.value;
                context.ans_imag = assigned.imag;
            }
            return assigned;
        }
    }

    const bool cacheable = !contains_special_function(start);
    if (cacheable && g_cached_parse_valid && std::strcmp(start, g_cached_source) == 0) {
        EvalResult result = evaluate_rpn(g_cached_parse, context, override_variable, override_value);
        if (result.ok && update_ans) {
            context.ans = result.value;
            context.ans_imag = result.imag;
        }
        return result;
    }

    char special_expanded[kExpressionCapacity]{};
    if (!expand_special_functions(start, context, override_variable, override_value, special_expanded, kExpressionCapacity)) {
        return fail(EvalError::Domain, 0);
    }
    char expanded[kExpressionCapacity]{};
    if (!expand_roots(special_expanded, expanded, kExpressionCapacity)) {
        return fail(EvalError::TooManyTokens, 0);
    }
    ParseState& state = g_cached_parse;
    state.output_count = 0;
    state.operator_count = 0;
    state.error = EvalError::None;
    state.error_pos = -1;
    if (!parse_to_rpn(expanded, state)) {
        g_cached_parse_valid = false;
        return fail(state.error, state.error_pos);
    }
    if (cacheable) {
        std::snprintf(g_cached_source, sizeof(g_cached_source), "%s", start);
        g_cached_parse_valid = true;
    } else {
        g_cached_parse_valid = false;
    }

    EvalResult result = evaluate_rpn(state, context, override_variable, override_value);
    if (result.ok && update_ans) {
        context.ans = result.value;
        context.ans_imag = result.imag;
    }
    return result;
}

}  // namespace

void eval_context_init(EvalContext& context) {
    for (int i = 0; i < 26; ++i) {
        context.variables[i] = 0.0;
        context.variable_imag[i] = 0.0;
        context.variable_valid[i] = false;
    }
    context.ans = 0.0;
    context.ans_imag = 0.0;
    context.degree_mode = false;
    context.rng_state = 0x1234abcdU;
    context.variables['X' - 'A'] = 0.0;
    context.variable_imag['X' - 'A'] = 0.0;
    context.variable_valid['X' - 'A'] = true;
}

EvalResult evaluate_expression(const char* expression, EvalContext& context) {
    return evaluate_impl(expression, context, '\0', 0.0, true);
}

EvalResult evaluate_expression_with_x(const char* expression, EvalContext& context, CalcReal x_value) {
    return evaluate_impl(expression, context, 'X', x_value, true);
}

EvalResult evaluate_expression_with_x_readonly(const char* expression, const EvalContext& context, CalcReal x_value) {
    EvalContext copy = context;
    return evaluate_impl(expression, copy, 'X', x_value, false);
}

const char* eval_error_text(EvalError error) {
    switch (error) {
        case EvalError::None: return "OK";
        case EvalError::EmptyExpression: return "EMPTY";
        case EvalError::InvalidToken: return "BAD TOKEN";
        case EvalError::UnexpectedToken: return "SYNTAX";
        case EvalError::TooManyTokens: return "TOO LONG";
        case EvalError::StackOverflow: return "STACK";
        case EvalError::MismatchedParentheses: return "PARENS";
        case EvalError::UnknownIdentifier: return "UNKNOWN";
        case EvalError::DivideByZero: return "DIV ZERO";
        case EvalError::Domain: return "DOMAIN";
        case EvalError::Overflow: return "OVERFLOW";
        case EvalError::Assignment: return "ASSIGN";
        default: return "ERROR";
    }
}

}  // namespace calc
