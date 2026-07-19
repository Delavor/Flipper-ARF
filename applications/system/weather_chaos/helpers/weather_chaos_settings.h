#pragma once

#include <stdint.h>
#include <stdbool.h>

// Protocol-agnostic chaos settings: whichever protocol we're spoofing reads
// from here to build its own frame.
#define WEATHER_CHAOS_TEMP_MIN     (-50)
#define WEATHER_CHAOS_TEMP_MAX     50
#define WEATHER_CHAOS_HUMIDITY_MIN 0
#define WEATHER_CHAOS_HUMIDITY_MAX 150

typedef enum {
    WeatherChaosBatteryOk,
    WeatherChaosBatteryLow,
    WeatherChaosBatteryRandom,
} WeatherChaosBatteryMode;

typedef struct {
    int8_t temp; // WEATHER_CHAOS_TEMP_MIN..WEATHER_CHAOS_TEMP_MAX, ignored if temp_random
    bool temp_random;
    uint8_t humidity; // WEATHER_CHAOS_HUMIDITY_MIN..WEATHER_CHAOS_HUMIDITY_MAX, ignored if humidity_random
    bool humidity_random;
    WeatherChaosBatteryMode battery;
} WeatherChaosSettings;

/** Load settings from SD card, falling back to defaults (20C, 70%, Battery OK) if missing. */
void weather_chaos_settings_load(WeatherChaosSettings* settings);

/** Save settings to SD card. */
void weather_chaos_settings_save(const WeatherChaosSettings* settings);

/** Resolve the configured temp/humidity/battery to concrete values to send,
 * rolling fresh random numbers/state for whichever fields are set to Random. */
void weather_chaos_settings_resolve(
    const WeatherChaosSettings* settings,
    float* out_temp,
    uint8_t* out_humidity,
    bool* out_battery_ok);
