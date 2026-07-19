#pragma once

#include "../weather_chaos_app_i.h"

/** Whether protocol_name has a working "send fake" encoder wired up. */
bool weather_chaos_protocol_supported(const char* protocol_name);

/**
 * Transmit one already-packed frame (10 repeats) at 433.92MHz OOK using the
 * app's radio device and SubGhz environment/protocol registry. Protocol
 * agnostic: protocol_name must be registered in the app's environment and
 * have a working encoder (see weather_chaos_protocol_supported).
 */
void weather_chaos_transmit(
    WeatherStationApp* app,
    const char* protocol_name,
    uint64_t data,
    uint8_t bit_count);
