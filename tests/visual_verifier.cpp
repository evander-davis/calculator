#include "calc/calculator.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr calc::Color kWhite = 0xffff;
constexpr calc::Color kGray = 0xbdf7;
using Frame = std::vector<calc::Color>;

int g_failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::printf("FAIL: %s\n", name);
        ++g_failures;
    }
}

struct TestClock {
    std::uint32_t now;
};

std::uint32_t clock_millis(void* context) {
    return static_cast<TestClock*>(context)->now;
}

struct Harness {
    std::array<calc::Color, calc::kLcdWidth * calc::kLcdHeight> pixels{};
    TestClock clock{0};
    calc::Platform platform{};

    void init() {
        pixels.fill(kWhite);
        platform = {};
        platform.display = {pixels.data(), calc::kLcdWidth, calc::kLcdHeight};
        platform.clock = {&clock, clock_millis};
        clock.now = 0;
        calc::calc_init(platform);
    }

    void press(calc::Key key) {
        calc::calc_key_down(key);
        calc::calc_key_up(key);
    }

    Frame render() {
        calc::calc_render();
        return Frame(pixels.begin(), pixels.end());
    }

    Frame render_cursor_off() {
        clock.now += 500;
        calc::calc_tick();
        return render();
    }

    Frame render_cursor_on() {
        clock.now += 500;
        calc::calc_tick();
        return render();
    }
};

void save_ppm(const std::filesystem::path& path, const Frame& pixels) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "P6\n" << calc::kLcdWidth << " " << calc::kLcdHeight << "\n255\n";
    for (calc::Color pixel : pixels) {
        const unsigned char r = static_cast<unsigned char>(((pixel >> 11u) & 0x1fu) * 255u / 31u);
        const unsigned char g = static_cast<unsigned char>(((pixel >> 5u) & 0x3fu) * 255u / 63u);
        const unsigned char b = static_cast<unsigned char>((pixel & 0x1fu) * 255u / 31u);
        out.put(static_cast<char>(r));
        out.put(static_cast<char>(g));
        out.put(static_cast<char>(b));
    }
}

void write_u16(std::ofstream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 0xffu));
    out.put(static_cast<char>((value >> 8u) & 0xffu));
}

void write_u32(std::ofstream& out, std::uint32_t value) {
    out.put(static_cast<char>(value & 0xffu));
    out.put(static_cast<char>((value >> 8u) & 0xffu));
    out.put(static_cast<char>((value >> 16u) & 0xffu));
    out.put(static_cast<char>((value >> 24u) & 0xffu));
}

void save_bmp(const std::filesystem::path& path, const Frame& pixels) {
    constexpr int row_stride = ((calc::kLcdWidth * 3 + 3) / 4) * 4;
    constexpr std::uint32_t pixel_bytes = row_stride * calc::kLcdHeight;
    constexpr std::uint32_t header_bytes = 14 + 40;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.put('B');
    out.put('M');
    write_u32(out, header_bytes + pixel_bytes);
    write_u16(out, 0);
    write_u16(out, 0);
    write_u32(out, header_bytes);
    write_u32(out, 40);
    write_u32(out, calc::kLcdWidth);
    write_u32(out, calc::kLcdHeight);
    write_u16(out, 1);
    write_u16(out, 24);
    write_u32(out, 0);
    write_u32(out, pixel_bytes);
    write_u32(out, 2835);
    write_u32(out, 2835);
    write_u32(out, 0);
    write_u32(out, 0);
    for (int y = calc::kLcdHeight - 1; y >= 0; --y) {
        int bytes = 0;
        for (int x = 0; x < calc::kLcdWidth; ++x) {
            const calc::Color pixel = pixels[y * calc::kLcdWidth + x];
            const unsigned char r = static_cast<unsigned char>(((pixel >> 11u) & 0x1fu) * 255u / 31u);
            const unsigned char g = static_cast<unsigned char>(((pixel >> 5u) & 0x3fu) * 255u / 63u);
            const unsigned char b = static_cast<unsigned char>((pixel & 0x1fu) * 255u / 31u);
            out.put(static_cast<char>(b));
            out.put(static_cast<char>(g));
            out.put(static_cast<char>(r));
            bytes += 3;
        }
        while (bytes < row_stride) {
            out.put('\0');
            ++bytes;
        }
    }
}

void save_screenshot(const std::filesystem::path& path_without_extension,
                     const Frame& pixels) {
    save_ppm(path_without_extension.string() + ".ppm", pixels);
    save_bmp(path_without_extension.string() + ".bmp", pixels);
}

int changed_pixels(const Frame& a, const Frame& b) {
    int changed = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++changed;
        }
    }
    return changed;
}

int non_white_count(const Frame& pixels) {
    int count = 0;
    for (calc::Color pixel : pixels) {
        if (pixel != kWhite) {
            ++count;
        }
    }
    return count;
}

bool edge_is_clean(const Frame& pixels) {
    for (int y = 0; y < calc::kLcdHeight; ++y) {
        if (pixels[y * calc::kLcdWidth] != kWhite || pixels[y * calc::kLcdWidth + calc::kLcdWidth - 1] != kWhite) {
            return false;
        }
    }
    for (int x = 0; x < calc::kLcdWidth; ++x) {
        if (pixels[x] != kWhite || pixels[(calc::kLcdHeight - 1) * calc::kLcdWidth + x] != kWhite) {
            return false;
        }
    }
    return true;
}

int row_non_white_non_gray(const Frame& pixels, int y) {
    int count = 0;
    if (y < 0 || y >= calc::kLcdHeight) {
        return 0;
    }
    for (int x = 0; x < calc::kLcdWidth; ++x) {
        const calc::Color pixel = pixels[y * calc::kLcdWidth + x];
        if (pixel != kWhite && pixel != kGray) {
            ++count;
        }
    }
    return count;
}

bool separators_have_clearance(const Frame& pixels) {
    int separators = 0;
    for (int y = 1; y < calc::kLcdHeight - 1; ++y) {
        int gray = 0;
        for (int x = 0; x < calc::kLcdWidth; ++x) {
            if (pixels[y * calc::kLcdWidth + x] == kGray) {
                ++gray;
            }
        }
        if (gray >= 20) {
            ++separators;
            if (row_non_white_non_gray(pixels, y - 1) > 6 || row_non_white_non_gray(pixels, y + 1) > 6) {
                return false;
            }
        }
    }
    return separators > 0;
}

bool cursor_blink_visible(Harness& h) {
    const Frame cursor_on = h.render();
    const Frame cursor_off = h.render_cursor_off();
    h.render_cursor_on();
    return changed_pixels(cursor_on, cursor_off) >= 4;
}

void enter_expression(Harness& h, const char* expression) {
    calc::calc_debug_set_home_expression(expression, static_cast<int>(std::strlen(expression)));
    h.press(calc::Key::Enter);
}

void test_layout_measurement() {
    const char* expressions[] = {
        "sqrt(3)",
        "3^2",
        "()/()",
        "(sqrt(3))/(2^3)",
        "((sqrt(1+2))/(3^4))/(5+6)",
        "(1)/((2)/((3)/(sqrt(4^5))))",
        "1234567890+1234567890+1234567890+1234567890+1234567890+1234567890+1234567890+1234567890+1234",
    };
    for (const char* expression : expressions) {
        calc::LayoutDebugInfo info{};
        check(calc::calc_debug_layout_expression(expression, info), "layout debug expression does not overflow");
        check(info.width > 0 && info.height > 0 && info.anchor_count > 0, "layout debug expression has measurable output");
    }

    calc::LayoutDebugInfo top_sqrt{};
    check(calc::calc_debug_layout_expression("sqrt(12)+3", top_sqrt) && top_sqrt.large_text_count >= 3 &&
              top_sqrt.small_text_count == 0 && top_sqrt.sqrt_count == 1,
          "top-level sqrt keeps large font");

    calc::LayoutDebugInfo fraction_sqrt{};
    check(calc::calc_debug_layout_expression("(sqrt(12))/(3)", fraction_sqrt) && fraction_sqrt.small_text_count >= 3 &&
              fraction_sqrt.large_text_count == 0 && fraction_sqrt.fraction_count >= 1 && fraction_sqrt.sqrt_count >= 1,
          "fraction contents use small font including sqrt body");

    calc::LayoutDebugInfo exponent_sqrt{};
    check(calc::calc_debug_layout_expression("2^sqrt(3)", exponent_sqrt) && exponent_sqrt.large_text_count >= 1 &&
              exponent_sqrt.small_text_count >= 1 && exponent_sqrt.sqrt_count >= 1 && exponent_sqrt.superscript_count >= 1,
          "sqrt inside exponent remains small and distinct");

    calc::LayoutDebugInfo fraction_exponent{};
    check(calc::calc_debug_layout_expression("(2^3)/(sqrt(4))", fraction_exponent) && fraction_exponent.fraction_count >= 1 &&
              fraction_exponent.superscript_count >= 1 && fraction_exponent.sqrt_count >= 1 && fraction_exponent.small_text_count >= 3,
          "exponent and sqrt inside fraction remain distinct");
}

void test_cursor_and_navigation(const std::filesystem::path& out_dir) {
    Harness h;
    h.init();
    auto empty_home = h.render();
    save_screenshot(out_dir / "00_empty_home_no_base_placeholder", empty_home);
    check(non_white_count(empty_home) < 120, "empty base-level input has no dotted placeholder box");

    calc::calc_debug_set_home_expression("3^2+5", 2);
    auto exponent_cursor = h.render();
    save_screenshot(out_dir / "01_exponent_cursor", exponent_cursor);
    auto exponent_cursor_off = h.render_cursor_off();
    check(changed_pixels(exponent_cursor, exponent_cursor_off) >= 4, "exponent cursor blink is visible");
    h.render_cursor_on();
    h.press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 3, "right exits non-empty exponent in one press");
    auto after_exponent = h.render();
    save_screenshot(out_dir / "02_exponent_after_right", after_exponent);
    check(changed_pixels(exponent_cursor, after_exponent) > 2, "exponent right arrow has visible effect");
    check(cursor_blink_visible(h), "baseline cursor after exponent blink is visible");
    h.press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 2, "left re-enters exponent in one press");
    check(cursor_blink_visible(h), "exponent cursor after left blink is visible");

    h.init();
    calc::calc_debug_set_home_expression("()/()", 1);
    auto numerator = h.render();
    save_screenshot(out_dir / "03_empty_fraction_numerator", numerator);
    auto numerator_off = h.render_cursor_off();
    check(changed_pixels(numerator, numerator_off) >= 4, "fraction numerator cursor blink is visible");
    h.render_cursor_on();
    h.press(calc::Key::Right);
    check(calc::calc_debug_home_cursor() == 4, "right moves empty numerator to denominator in one press");
    auto denominator = h.render();
    save_screenshot(out_dir / "04_empty_fraction_denominator", denominator);
    check(changed_pixels(numerator, denominator) > 2, "fraction right arrow has visible effect");
    check(cursor_blink_visible(h), "fraction denominator cursor blink is visible");
    h.press(calc::Key::Up);
    check(calc::calc_debug_home_cursor() == 1, "up moves denominator to numerator in one press");
    h.press(calc::Key::Down);
    check(calc::calc_debug_home_cursor() == 4, "down moves numerator to denominator in one press");

    h.init();
    calc::calc_debug_set_home_expression("()/(2)", 1);
    h.press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "delete on blank numerator removes whole fraction and denominator contents");
    auto deleted_num_fraction = h.render();
    save_screenshot(out_dir / "05_delete_blank_numerator_fraction", deleted_num_fraction);

    calc::calc_debug_set_home_expression("(2)/()", 5);
    h.press(calc::Key::Delete);
    check(std::strcmp(calc::calc_debug_home_expression(), "") == 0,
          "delete on blank denominator removes whole fraction and numerator contents");
    auto deleted_den_fraction = h.render();
    save_screenshot(out_dir / "06_delete_blank_denominator_fraction", deleted_den_fraction);

    calc::calc_debug_set_home_expression("2^(3+2)", 3);
    h.press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 1,
          "left from exponent start lands on right side of adjacent base character");
    calc::calc_debug_set_home_expression("2^3+2", 2);
    h.press(calc::Key::Left);
    check(calc::calc_debug_home_cursor() == 1,
          "left from simple exponent start lands on right side of adjacent base character");
}

void test_exponent_editing_sequences(const std::filesystem::path& out_dir) {
    Harness h;
    h.init();
    h.press(calc::Key::Digit2);
    h.press(calc::Key::Power);
    h.press(calc::Key::Digit3);
    h.press(calc::Key::Add);
    h.press(calc::Key::Digit2);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^(3+2)") == 0,
          "operators typed after power remain inside hidden exponent group");
    auto exponent_sum = h.render();
    save_screenshot(out_dir / "07_exponent_operator_sequence", exponent_sum);
    check(cursor_blink_visible(h), "cursor remains visible inside exponent after operator sequence");
    h.press(calc::Key::Right);
    h.press(calc::Key::Add);
    h.press(calc::Key::Digit5);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^(3+2)+5") == 0,
          "right arrow explicitly exits exponent group before baseline insertion");
    auto exponent_exited = h.render();
    save_screenshot(out_dir / "08_exponent_after_explicit_exit", exponent_exited);
    check(changed_pixels(exponent_sum, exponent_exited) > 20, "explicit exponent exit has visible effect");

    h.init();
    h.press(calc::Key::Digit2);
    h.press(calc::Key::Power);
    h.press(calc::Key::LParen);
    h.press(calc::Key::Digit5);
    h.press(calc::Key::Add);
    h.press(calc::Key::Digit5);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^((5+5))") == 0,
          "open parenthesis inside exponent shows auto closing parenthesis");
    auto open_paren = h.render();
    save_screenshot(out_dir / "09_exponent_open_parenthesis_visible", open_paren);
    h.press(calc::Key::RParen);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^((5+5))") == 0,
          "typed closing parenthesis replaces exponent placeholder");
    auto closed_paren = h.render();
    save_screenshot(out_dir / "10_exponent_closed_parenthesis_visible", closed_paren);
    check(changed_pixels(open_paren, closed_paren) > 2, "closing parenthesis has visible exponent-local effect");

    h.init();
    h.press(calc::Key::Digit2);
    h.press(calc::Key::Power);
    h.press(calc::Key::Fraction);
    h.press(calc::Key::Digit2);
    h.press(calc::Key::Down);
    h.press(calc::Key::Digit2);
    check(std::strcmp(calc::calc_debug_home_expression(), "2^((2)/(2))") == 0,
          "fraction typed after power remains inside exponent group");
    auto exponent_fraction = h.render();
    save_screenshot(out_dir / "11_fraction_inside_exponent", exponent_fraction);
    check(non_white_count(exponent_fraction) > 80, "fraction inside exponent renders visible output");

    h.press(calc::Key::Right);
    h.press(calc::Key::Add);
    h.press(calc::Key::Digit1);
    auto exponent_fraction_plus = h.render();
    save_screenshot(out_dir / "12_fraction_inside_exponent_with_plus", exponent_fraction_plus);
    check(changed_pixels(exponent_fraction, exponent_fraction_plus) > 10,
          "fraction inside exponent remains aligned when followed by plus");

    h.init();
    calc::calc_debug_set_home_expression("2^((2)/(3)+1)+(2)/(3)+1", 21);
    auto mixed_fraction_plus = h.render();
    save_screenshot(out_dir / "13_exponent_and_base_fraction_plus_alignment", mixed_fraction_plus);
    check(non_white_count(mixed_fraction_plus) > 180,
          "exponent and base fractions next to plus render visible output");
}

void test_cursor_blink_and_scroll(const std::filesystem::path& out_dir) {
    Harness h;
    h.init();
    const char* long_expression =
        "1234567890+1234567890+1234567890+1234567890+1234567890+1234567890+1234567890+1234567890";
    calc::calc_debug_set_home_expression(long_expression, static_cast<int>(std::strlen(long_expression)));
    auto cursor_on = h.render();
    auto cursor_off = h.render_cursor_off();
    save_screenshot(out_dir / "14_long_expression_cursor_on", cursor_on);
    save_screenshot(out_dir / "15_long_expression_cursor_off", cursor_off);
    check(calc::calc_debug_home_scroll_x() > 0, "long expression scrolls to keep cursor visible");
    check(changed_pixels(cursor_on, cursor_off) >= 6, "cursor blink changes visible pixels");
    check(edge_is_clean(cursor_on), "long expression does not draw on LCD edge pixels");

    const int scroll_before = calc::calc_debug_home_scroll_x();
    for (int i = 0; i < 60; ++i) {
        h.press(calc::Key::Left);
    }
    auto scrolled_left = h.render_cursor_on();
    save_screenshot(out_dir / "16_long_expression_after_left", scrolled_left);
    check(changed_pixels(cursor_on, scrolled_left) > 20, "left arrow across long expression changes visual state");
    const int scroll_after_left = calc::calc_debug_home_scroll_x();
    check(scroll_after_left < scroll_before, "left arrow scrolls long expression back toward the start");
    for (int i = 0; i < 60; ++i) {
        h.press(calc::Key::Right);
    }
    auto scrolled_right = h.render_cursor_on();
    save_screenshot(out_dir / "17_long_expression_after_right", scrolled_right);
    check(changed_pixels(scrolled_left, scrolled_right) > 20, "right arrow across long expression changes visual state");
    check(calc::calc_debug_home_scroll_x() > scroll_after_left, "right arrow scrolls long expression back toward the end");
}

void test_tall_history_and_nested_input(const std::filesystem::path& out_dir) {
    Harness h;
    h.init();
    enter_expression(h, "(sqrt(3))/(2^3)");
    enter_expression(h, "((1)/(2))/(sqrt(3^2))");
    enter_expression(h, "((sqrt(1+2))/(3^4))/(5+6)");
    auto history = h.render();
    save_screenshot(out_dir / "18_tall_history", history);
    check(non_white_count(history) > 500, "tall history renders non-empty output");
    check(edge_is_clean(history), "tall history does not draw on LCD edge pixels");
    check(separators_have_clearance(history), "history separators have whitespace clearance");

    h.press(calc::Key::Up);
    auto first_history_selection = h.render();
    save_screenshot(out_dir / "19_history_selection_latest", first_history_selection);
    h.press(calc::Key::Up);
    auto second_history_selection = h.render();
    save_screenshot(out_dir / "20_history_selection_older", second_history_selection);
    check(changed_pixels(history, first_history_selection) > 20, "up arrow visibly selects latest history row");
    check(changed_pixels(first_history_selection, second_history_selection) > 20, "up arrow visibly moves history selection");
    h.press(calc::Key::Down);
    auto down_history_selection = h.render();
    save_screenshot(out_dir / "21_history_selection_down", down_history_selection);
    check(changed_pixels(second_history_selection, down_history_selection) > 20, "down arrow visibly moves history selection");

    h.init();
    calc::calc_debug_set_home_expression("(1)/((2)/((3)/(sqrt(4^5))))", 30);
    auto nested = h.render();
    save_screenshot(out_dir / "22_deep_nested_input", nested);
    check(non_white_count(nested) > 100, "deep nested input renders non-empty output");
    check(edge_is_clean(nested), "deep nested input does not draw on LCD edge pixels");

    h.init();
    calc::calc_debug_set_home_expression("2^sqrt(3+4)", 6);
    auto sqrt_in_exponent = h.render();
    save_screenshot(out_dir / "23_sqrt_inside_exponent", sqrt_in_exponent);
    check(non_white_count(sqrt_in_exponent) > 100, "sqrt inside exponent renders visible output");
    check(edge_is_clean(sqrt_in_exponent), "sqrt inside exponent does not draw on LCD edge pixels");

    h.init();
    calc::calc_debug_set_home_expression("(2^3)/(sqrt(4+5))", 3);
    auto exponent_in_fraction = h.render();
    save_screenshot(out_dir / "24_exponent_and_sqrt_inside_fraction", exponent_in_fraction);
    check(non_white_count(exponent_in_fraction) > 150, "exponent and sqrt inside fraction render visible output");
    check(edge_is_clean(exponent_in_fraction), "exponent and sqrt inside fraction do not draw on LCD edge pixels");
}

}  // namespace

int main() {
    const std::filesystem::path out_dir = std::filesystem::current_path() / "visual_screenshots";
    std::filesystem::create_directories(out_dir);
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(out_dir)) {
        const std::filesystem::path ext = entry.path().extension();
        if (ext == ".ppm" || ext == ".bmp") {
            std::filesystem::remove(entry.path());
        }
    }

    test_layout_measurement();
    test_cursor_and_navigation(out_dir);
    test_exponent_editing_sequences(out_dir);
    test_cursor_blink_and_scroll(out_dir);
    test_tall_history_and_nested_input(out_dir);

    if (g_failures != 0) {
        std::printf("%d visual verification failure(s)\n", g_failures);
        std::printf("Screenshots written to %s\n", out_dir.string().c_str());
        return 1;
    }
    std::printf("Visual verification passed\n");
    std::printf("Screenshots written to %s\n", out_dir.string().c_str());
    return 0;
}
