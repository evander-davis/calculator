#include "calc/calculator.hpp"

#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include <cstddef>
#include <cstdint>

namespace {

constexpr std::uint8_t kDirtyMagic[4] = {'C', 'D', 'R', 'T'};
constexpr std::uint8_t kEndMagic[4] = {'C', 'E', 'N', 'D'};
constexpr std::uint8_t kKeyPacket = 'K';
constexpr std::uint8_t kRefreshPacket = 'R';

constexpr int kTileWidth = 16;
constexpr int kTileHeight = 16;
constexpr int kTileColumns = calc::kLcdWidth / kTileWidth;
constexpr int kTileRows = calc::kLcdHeight / kTileHeight;
constexpr int kTileCount = kTileColumns * kTileRows;
constexpr int kTilePayloadBytes = kTileWidth * kTileHeight * static_cast<int>(sizeof(calc::Color));
constexpr int kDirtyHeaderBytes = 16;
constexpr int kEndPacketBytes = 20;
constexpr std::uint32_t kRenderIntervalMs = 16;

struct FrameReady {
    std::uint32_t sequence;
    std::uint32_t render_us;
};

calc::Color g_lcd[calc::kLcdWidth * calc::kLcdHeight]{};
std::uint32_t g_sent_tile_hash[kTileCount]{};
std::uint8_t g_tx_packet[kDirtyHeaderBytes + kTilePayloadBytes]{};
alignas(8) std::uint32_t g_calculator_stack[2048]{};

mutex_t g_framebuffer_mutex;
queue_t g_key_queue;
queue_t g_frame_queue;

std::uint32_t g_frame_sequence = 0;
bool g_reference_valid = false;
bool g_force_keyframe = false;
bool g_waiting_for_key = false;

std::uint32_t clock_millis(void*) {
    return to_ms_since_boot(get_absolute_time());
}

void encode_u16(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xffu);
    destination[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void encode_u32(std::uint8_t* destination, std::uint32_t value) {
    destination[0] = static_cast<std::uint8_t>(value & 0xffu);
    destination[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    destination[2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    destination[3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void send_bytes(const std::uint8_t* data, int size) {
    stdio_put_string(reinterpret_cast<const char*>(data), size, false, false);
}

void consume_host_packets() {
    for (;;) {
        const int value = getchar_timeout_us(0);
        if (value < 0) {
            return;
        }

        const std::uint8_t byte = static_cast<std::uint8_t>(value);
        if (g_waiting_for_key) {
            queue_try_add(&g_key_queue, &byte);
            g_waiting_for_key = false;
        } else if (byte == kKeyPacket) {
            g_waiting_for_key = true;
        } else if (byte == kRefreshPacket) {
            g_force_keyframe = true;
        }
    }
}

bool prepare_dirty_tile(std::uint32_t sequence, int x, int y, int width, int height, bool force) {
    int packet_offset = kDirtyHeaderBytes;
    std::uint32_t hash = 2166136261u;

    mutex_enter_blocking(&g_framebuffer_mutex);
    for (int row = 0; row < height; ++row) {
        const int framebuffer_row = (y + row) * calc::kLcdWidth + x;
        for (int column = 0; column < width; ++column) {
            const int index = framebuffer_row + column;
            const calc::Color pixel = g_lcd[index];
            const std::uint8_t low = static_cast<std::uint8_t>(pixel & 0xffu);
            const std::uint8_t high = static_cast<std::uint8_t>((pixel >> 8u) & 0xffu);
            g_tx_packet[packet_offset++] = low;
            g_tx_packet[packet_offset++] = high;
            hash = (hash ^ low) * 16777619u;
            hash = (hash ^ high) * 16777619u;
        }
    }
    const int tile_index = (y / kTileHeight) * kTileColumns + x / kTileWidth;
    const bool changed = force || hash != g_sent_tile_hash[tile_index];
    if (changed) {
        g_sent_tile_hash[tile_index] = hash;
    }
    mutex_exit(&g_framebuffer_mutex);

    if (!changed) {
        return false;
    }

    for (int index = 0; index < 4; ++index) {
        g_tx_packet[index] = kDirtyMagic[index];
    }
    encode_u32(g_tx_packet + 4, sequence);
    encode_u16(g_tx_packet + 8, static_cast<std::uint16_t>(x));
    encode_u16(g_tx_packet + 10, static_cast<std::uint16_t>(y));
    encode_u16(g_tx_packet + 12, static_cast<std::uint16_t>(width));
    encode_u16(g_tx_packet + 14, static_cast<std::uint16_t>(height));
    return true;
}

bool send_dirty_frame(const FrameReady& frame, bool force) {
    std::uint16_t changed_tiles = 0;
    std::uint32_t payload_bytes = 0;

    for (int y = 0; y < calc::kLcdHeight; y += kTileHeight) {
        const int height = y + kTileHeight <= calc::kLcdHeight ? kTileHeight : calc::kLcdHeight - y;
        for (int x = 0; x < calc::kLcdWidth; x += kTileWidth) {
            const int width = x + kTileWidth <= calc::kLcdWidth ? kTileWidth : calc::kLcdWidth - x;
            consume_host_packets();
            if (!stdio_usb_connected()) {
                g_reference_valid = false;
                return false;
            }
            if (!prepare_dirty_tile(frame.sequence, x, y, width, height, force)) {
                continue;
            }

            const int tile_bytes = width * height * static_cast<int>(sizeof(calc::Color));
            send_bytes(g_tx_packet, kDirtyHeaderBytes + tile_bytes);
            ++changed_tiles;
            payload_bytes += static_cast<std::uint32_t>(tile_bytes);
        }
    }

    if (changed_tiles == 0) {
        return true;
    }

    std::uint8_t end_packet[kEndPacketBytes]{};
    for (int index = 0; index < 4; ++index) {
        end_packet[index] = kEndMagic[index];
    }
    encode_u32(end_packet + 4, frame.sequence);
    encode_u16(end_packet + 8, changed_tiles);
    encode_u16(end_packet + 10, force ? 1u : 0u);
    encode_u32(end_packet + 12, payload_bytes);
    encode_u32(end_packet + 16, frame.render_us);
    send_bytes(end_packet, kEndPacketBytes);
    stdio_flush();

    g_reference_valid = true;
    return stdio_usb_connected();
}

void usb_transport_main() {
    bool was_connected = false;
    FrameReady latest_frame{};

    for (;;) {
        const bool connected = stdio_usb_connected();
        if (!connected) {
            was_connected = false;
            g_reference_valid = false;
            g_waiting_for_key = false;
            sleep_ms(1);
            continue;
        }

        if (!was_connected) {
            was_connected = true;
            g_force_keyframe = true;
        }

        consume_host_packets();

        bool have_frame = false;
        FrameReady pending{};
        while (queue_try_remove(&g_frame_queue, &pending)) {
            latest_frame = pending;
            have_frame = true;
        }

        const bool force = g_force_keyframe || !g_reference_valid;
        if (force || have_frame) {
            g_force_keyframe = false;
            if (!send_dirty_frame(latest_frame, force)) {
                was_connected = false;
            }
        } else {
            sleep_ms(1);
        }
    }
}

void process_calculator_keys() {
    std::uint8_t raw_key = 0;
    while (queue_try_remove(&g_key_queue, &raw_key)) {
        if (raw_key == 0 || raw_key > static_cast<std::uint8_t>(calc::Key::ExpPower)) {
            continue;
        }
        const calc::Key key = static_cast<calc::Key>(raw_key);
        calc::calc_key_down(key);
        calc::calc_key_up(key);
    }
}

void render_calculator_frame() {
    const std::uint32_t render_start_us = time_us_32();
    mutex_enter_blocking(&g_framebuffer_mutex);
    calc::calc_render();
    mutex_exit(&g_framebuffer_mutex);

    FrameReady frame{};
    frame.sequence = g_frame_sequence++;
    frame.render_us = time_us_32() - render_start_us;
    queue_try_add(&g_frame_queue, &frame);
}

void calculator_core_main() {
    calc::Platform platform{};
    platform.display = {g_lcd, calc::kLcdWidth, calc::kLcdHeight};
    platform.clock = {nullptr, clock_millis};
    calc::calc_init(platform);

    render_calculator_frame();
    std::uint32_t last_render_ms = clock_millis(nullptr);
    for (;;) {
        process_calculator_keys();
        calc::calc_tick();

        const std::uint32_t now = clock_millis(nullptr);
        if (calc::calc_needs_render() && now - last_render_ms >= kRenderIntervalMs) {
            render_calculator_frame();
            last_render_ms = now;
        }

        sleep_ms(1);
    }
}

}  // namespace

int main() {
    stdio_init_all();
    mutex_init(&g_framebuffer_mutex);
    queue_init(&g_key_queue, sizeof(std::uint8_t), 32);
    queue_init(&g_frame_queue, sizeof(FrameReady), 2);

    multicore_launch_core1_with_stack(calculator_core_main, g_calculator_stack, sizeof(g_calculator_stack));
    usb_transport_main();
}
