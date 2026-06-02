#include "calc/calculator.hpp"

#include <SDL.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

constexpr int kLcdX = 20;
constexpr int kLcdY = 20;
constexpr int kButtonW = 58;
constexpr int kButtonH = 28;
constexpr int kButtonGap = 7;
constexpr int kRows = 10;
constexpr int kCols = 5;
constexpr int kKeyboardX = 20;
constexpr int kKeyboardY = kLcdY + calc::kLcdHeight + 18;
constexpr int kKeyboardW = kCols * kButtonW + (kCols - 1) * kButtonGap;
constexpr int kLcdAreaW = kLcdX + calc::kLcdWidth + 20;
constexpr int kKeyboardAreaW = kKeyboardX + kKeyboardW + 20;
constexpr int kWindowW = kLcdAreaW > kKeyboardAreaW ? kLcdAreaW : kKeyboardAreaW;
constexpr int kWindowH = kKeyboardY + kRows * kButtonH + (kRows - 1) * kButtonGap + 20;

struct Button {
    const char* primary;
    const char* second;
    const char* alpha;
    calc::Key key;
    int x;
    int y;
    int w;
    int h;
};

struct EmulatorClock {
    std::chrono::steady_clock::time_point start;
};

std::uint32_t millis(void* context) {
    const auto* clock = static_cast<EmulatorClock*>(context);
    const auto elapsed = std::chrono::steady_clock::now() - clock->start;
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

bool storage_read(void*, const char* key, void* out, std::size_t size) {
    char path[80]{};
    std::snprintf(path, sizeof(path), "calc_%s.bin", key);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
    return input.gcount() == static_cast<std::streamsize>(size);
}

bool storage_write(void*, const char* key, const void* data, std::size_t size) {
    char path[80]{};
    std::snprintf(path, sizeof(path), "calc_%s.bin", key);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(output);
}

void log_message(void*, const char* message) {
    std::fprintf(stderr, "%s\n", message);
}

calc::Key sdl_key(SDL_Keycode code) {
    switch (code) {
        case SDLK_0: return calc::Key::Digit0;
        case SDLK_1: return calc::Key::Digit1;
        case SDLK_2: return calc::Key::Digit2;
        case SDLK_3: return calc::Key::Digit3;
        case SDLK_4: return calc::Key::Digit4;
        case SDLK_5: return calc::Key::Digit5;
        case SDLK_6: return calc::Key::Digit6;
        case SDLK_7: return calc::Key::Digit7;
        case SDLK_8: return calc::Key::Digit8;
        case SDLK_9: return calc::Key::Digit9;
        case SDLK_PERIOD: return calc::Key::Dot;
        case SDLK_COMMA: return calc::Key::Comma;
        case SDLK_PLUS: return calc::Key::Add;
        case SDLK_EQUALS: return calc::Key::Equal;
        case SDLK_MINUS: return calc::Key::Subtract;
        case SDLK_ASTERISK: return calc::Key::Multiply;
        case SDLK_SLASH: return calc::Key::Divide;
        case SDLK_CARET: return calc::Key::Power;
        case SDLK_LEFTPAREN: return calc::Key::LParen;
        case SDLK_RIGHTPAREN: return calc::Key::RParen;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return calc::Key::Enter;
        case SDLK_BACKSPACE: return calc::Key::Back;
        case SDLK_DELETE: return calc::Key::Delete;
        case SDLK_ESCAPE:
        case SDLK_c: return calc::Key::Clear;
        case SDLK_LEFT: return calc::Key::Left;
        case SDLK_RIGHT: return calc::Key::Right;
        case SDLK_UP: return calc::Key::Up;
        case SDLK_DOWN: return calc::Key::Down;
        case SDLK_F1:
        case SDLK_h: return calc::Key::Home;
        case SDLK_F2:
        case SDLK_y: return calc::Key::YEquals;
        case SDLK_F3:
        case SDLK_g: return calc::Key::Graph;
        case SDLK_F4:
        case SDLK_w: return calc::Key::Window;
        case SDLK_F5:
        case SDLK_m: return calc::Key::Settings;
        case SDLK_s: return calc::Key::Sin;
        case SDLK_t: return calc::Key::Tan;
        case SDLK_l: return calc::Key::Ln;
        case SDLK_p: return calc::Key::Pi;
        case SDLK_x: return calc::Key::X;
        case SDLK_a: return calc::Key::LetterA;
        case SDLK_b: return calc::Key::LetterB;
        case SDLK_d: return calc::Key::LetterD;
        case SDLK_e: return calc::Key::LetterE;
        case SDLK_f: return calc::Key::LetterF;
        case SDLK_i: return calc::Key::LetterI;
        case SDLK_j: return calc::Key::LetterJ;
        case SDLK_k: return calc::Key::LetterK;
        case SDLK_n: return calc::Key::LetterN;
        case SDLK_o: return calc::Key::LetterO;
        case SDLK_q: return calc::Key::LetterQ;
        case SDLK_r: return calc::Key::LetterR;
        case SDLK_u: return calc::Key::LetterU;
        case SDLK_v: return calc::Key::LetterV;
        case SDLK_z: return calc::Key::LetterZ;
        default: return calc::Key::None;
    }
}

void add_button_at(std::vector<Button>& buttons,
                   int x,
                   int y,
                   int w,
                   int h,
                   const char* primary,
                   const char* second,
                   const char* alpha,
                   calc::Key key) {
    Button button{};
    button.primary = primary;
    button.second = second;
    button.alpha = alpha;
    button.key = key;
    button.x = x;
    button.y = y;
    button.w = w;
    button.h = h;
    buttons.push_back(button);
}

void add_button(std::vector<Button>& buttons,
                int row,
                int col,
                const char* primary,
                const char* second,
                const char* alpha,
                calc::Key key) {
    add_button_at(buttons,
                  kKeyboardX + col * (kButtonW + kButtonGap),
                  kKeyboardY + row * (kButtonH + kButtonGap),
                  kButtonW,
                  kButtonH,
                  primary,
                  second,
                  alpha,
                  key);
}

std::vector<Button> make_buttons() {
    std::vector<Button> buttons;
    buttons.reserve(kRows * kCols);
    add_button(buttons, 0, 0, "Y=", "STATPLT", "F1", calc::Key::YEquals);
    add_button(buttons, 0, 1, "WINDOW", "TBLSET", "F2", calc::Key::Window);
    add_button(buttons, 0, 2, "ZOOM", "FORMAT", "F3", calc::Key::Zoom);
    add_button(buttons, 0, 3, "TRACE", "CALC", "F4", calc::Key::Trace);
    add_button(buttons, 0, 4, "GRAPH", "TABLE", "F5", calc::Key::Graph);
    add_button(buttons, 1, 0, "2ND", "", "", calc::Key::Second);
    add_button(buttons, 1, 1, "n/d", "MODE", "", calc::Key::Fraction);
    add_button(buttons, 1, 4, "DEL", "", "", calc::Key::Delete);
    add_button(buttons, 2, 0, "ALPHA", "A-LOCK", "", calc::Key::Alpha);
    add_button(buttons, 2, 1, "X,T,t,n", "LINK", "", calc::Key::X);
    add_button(buttons, 2, 4, "STAT", "LIST", "", calc::Key::Stat);
    add_button(buttons, 3, 0, "MATH", "TEST", "A", calc::Key::Math);
    add_button(buttons, 3, 1, "APPS", "ANGLE", "B", calc::Key::Apps);
    add_button(buttons, 3, 2, "PRGM", "DRAW", "C", calc::Key::Program);
    add_button(buttons, 3, 3, "VARS", "DISTR", "", calc::Key::Vars);
    add_button(buttons, 3, 4, "CLEAR", "", "", calc::Key::Clear);
    add_button(buttons, 4, 0, "^", "ROOT", "D", calc::Key::Power);
    add_button(buttons, 4, 1, "SIN", "SIN^-1", "E", calc::Key::Sin);
    add_button(buttons, 4, 2, "COS", "COS^-1", "F", calc::Key::Cos);
    add_button(buttons, 4, 3, "TAN", "TAN^-1", "G", calc::Key::Tan);
    add_button(buttons, 4, 4, "<>", "PI", "H", calc::Key::FracDecimal);
    add_button(buttons, 5, 0, "x^2", "SQRT", "I", calc::Key::Square);
    add_button(buttons, 5, 1, ",", "EE", "J", calc::Key::Comma);
    add_button(buttons, 5, 2, "(", "{", "K", calc::Key::LParen);
    add_button(buttons, 5, 3, ")", "}", "L", calc::Key::RParen);
    add_button(buttons, 5, 4, "/", "e", "M", calc::Key::Divide);
    add_button(buttons, 6, 0, "LOG", "10^x", "N", calc::Key::Log);
    add_button(buttons, 6, 1, "7", "u", "O", calc::Key::Digit7);
    add_button(buttons, 6, 2, "8", "v", "P", calc::Key::Digit8);
    add_button(buttons, 6, 3, "9", "w", "Q", calc::Key::Digit9);
    add_button(buttons, 6, 4, "*", "[", "R", calc::Key::Multiply);
    add_button(buttons, 7, 0, "LN", "e^x", "S", calc::Key::Ln);
    add_button(buttons, 7, 1, "4", "L4", "T", calc::Key::Digit4);
    add_button(buttons, 7, 2, "5", "L5", "U", calc::Key::Digit5);
    add_button(buttons, 7, 3, "6", "L6", "V", calc::Key::Digit6);
    add_button(buttons, 7, 4, "-", "]", "W", calc::Key::Subtract);
    add_button(buttons, 8, 0, "STO>", "RCL", "X", calc::Key::Store);
    add_button(buttons, 8, 1, "1", "L1", "Y", calc::Key::Digit1);
    add_button(buttons, 8, 2, "2", "L2", "Z", calc::Key::Digit2);
    add_button(buttons, 8, 3, "3", "L3", "t", calc::Key::Digit3);
    add_button(buttons, 8, 4, "+", "MEM", "\"", calc::Key::Add);
    add_button(buttons, 9, 0, "ON", "OFF", "", calc::Key::On);
    add_button(buttons, 9, 1, "0", "CATALOG", "SPACE", calc::Key::Digit0);
    add_button(buttons, 9, 2, ".", "", "i", calc::Key::Dot);
    add_button(buttons, 9, 3, "(-)", "ANS", "?", calc::Key::Negate);
    add_button(buttons, 9, 4, "ENTER", "ENTRY", "SOLVE", calc::Key::Enter);

    const int arrow_area_x = kKeyboardX + 2 * (kButtonW + kButtonGap);
    const int arrow_area_y = kKeyboardY + 1 * (kButtonH + kButtonGap);
    const int arrow_area_w = 2 * kButtonW + kButtonGap;
    const int arrow_area_h = 2 * kButtonH + kButtonGap;
    constexpr int arrow_w = 35;
    constexpr int arrow_h = 22;
    const int arrow_center_x = arrow_area_x + (arrow_area_w - arrow_w) / 2;
    const int arrow_center_y = arrow_area_y + (arrow_area_h - arrow_h) / 2;
    add_button_at(buttons, arrow_center_x, arrow_area_y, arrow_w, arrow_h, "UP", "BRI+", "", calc::Key::Up);
    add_button_at(buttons, arrow_area_x + 7, arrow_center_y, arrow_w, arrow_h, "LEFT", "", "", calc::Key::Left);
    add_button_at(buttons, arrow_area_x + arrow_area_w - arrow_w - 7, arrow_center_y, arrow_w, arrow_h, "RIGHT", "", "", calc::Key::Right);
    add_button_at(buttons, arrow_center_x, arrow_area_y + arrow_area_h - arrow_h, arrow_w, arrow_h, "DOWN", "BRI-", "", calc::Key::Down);
    return buttons;
}

void blit_lcd(calc::Display& canvas, const calc::Color* lcd) {
    for (int y = 0; y < calc::kLcdHeight; ++y) {
        for (int x = 0; x < calc::kLcdWidth; ++x) {
            calc::set_pixel(canvas, kLcdX + x, kLcdY + y, lcd[y * calc::kLcdWidth + x]);
        }
    }
}

void draw_buttons(calc::Display& canvas, const std::vector<Button>& buttons, calc::Key held_key) {
    const calc::Color bg = calc::rgb565(42, 45, 52);
    const calc::Color outline = calc::rgb565(20, 22, 27);
    const calc::Color face = calc::rgb565(224, 228, 236);
    const calc::Color active = calc::rgb565(170, 210, 255);
    const calc::Color second = calc::rgb565(38, 86, 160);
    const calc::Color alpha = calc::rgb565(34, 118, 70);
    for (const Button& button : buttons) {
        const bool held = button.key == held_key;
        calc::fill_rect(canvas, button.x, button.y, button.w, button.h, held ? active : face);
        calc::draw_rect(canvas, button.x, button.y, button.w, button.h, outline);
        if (button.second != nullptr && button.second[0] != '\0') {
            calc::draw_text(canvas, button.x + 3, button.y + 3, button.second, second, held ? active : face);
        }
        if (button.alpha != nullptr && button.alpha[0] != '\0') {
            const int alpha_w = static_cast<int>(std::strlen(button.alpha)) * 6;
            calc::draw_text(canvas, button.x + button.w - alpha_w - 3, button.y + 3, button.alpha, alpha, held ? active : face);
        }
        const int label_w = static_cast<int>(std::strlen(button.primary)) * 6;
        int label_x = button.x + (button.w - label_w) / 2;
        if (label_x < button.x + 2) {
            label_x = button.x + 2;
        }
        calc::draw_text(canvas, label_x, button.y + button.h - 12, button.primary, outline, held ? active : face);
    }
    calc::draw_text(canvas, 20, kWindowH - 14, "HOST: KEYS/ARROWS/ENTER  F12 SHOT", calc::rgb565(210, 214, 220), bg);
}

bool save_screenshot(const char* path, const calc::Color* lcd) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "P6\n" << calc::kLcdWidth << " " << calc::kLcdHeight << "\n255\n";
    for (int i = 0; i < calc::kLcdWidth * calc::kLcdHeight; ++i) {
        const std::uint16_t c = lcd[i];
        const unsigned char r = static_cast<unsigned char>(((c >> 11u) & 0x1fu) * 255u / 31u);
        const unsigned char g = static_cast<unsigned char>(((c >> 5u) & 0x3fu) * 255u / 63u);
        const unsigned char b = static_cast<unsigned char>((c & 0x1fu) * 255u / 31u);
        out.put(static_cast<char>(r));
        out.put(static_cast<char>(g));
        out.put(static_cast<char>(b));
    }
    return static_cast<bool>(out);
}

}  // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("RP2350 Calculator Emulator",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          kWindowW,
                                          kWindowH,
                                          SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kWindowW, kWindowH);
    if (texture == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::array<calc::Color, calc::kLcdWidth * calc::kLcdHeight> lcd{};
    std::vector<calc::Color> canvas(static_cast<std::size_t>(kWindowW * kWindowH));
    calc::Display lcd_display{lcd.data(), calc::kLcdWidth, calc::kLcdHeight};
    calc::Display canvas_display{canvas.data(), kWindowW, kWindowH};
    EmulatorClock clock{std::chrono::steady_clock::now()};
    calc::Platform platform{};
    platform.display = lcd_display;
    platform.storage = {nullptr, storage_read, storage_write};
    platform.clock = {&clock, millis};
    platform.logger = {nullptr, log_message};
    calc::calc_init(platform);

    const std::vector<Button> buttons = make_buttons();
    bool running = true;
    calc::Key held_key = calc::Key::None;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (event.key.keysym.sym == SDLK_F12) {
                    save_screenshot("lcd_screenshot.ppm", lcd.data());
                } else {
                    const calc::Key key = sdl_key(event.key.keysym.sym);
                    if (key != calc::Key::None) {
                        held_key = key;
                        calc::calc_key_down(key);
                    }
                }
            } else if (event.type == SDL_KEYUP) {
                const calc::Key key = sdl_key(event.key.keysym.sym);
                if (key != calc::Key::None) {
                    calc::calc_key_up(key);
                    held_key = calc::Key::None;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                for (const Button& button : buttons) {
                    if (event.button.x >= button.x && event.button.x < button.x + button.w &&
                        event.button.y >= button.y && event.button.y < button.y + button.h) {
                        held_key = button.key;
                        calc::calc_key_down(button.key);
                        break;
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                if (held_key != calc::Key::None) {
                    calc::calc_key_up(held_key);
                    held_key = calc::Key::None;
                }
            }
        }

        calc::calc_tick();
        calc::calc_render();
        calc::clear(canvas_display, calc::rgb565(42, 45, 52));
        blit_lcd(canvas_display, lcd.data());
        calc::draw_rect(canvas_display, kLcdX - 1, kLcdY - 1, calc::kLcdWidth + 2, calc::kLcdHeight + 2, calc::rgb565(8, 9, 11));
        draw_buttons(canvas_display, buttons, held_key);

        SDL_UpdateTexture(texture, nullptr, canvas.data(), kWindowW * static_cast<int>(sizeof(calc::Color)));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
