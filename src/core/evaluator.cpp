#include "calc/calculator.hpp"

#include <cctype>
#include <cmath>
#include <cstring>

namespace calc {
namespace {

constexpr int kMaxTokens = 96;
constexpr int kMaxStack = 32;

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
    Ln
};

struct Token {
    TokenKind kind;
    CalcReal number;
    char op;
    char variable;
    Function function;
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

EvalResult fail(EvalError error, int pos) {
    EvalResult result{};
    result.ok = false;
    result.value = real(0.0);
    result.error = error;
    result.error_pos = pos;
    return result;
}

EvalResult ok(CalcReal value) {
    EvalResult result{};
    result.ok = true;
    result.value = value;
    result.error = EvalError::None;
    result.error_pos = -1;
    return result;
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
            CalcReal value = 0.0;
            const int start = i;
            if (!parse_number(expression, i, value)) {
                set_error(state, EvalError::InvalidToken, i);
                return false;
            }
            Token token{};
            token.kind = TokenKind::Number;
            token.number = value;
            token.pos = start;
            if (!push_output(state, token)) {
                return false;
            }
            expect_operand = false;
            continue;
        }

        if (std::isalpha(ch)) {
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
                token.pos = start;
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

        if (expression[i] == '(') {
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

        if (expression[i] == ')') {
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
                if (!pop_operator_to_output(state)) {
                    return false;
                }
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

            Token token{};
            token.kind = TokenKind::Operator;
            token.op = op;
            token.pos = i;

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

            if (!push_operator(state, token)) {
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

EvalResult evaluate_rpn(const ParseState& state, EvalContext& context, bool override_x, CalcReal x_value) {
    CalcReal stack[kMaxStack]{};
    int stack_count = 0;

    for (int i = 0; i < state.output_count; ++i) {
        const Token& token = state.output[i];
        if (token.kind == TokenKind::Number) {
            if (stack_count >= kMaxStack) {
                return fail(EvalError::StackOverflow, token.pos);
            }
            stack[stack_count++] = token.number;
            continue;
        }

        if (token.kind == TokenKind::Variable) {
            CalcReal value = 0.0;
            if (token.variable == '@') {
                value = context.ans;
            } else {
                const int idx = token.variable - 'A';
                if (idx < 0 || idx >= 26) {
                    return fail(EvalError::UnknownIdentifier, token.pos);
                }
                if (override_x && token.variable == 'X') {
                    value = x_value;
                } else if (context.variable_valid[idx]) {
                    value = context.variables[idx];
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
                stack[stack_count - 1] = -stack[stack_count - 1];
            } else {
                if (stack_count < 2) {
                    return fail(EvalError::UnexpectedToken, token.pos);
                }
                const CalcReal rhs = stack[--stack_count];
                const CalcReal lhs = stack[--stack_count];
                CalcReal value = 0.0;
                switch (token.op) {
                    case '+': value = lhs + rhs; break;
                    case '-': value = lhs - rhs; break;
                    case '*': value = lhs * rhs; break;
                    case '/':
                        if (rhs == 0.0) {
                            return fail(EvalError::DivideByZero, token.pos);
                        }
                        value = lhs / rhs;
                        break;
                    case '^': value = pow_fast(lhs, rhs); break;
                    default: return fail(EvalError::InvalidToken, token.pos);
                }
                stack[stack_count++] = value;
            }
            if (!std::isfinite(stack[stack_count - 1])) {
                return fail(EvalError::Overflow, token.pos);
            }
            continue;
        }

        if (token.kind == TokenKind::Function) {
            if (stack_count < 1) {
                return fail(EvalError::UnexpectedToken, token.pos);
            }
            const CalcReal arg = stack[stack_count - 1];
            CalcReal value = 0.0;
            switch (token.function) {
                case Function::Sin: value = std::sin(arg); break;
                case Function::Cos: value = std::cos(arg); break;
                case Function::Tan: value = std::tan(arg); break;
                case Function::ASin: value = std::asin(arg); break;
                case Function::ACos: value = std::acos(arg); break;
                case Function::ATan: value = std::atan(arg); break;
                case Function::Sqrt: value = std::sqrt(arg); break;
                case Function::Log: value = std::log10(arg); break;
                case Function::Ln: value = std::log(arg); break;
            }
            if (!std::isfinite(value)) {
                return fail(EvalError::Domain, token.pos);
            }
            stack[stack_count - 1] = value;
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
        if (expression[i] == '(') {
            ++depth;
        } else if (expression[i] == ')' && depth > 0) {
            --depth;
        } else if (depth == 0 && expression[i] == '-' && expression[i + 1] == '>') {
            return i;
        }
    }
    return -1;
}

EvalResult evaluate_impl(const char* expression, EvalContext& context, bool override_x, CalcReal x_value, bool update_ans) {
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
        EvalResult assigned = evaluate_impl(lhs, context, override_x, x_value, update_ans);
        if (!assigned.ok) {
            return assigned;
        }
        const int idx = variable - 'A';
        context.variables[idx] = assigned.value;
        context.variable_valid[idx] = true;
        if (update_ans) {
            context.ans = assigned.value;
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
            EvalResult assigned = evaluate_impl(after_var + 1, context, override_x, x_value, update_ans);
            if (!assigned.ok) {
                return assigned;
            }
            const int idx = variable - 'A';
            context.variables[idx] = assigned.value;
            context.variable_valid[idx] = true;
            if (update_ans) {
                context.ans = assigned.value;
            }
            return assigned;
        }
    }

    ParseState state{};
    state.error = EvalError::None;
    state.error_pos = -1;
    if (!parse_to_rpn(expression, state)) {
        return fail(state.error, state.error_pos);
    }

    EvalResult result = evaluate_rpn(state, context, override_x, x_value);
    if (result.ok && update_ans) {
        context.ans = result.value;
    }
    return result;
}

}  // namespace

void eval_context_init(EvalContext& context) {
    for (int i = 0; i < 26; ++i) {
        context.variables[i] = 0.0;
        context.variable_valid[i] = false;
    }
    context.ans = 0.0;
    context.variables['X' - 'A'] = 0.0;
    context.variable_valid['X' - 'A'] = true;
}

EvalResult evaluate_expression(const char* expression, EvalContext& context) {
    return evaluate_impl(expression, context, false, 0.0, true);
}

EvalResult evaluate_expression_with_x(const char* expression, EvalContext& context, CalcReal x_value) {
    return evaluate_impl(expression, context, true, x_value, true);
}

EvalResult evaluate_expression_with_x_readonly(const char* expression, const EvalContext& context, CalcReal x_value) {
    EvalContext copy = context;
    return evaluate_impl(expression, copy, true, x_value, false);
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
