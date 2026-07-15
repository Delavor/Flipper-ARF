#pragma once

#include "definitions.h"
#include "register.h"

class Gameboy;

class Timer {
public:
    Timer(Gameboy& inGb)
        : gb(inGb) {}

    /* Called once per emulated instruction: defined inline at the bottom
     * of gameboy.h (needs the complete Gameboy type). */
    void tick(uint cycles);

    auto get_divider() const -> u8 { return divider.value(); }
    auto get_timer() const -> u8 { return timer_counter.value(); }
    auto get_timer_modulo() const -> u8 { return timer_modulo.value(); }
    /* Only the bottom three bits of this register are usable */
    auto get_timer_control() const -> u8 { return timer_control.value() & 0x7; }

    void reset_divider() { divider.set(0x0); }
    void set_timer(u8 value) { timer_counter.set(value); }
    void set_timer_modulo(u8 value) { timer_modulo.set(value); }
    void set_timer_control(u8 value) { timer_control.set(value); }

private:
    auto clocks_needed_to_increment() const -> uint {
        switch(get_timer_control() & 0x3) {
        case 0: return CLOCK_RATE / 4096;
        case 1: return CLOCK_RATE / 262144;
        case 2: return CLOCK_RATE / 65536;
        default: return CLOCK_RATE / 16384;
        }
    }

    uint clocks = 0;
    uint div_clocks = 0;

    Gameboy& gb;

    ByteRegister divider;
    ByteRegister timer_counter;

    ByteRegister timer_modulo;
    ByteRegister timer_control;
};
