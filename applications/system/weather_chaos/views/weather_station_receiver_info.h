#pragma once

#include <gui/view.h>
#include "../helpers/weather_station_types.h"
#include "../helpers/weather_station_event.h"
#include "weather_station_receiver.h"
#include <lib/flipper_format/flipper_format.h>

typedef struct WSReceiverInfo WSReceiverInfo;

void ws_view_receiver_info_update(WSReceiverInfo* ws_receiver_info, FlipperFormat* fff);

/** Update just the displayed Temp/Humidity/Battery (e.g. after sending fake
 * data), leaving the "time since last real signal" timestamp untouched. */
void ws_view_receiver_info_set_reading(
    WSReceiverInfo* ws_receiver_info,
    float temp,
    uint8_t humidity,
    bool battery_low);

void ws_view_receiver_info_set_callback(
    WSReceiverInfo* ws_receiver_info,
    WSReceiverCallback callback,
    void* context);

WSReceiverInfo* ws_view_receiver_info_alloc();

void ws_view_receiver_info_free(WSReceiverInfo* ws_receiver_info);

View* ws_view_receiver_info_get_view(WSReceiverInfo* ws_receiver_info);
