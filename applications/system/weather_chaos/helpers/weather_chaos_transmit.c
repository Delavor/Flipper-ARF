#include "weather_chaos_transmit.h"
#include "../protocols/nexus_th.h"
#include "../protocols/infactory.h"
#include "../protocols/thermopro_tx4.h"
#include "../protocols/bl999.h"
#include "../protocols/solight_te44.h"
#include "../protocols/bresser_3ch.h"
#include <flipper_format/flipper_format_i.h>

#define TAG "WeatherChaosTransmit"

// Fixed 433.92MHz OOK - the frequency/modulation almost every cheap 433MHz
// weather sensor these protocols cover uses.
#define WEATHER_CHAOS_FREQUENCY 433920000
#define WEATHER_CHAOS_REPEAT    10

static const char* const weather_chaos_supported_protocols[] = {
    WS_PROTOCOL_NEXUS_TH_NAME,
    WS_PROTOCOL_INFACTORY_NAME,
    WS_PROTOCOL_THERMOPRO_TX4_NAME,
    WS_PROTOCOL_BL999_NAME,
    WS_PROTOCOL_SOLIGHT_TE44_NAME,
    WS_PROTOCOL_BRESSER_3CH_NAME,
};

bool weather_chaos_protocol_supported(const char* protocol_name) {
    for(size_t i = 0; i < COUNT_OF(weather_chaos_supported_protocols); i++) {
        if(strcmp(protocol_name, weather_chaos_supported_protocols[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void weather_chaos_build_flipper_format(
    FlipperFormat* ff,
    const char* protocol_name,
    uint64_t data,
    uint8_t bit_count,
    uint32_t repeat) {
    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (data >> (i * 8)) & 0xFF;
    }
    FuriString* text = furi_string_alloc_printf(
        "Protocol: %s\n"
        "Bit: %d\n"
        "Key: %02X %02X %02X %02X %02X %02X %02X %02X\n"
        "Repeat: %lu\n",
        protocol_name,
        bit_count,
        key_data[0],
        key_data[1],
        key_data[2],
        key_data[3],
        key_data[4],
        key_data[5],
        key_data[6],
        key_data[7],
        repeat);
    Stream* stream = flipper_format_get_raw_stream(ff);
    stream_clean(stream);
    stream_write_cstring(stream, furi_string_get_cstr(text));
    stream_seek(stream, 0, StreamOffsetFromStart);
    furi_string_free(text);
}

void weather_chaos_transmit(
    WeatherStationApp* app,
    const char* protocol_name,
    uint64_t data,
    uint8_t bit_count) {
    // Background scanning may still be running (e.g. right after viewing a
    // history entry, or after backing out of the receiver without it being
    // stopped) - TX can't start while the device is mid-RX, or the subghz
    // device layer hits an internal furi_check.
    bool was_rx = app->txrx->txrx_state == WSTxRxStateRx;
    uint32_t rx_frequency = app->txrx->preset->frequency;
    if(was_rx) {
        ws_rx_end(app);
    }

    FlipperFormat* flipper_format = flipper_format_string_alloc();
    weather_chaos_build_flipper_format(
        flipper_format, protocol_name, data, bit_count, WEATHER_CHAOS_REPEAT);

    SubGhzTransmitter* transmitter =
        subghz_transmitter_alloc_init(app->txrx->environment, protocol_name);
    if(!transmitter) {
        FURI_LOG_E(TAG, "Failed to allocate %s transmitter", protocol_name);
        flipper_format_free(flipper_format);
        return;
    }

    if(subghz_transmitter_deserialize(transmitter, flipper_format) == SubGhzProtocolStatusOk) {
        subghz_devices_reset(app->txrx->radio_device);
        subghz_devices_idle(app->txrx->radio_device);
        subghz_devices_load_preset(
            app->txrx->radio_device, FuriHalSubGhzPresetOok650Async, NULL);
        subghz_devices_set_frequency(app->txrx->radio_device, WEATHER_CHAOS_FREQUENCY);

        furi_hal_power_suppress_charge_enter();
        if(subghz_devices_start_async_tx(
               app->txrx->radio_device, subghz_transmitter_yield, transmitter)) {
            while(!subghz_devices_is_async_complete_tx(app->txrx->radio_device)) {
                furi_delay_ms(20);
            }
            subghz_devices_stop_async_tx(app->txrx->radio_device);
        }
        subghz_devices_idle(app->txrx->radio_device);
        furi_hal_power_suppress_charge_exit();
    } else {
        FURI_LOG_E(TAG, "Failed to deserialize/build %s upload", protocol_name);
    }

    subghz_transmitter_free(transmitter);
    flipper_format_free(flipper_format);

    if(was_rx) {
        ws_rx(app, rx_frequency);
    }
}
