#include "../weather_chaos_app_i.h"
#include "../views/weather_station_receiver.h"
#include "../protocols/nexus_th.h"
#include "../protocols/infactory.h"
#include "../protocols/thermopro_tx4.h"
#include "../protocols/bl999.h"
#include "../protocols/solight_te44.h"
#include "../protocols/bresser_3ch.h"
#include "../protocols/ws_generic.h"
#include "../helpers/weather_chaos_pack.h"
#include "../helpers/weather_chaos_transmit.h"

void weather_station_scene_receiver_info_callback(WSCustomEvent event, void* context) {
    furi_assert(context);
    WeatherStationApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

// Pack this protocol's frame from the captured entry's id/channel (so a base
// station that already learned that sensor keeps accepting it) plus the
// given Temp/Hum/Battery. NULL if this protocol has no encoder.
static uint64_t weather_station_scene_receiver_info_pack(
    const char* protocol_name,
    const WSBlockGeneric* generic,
    float temp,
    uint8_t humidity,
    bool battery_ok,
    uint8_t* out_bit_count,
    bool* out_ok) {
    *out_ok = true;
    if(strcmp(protocol_name, WS_PROTOCOL_NEXUS_TH_NAME) == 0) {
        *out_bit_count = 36;
        return nexus_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    } else if(strcmp(protocol_name, WS_PROTOCOL_INFACTORY_NAME) == 0) {
        *out_bit_count = 40;
        return infactory_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    } else if(strcmp(protocol_name, WS_PROTOCOL_THERMOPRO_TX4_NAME) == 0) {
        *out_bit_count = 37;
        return thermopro_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    } else if(strcmp(protocol_name, WS_PROTOCOL_BL999_NAME) == 0) {
        *out_bit_count = 36;
        return bl999_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    } else if(strcmp(protocol_name, WS_PROTOCOL_SOLIGHT_TE44_NAME) == 0) {
        *out_bit_count = 36;
        return solight_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    } else if(strcmp(protocol_name, WS_PROTOCOL_BRESSER_3CH_NAME) == 0) {
        *out_bit_count = 40;
        return bresser_chaos_pack(
            (uint8_t)generic->id, generic->channel, battery_ok, temp, humidity);
    }
    *out_ok = false;
    return 0;
}

// Re-send this captured entry's sensor as itself (same id/channel, so a base
// station that already learned it keeps accepting it) with Temp/Hum/Battery
// taken from the configured chaos settings.
static void weather_station_scene_receiver_info_send_fake(WeatherStationApp* app) {
    FlipperFormat* fff = ws_history_get_raw_data(app->txrx->history, app->txrx->idx_menu_chosen);
    if(!fff) return;

    flipper_format_rewind(fff);
    FuriString* protocol_name = furi_string_alloc();
    bool have_protocol = flipper_format_read_string(fff, "Protocol", protocol_name);
    bool supported = have_protocol &&
                      weather_chaos_protocol_supported(furi_string_get_cstr(protocol_name));

    if(!supported) {
        furi_string_free(protocol_name);
        notification_message(app->notifications, &sequence_error);
        return;
    }

    WSBlockGeneric generic = {0};
    if(ws_block_generic_deserialize(&generic, fff) != SubGhzProtocolStatusOk) {
        furi_string_free(protocol_name);
        notification_message(app->notifications, &sequence_error);
        return;
    }

    float temp;
    uint8_t humidity;
    bool battery_ok;
    weather_chaos_settings_resolve(&app->chaos_settings, &temp, &humidity, &battery_ok);

    uint8_t bit_count = 0;
    bool packed_ok = false;
    uint64_t data = weather_station_scene_receiver_info_pack(
        furi_string_get_cstr(protocol_name),
        &generic,
        temp,
        humidity,
        battery_ok,
        &bit_count,
        &packed_ok);

    if(packed_ok) {
        weather_chaos_transmit(app, furi_string_get_cstr(protocol_name), data, bit_count);
        ws_view_receiver_info_set_reading(app->ws_receiver_info, temp, humidity, !battery_ok);
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_error);
    }

    furi_string_free(protocol_name);
}

static void weather_station_scene_receiver_info_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    WeatherStationApp* app = context;

    if(ws_history_add_to_history(app->txrx->history, decoder_base, app->txrx->preset) ==
       WSHistoryStateAddKeyUpdateData) {
        ws_view_receiver_info_update(
            app->ws_receiver_info,
            ws_history_get_raw_data(app->txrx->history, app->txrx->idx_menu_chosen));
        subghz_receiver_reset(receiver);

        notification_message(app->notifications, &sequence_blink_green_10);
        app->txrx->rx_key_state = WSRxKeyStateAddKey;
    }
}

void weather_station_scene_receiver_info_on_enter(void* context) {
    WeatherStationApp* app = context;

    subghz_receiver_set_rx_callback(
        app->txrx->receiver, weather_station_scene_receiver_info_add_to_history_callback, app);
    ws_view_receiver_info_set_callback(
        app->ws_receiver_info, weather_station_scene_receiver_info_callback, app);
    ws_view_receiver_info_update(
        app->ws_receiver_info,
        ws_history_get_raw_data(app->txrx->history, app->txrx->idx_menu_chosen));
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewReceiverInfo);
}

bool weather_station_scene_receiver_info_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WSCustomEventViewReceiverInfoSendFake) {
            weather_station_scene_receiver_info_send_fake(app);
            consumed = true;
        }
    }

    return consumed;
}

void weather_station_scene_receiver_info_on_exit(void* context) {
    UNUSED(context);
}
