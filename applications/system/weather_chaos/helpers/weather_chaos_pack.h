#pragma once

#include "../weather_chaos_app_i.h"

/**
 * Pack a Nexus-TH 36-bit frame: [id 8][batt 1][unused 1][ch 2][sign 1][temp 11][const 4][hum 8].
 * temp is in degrees C, may be negative.
 */
uint64_t
    nexus_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity);

/**
 * Pack an inFactory-TH 40-bit frame:
 * [id 8][crc4 4][unknown 1][batt 1][unused 2][temp 12][hum BCD 8][unused 2][ch 2].
 * temp is in degrees C (protocol stores Fahrenheit*10+900 internally).
 * channel must be the raw 0..3 value (no +1, unlike Nexus-TH/ThermoPRO-TX4 -
 * see ws_protocol_infactory_remote_controller, which stores it unadjusted).
 */
uint64_t infactory_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity);

/**
 * Pack a ThermoPRO-TX4 37-bit frame:
 * [type 4=0x9][id 8][batt 1][btn 1][ch 2][sign 1][temp 11][hum 8][pad 1].
 * temp is in degrees C, may be negative. Button is always reported unpressed.
 */
uint64_t thermopro_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity);

/**
 * Pack a BL999 36-bit frame. Each of the 9 over-the-air nibbles is the
 * bit-reversal of a "logical" nibble; see ws_protocol_bl999_remote_controller
 * for the decode this mirrors. channel is unused - it's embedded in id's low
 * nibble for this protocol (channel is a re-interpretation of those same
 * bits, not a separate field), so re-sending id alone preserves it.
 */
uint64_t
    bl999_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity);

/**
 * Pack a Solight TE44 36-bit frame:
 * [id 8][batt 1][unused 1][ch 2][temp 12][const 4=0xF][crc8 8].
 * temp is in degrees C, may be negative. humidity is unused - this protocol
 * carries no humidity field, just an 8-bit CRC (see
 * ws_protocol_solight_te44_check for the "Rubicson" CRC-8 this mirrors).
 */
uint64_t
    solight_chaos_pack(uint8_t id, uint8_t channel, bool battery_ok, float temp, uint8_t humidity);

/**
 * Pack a Bresser-3CH 40-bit frame:
 * [id 8][batt 1][btn 1][ch 2][temp 12][hum 8][checksum 8].
 * temp is in degrees C (protocol stores Fahrenheit*10+900 internally).
 * channel must be the raw 0..3 value (no +1, unlike Nexus-TH/ThermoPRO-TX4 -
 * see ws_protocol_bresser_3ch_extract_data, which stores it unadjusted).
 * Button is always reported unpressed.
 */
uint64_t bresser_chaos_pack(
    uint8_t id,
    uint8_t channel,
    bool battery_ok,
    float temp,
    uint8_t humidity);
