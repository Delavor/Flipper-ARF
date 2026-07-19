#include "weather_chaos_settings.h"
#include <furi.h>
#include <furi_hal_random.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define WEATHER_CHAOS_SETTINGS_DIR     EXT_PATH("apps_data/weather_chaos")
#define WEATHER_CHAOS_SETTINGS_FILE    EXT_PATH("apps_data/weather_chaos/settings.txt")
#define WEATHER_CHAOS_SETTINGS_HEADER  "Weather Chaos Settings"
#define WEATHER_CHAOS_SETTINGS_VERSION 1

void weather_chaos_settings_load(WeatherChaosSettings* settings) {
    settings->temp = 20;
    settings->temp_random = false;
    settings->humidity = 70;
    settings->humidity_random = false;
    settings->battery = WeatherChaosBatteryOk;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_existing(ff, WEATHER_CHAOS_SETTINGS_FILE)) break;

        FuriString* header = furi_string_alloc();
        uint32_t version = 0;
        bool header_ok = flipper_format_read_header(ff, header, &version);
        furi_string_free(header);
        if(!header_ok) break;

        int32_t temp = settings->temp;
        if(flipper_format_read_int32(ff, "Temp", &temp, 1)) {
            settings->temp = (int8_t)temp;
        }
        flipper_format_read_bool(ff, "TempRandom", &settings->temp_random, 1);

        uint32_t humidity = settings->humidity;
        if(flipper_format_read_uint32(ff, "Humidity", &humidity, 1)) {
            settings->humidity = (uint8_t)humidity;
        }
        flipper_format_read_bool(ff, "HumidityRandom", &settings->humidity_random, 1);

        uint32_t battery = settings->battery;
        if(flipper_format_read_uint32(ff, "Battery", &battery, 1) &&
           battery <= WeatherChaosBatteryRandom) {
            settings->battery = (WeatherChaosBatteryMode)battery;
        }
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

void weather_chaos_settings_save(const WeatherChaosSettings* settings) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, WEATHER_CHAOS_SETTINGS_DIR);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        if(!flipper_format_file_open_always(ff, WEATHER_CHAOS_SETTINGS_FILE)) break;
        if(!flipper_format_write_header_cstr(
               ff, WEATHER_CHAOS_SETTINGS_HEADER, WEATHER_CHAOS_SETTINGS_VERSION))
            break;

        int32_t temp = settings->temp;
        flipper_format_write_int32(ff, "Temp", &temp, 1);
        flipper_format_write_bool(ff, "TempRandom", &settings->temp_random, 1);

        uint32_t humidity = settings->humidity;
        flipper_format_write_uint32(ff, "Humidity", &humidity, 1);
        flipper_format_write_bool(ff, "HumidityRandom", &settings->humidity_random, 1);

        uint32_t battery = settings->battery;
        flipper_format_write_uint32(ff, "Battery", &battery, 1);
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

void weather_chaos_settings_resolve(
    const WeatherChaosSettings* settings,
    float* out_temp,
    uint8_t* out_humidity,
    bool* out_battery_ok) {
    if(settings->temp_random) {
        int32_t span = WEATHER_CHAOS_TEMP_MAX - WEATHER_CHAOS_TEMP_MIN + 1;
        *out_temp = (float)(WEATHER_CHAOS_TEMP_MIN + (int32_t)(furi_hal_random_get() % span));
    } else {
        *out_temp = (float)settings->temp;
    }

    if(settings->humidity_random) {
        uint32_t span = WEATHER_CHAOS_HUMIDITY_MAX - WEATHER_CHAOS_HUMIDITY_MIN + 1;
        *out_humidity = (uint8_t)(WEATHER_CHAOS_HUMIDITY_MIN + (furi_hal_random_get() % span));
    } else {
        *out_humidity = settings->humidity;
    }

    switch(settings->battery) {
    case WeatherChaosBatteryLow:
        *out_battery_ok = false;
        break;
    case WeatherChaosBatteryRandom:
        *out_battery_ok = (furi_hal_random_get() & 1) != 0;
        break;
    case WeatherChaosBatteryOk:
    default:
        *out_battery_ok = true;
        break;
    }
}
