#include "weather_chaos_pack.h"
#include <locale/locale.h>
#include <lib/subghz/blocks/math.h>

#define THERMOPRO_TX4_TYPE 0x9

uint64_t
    nexus_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity) {
    // Temp is an 11-bit magnitude plus a separate sign bit (23), but negative
    // magnitudes are stored two's-complement style: raw = 0x800 - magnitude
    // (see ws_protocol_nexus_th_remote_controller's decode for the inverse).
    int16_t temp_tenths = (int16_t)(temp * 10.0f);
    bool negative = temp_tenths < 0;
    uint16_t raw11 = negative ? (uint16_t)(0x800 - (-temp_tenths)) & 0x7FF :
                                 (uint16_t)temp_tenths & 0x7FF;

    uint64_t data = 0;
    data |= ((uint64_t)id & 0xFF) << 28;
    data |= ((uint64_t)(battery_ok ? 1 : 0) & 0x1) << 27;
    data |= ((uint64_t)(channel - 1) & 0x3) << 24;
    data |= ((uint64_t)(negative ? 1 : 0) & 0x1) << 23;
    data |= ((uint64_t)raw11 & 0x7FF) << 12;
    data |= (uint64_t)0xF << 8; // Nexus-TH const nibble
    data |= (uint64_t)humidity & 0xFF;
    return data;
}

uint64_t infactory_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity) {
    float temp_f = locale_celsius_to_fahrenheit(temp);
    uint16_t temp_raw = (uint16_t)((temp_f * 10.0f) + 900.0f) & 0x0FFF;

    uint8_t hum_tens = (humidity / 10) & 0x0F;
    uint8_t hum_units = (humidity % 10) & 0x0F;

    uint64_t data = 0;
    data |= (uint64_t)id << 32;
    data |= ((uint64_t)(battery_ok ? 0 : 1) & 0x1) << 26; // bit set = low battery
    data |= (uint64_t)temp_raw << 12;
    data |= (uint64_t)hum_tens << 8;
    data |= (uint64_t)hum_units << 4;
    data |= (uint64_t)channel & 0x03;

    // Mirrors ws_protocol_infactory_check_crc exactly (CRC-4/ITU over a
    // rearranged 5-byte view of the frame - see that function for why msg[1]
    // combines bits from two different places).
    uint8_t msg[5] = {
        (uint8_t)(data >> 32),
        (uint8_t)(((data >> 24) & 0x0F) | ((data & 0x0F) << 4)),
        (uint8_t)(data >> 16),
        (uint8_t)(data >> 8),
        (uint8_t)data};
    uint8_t crc = subghz_protocol_blocks_crc4(msg, 4, 0x13, 0);
    crc ^= msg[4] >> 4;
    data |= (uint64_t)(crc & 0x0F) << 28;

    return data;
}

uint64_t thermopro_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity) {
    // Same sign-bit + two's-complement-magnitude temp encoding as Nexus-TH,
    // just at a different bit offset (see ws_protocol_thermopro_tx4_remote_controller).
    int16_t temp_tenths = (int16_t)(temp * 10.0f);
    bool negative = temp_tenths < 0;
    uint16_t raw11 = negative ? (uint16_t)(0x800 - (-temp_tenths)) & 0x7FF :
                                 (uint16_t)temp_tenths & 0x7FF;

    uint64_t data = 0;
    data |= (uint64_t)THERMOPRO_TX4_TYPE << 33;
    data |= ((uint64_t)id & 0xFF) << 25;
    data |= ((uint64_t)(battery_ok ? 0 : 1) & 0x1) << 24; // bit set = low battery
    data |= ((uint64_t)(channel - 1) & 0x3) << 21;
    data |= ((uint64_t)(negative ? 1 : 0) & 0x1) << 20;
    data |= ((uint64_t)raw11 & 0x7FF) << 9;
    data |= ((uint64_t)humidity & 0xFF) << 1;
    return data;
}

static uint8_t bl999_reverse_nibble(uint8_t n) {
    n = ((n & 0xC) >> 2) | ((n & 0x3) << 2);
    n = ((n & 0xA) >> 1) | ((n & 0x5) << 1);
    return n & 0xF;
}

uint64_t
    bl999_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity) {
    UNUSED(channel); // embedded in id's low nibble for this protocol - see header.

    uint8_t logical_nib[9] = {0};
    logical_nib[0] = (id >> 4) & 0xF;
    logical_nib[1] = id & 0xF;
    logical_nib[2] = battery_ok ? 0x0 : 0x1;

    // 12-bit two's complement, tenths of a degree.
    int16_t temp_tenths = (int16_t)(temp * 10.0f);
    uint16_t raw12 = (uint16_t)temp_tenths & 0xFFF;
    logical_nib[3] = raw12 & 0xF;
    logical_nib[4] = (raw12 >> 4) & 0xF;
    logical_nib[5] = (raw12 >> 8) & 0xF;

    // Decode does hum = 100 - (int8_t)hum_raw regardless of sign, so the
    // inverse is just an int8 wrap of (100 - humidity).
    uint8_t hum_raw = (uint8_t)(100 - (int)humidity);
    logical_nib[6] = hum_raw & 0xF;
    logical_nib[7] = (hum_raw >> 4) & 0xF;

    int sum = 0;
    for(uint8_t i = 0; i < 8; i++) {
        sum += logical_nib[i];
    }
    logical_nib[8] = sum & 0xF;

    uint64_t data = 0;
    for(uint8_t i = 0; i < 9; i++) {
        uint8_t raw_nibble = bl999_reverse_nibble(logical_nib[i]);
        data |= (uint64_t)raw_nibble << ((8 - i) * 4);
    }
    return data;
}

uint64_t
    solight_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity) {
    UNUSED(humidity); // no humidity field in this protocol - see header.

    int16_t temp_tenths = (int16_t)(temp * 10.0f);
    uint16_t raw12 = (uint16_t)temp_tenths & 0x0FFF;

    uint64_t data = 0;
    data |= (uint64_t)id << 28;
    data |= ((uint64_t)(battery_ok ? 1 : 0) & 0x1) << 27;
    data |= ((uint64_t)(channel - 1) & 0x3) << 24;
    data |= (uint64_t)raw12 << 12;
    data |= (uint64_t)0xF << 8;

    // Mirrors ws_protocol_solight_te44_check: CRC-8 over 5 bytes with the
    // checksum's own position zeroed produces the value that must be placed
    // there so a re-check over the full 5 bytes comes out to 0.
    uint8_t msg[5] = {
        (uint8_t)(data >> 28),
        (uint8_t)(data >> 20),
        (uint8_t)(data >> 12),
        0xF0,
        0x00,
    };
    uint8_t crc = subghz_protocol_blocks_crc8(msg, 5, 0x31, 0x6c);
    data |= (uint64_t)crc;

    return data;
}

uint64_t bresser_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity) {
    float temp_f = locale_celsius_to_fahrenheit(temp);
    uint16_t temp_raw = (uint16_t)((temp_f * 10.0f) + 900.0f) & 0x0FFF;

    uint64_t data = 0;
    data |= (uint64_t)id << 32;
    data |= ((uint64_t)(battery_ok ? 0 : 1) & 0x1) << 31; // bit set = low battery
    data |= (uint64_t)channel << 28;
    data |= (uint64_t)temp_raw << 16;
    data |= (uint64_t)humidity << 8;

    // Mirrors ws_protocol_bresser_3ch_check: checksum is the sum of the
    // other 4 bytes, mod 256.
    uint8_t sum = (uint8_t)(
        ((data >> 32) & 0xFF) + ((data >> 24) & 0xFF) + ((data >> 16) & 0xFF) +
        ((data >> 8) & 0xFF));
    data |= (uint64_t)sum;

    return data;
}
