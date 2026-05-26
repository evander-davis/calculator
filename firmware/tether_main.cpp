#include "calc/calculator.hpp"

#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include <cstdint>

namespace {

constexpr std::uint8_t kMagic0 = 'C';
constexpr std::uint8_t kMagic1 = 'F';
constexpr std::uint8_t kMagic2 = 'R';
constexpr std::uint8_t kMagic3 = 'M';
constexpr std::uint8_t kKeyPacket = 'K';

calc::Color g_lcd[calc::kLcdWidth * calc::kLcdHeight]{};
std::uint32_t g_frame_seq = 0;

std::uint32_t clock_millis(void*) {
    return to_ms_since_boot(get_absolute_time());
}

void write_u8(std::uint8_t value) {
    putchar_raw(static_cast<int>(value));
}

void write_u16(std::uint16_t value) {
    write_u8(static_cast<std::uint8_t>(value & 0xffu));
    write_u8(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void write_u32(std::uint32_t value) {
    write_u8(static_cast<std::uint8_t>(value & 0xffu));
    write_u8(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    write_u8(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    write_u8(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void send_frame() {
    if (!stdio_usb_connected()) {
        return;
    }

    write_u8(kMagic0);
    write_u8(kMagic1);
    write_u8(kMagic2);
    write_u8(kMagic3);
    write_u32(g_frame_seq++);
    write_u16(static_cast<std::uint16_t>(calc::kLcdWidth));
    write_u16(static_cast<std::uint16_t>(calc::kLcdHeight));

    for (int i = 0; i < calc::kLcdWidth * calc::kLcdHeight; ++i) {
        write_u16(g_lcd[i]);
    }
    stdio_flush();
}

void consume_host_packets() {
    for (;;) {
        const int first = getchar_timeout_us(0);
        if (first < 0) {
            return;
        }
        if (first == kKeyPacket) {
            const int key = getchar_timeout_us(0);
            if (key >= 0) {
                calc::Key calc_key = static_cast<calc::Key>(static_cast<std::uint8_t>(key));
                calc::calc_key_down(calc_key);
                calc::calc_key_up(calc_key);
            }
        }
    }
}

}  // namespace

int main() {
    stdio_init_all();

    calc::Platform platform{};
    platform.display = {g_lcd, calc::kLcdWidth, calc::kLcdHeight};
    platform.clock = {nullptr, clock_millis};
    calc::calc_init(platform);

    std::uint32_t last_frame_ms = 0;

    for (;;) {
        consume_host_packets();
        calc::calc_tick();
        calc::calc_render();

        const std::uint32_t now = clock_millis(nullptr);
        if (now - last_frame_ms >= 250u) {
            send_frame();
            last_frame_ms = now;
        }

        sleep_ms(5);
    }
}
