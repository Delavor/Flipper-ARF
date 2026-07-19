#include "../weather_chaos_app_i.h"

// One extra index past the numeric range represents "Random".
#define SETTINGS_TEMP_COUNT (WEATHER_CHAOS_TEMP_MAX - WEATHER_CHAOS_TEMP_MIN + 1 + 1)
#define SETTINGS_TEMP_RANDOM_INDEX (SETTINGS_TEMP_COUNT - 1)
#define SETTINGS_HUMIDITY_COUNT (WEATHER_CHAOS_HUMIDITY_MAX - WEATHER_CHAOS_HUMIDITY_MIN + 1 + 1)
#define SETTINGS_HUMIDITY_RANDOM_INDEX (SETTINGS_HUMIDITY_COUNT - 1)
#define SETTINGS_BATTERY_COUNT 3

static const char* const settings_battery_text[SETTINGS_BATTERY_COUNT] = {
    "OK",
    "Low",
    "Random",
};

static void weather_station_scene_settings_set_temp(VariableItem* item) {
    WeatherStationApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    char buf[16];

    if(index == SETTINGS_TEMP_RANDOM_INDEX) {
        app->chaos_settings.temp_random = true;
        snprintf(buf, sizeof(buf), "Random");
    } else {
        app->chaos_settings.temp_random = false;
        app->chaos_settings.temp = (int8_t)(WEATHER_CHAOS_TEMP_MIN + index);
        snprintf(buf, sizeof(buf), "%d C", app->chaos_settings.temp);
    }
    variable_item_set_current_value_text(item, buf);
    weather_chaos_settings_save(&app->chaos_settings);
}

static void weather_station_scene_settings_set_humidity(VariableItem* item) {
    WeatherStationApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    char buf[16];

    if(index == SETTINGS_HUMIDITY_RANDOM_INDEX) {
        app->chaos_settings.humidity_random = true;
        snprintf(buf, sizeof(buf), "Random");
    } else {
        app->chaos_settings.humidity_random = false;
        app->chaos_settings.humidity = (uint8_t)(WEATHER_CHAOS_HUMIDITY_MIN + index);
        snprintf(buf, sizeof(buf), "%d%%", app->chaos_settings.humidity);
    }
    variable_item_set_current_value_text(item, buf);
    weather_chaos_settings_save(&app->chaos_settings);
}

static void weather_station_scene_settings_set_battery(VariableItem* item) {
    WeatherStationApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    app->chaos_settings.battery = (WeatherChaosBatteryMode)index;
    variable_item_set_current_value_text(item, settings_battery_text[index]);
    weather_chaos_settings_save(&app->chaos_settings);
}

void weather_station_scene_settings_on_enter(void* context) {
    WeatherStationApp* app = context;
    VariableItem* item;
    char buf[16];

    item = variable_item_list_add(
        app->variable_item_list,
        "Temp:",
        SETTINGS_TEMP_COUNT,
        weather_station_scene_settings_set_temp,
        app);
    if(app->chaos_settings.temp_random) {
        variable_item_set_current_value_index(item, SETTINGS_TEMP_RANDOM_INDEX);
        snprintf(buf, sizeof(buf), "Random");
    } else {
        variable_item_set_current_value_index(
            item, (uint8_t)(app->chaos_settings.temp - WEATHER_CHAOS_TEMP_MIN));
        snprintf(buf, sizeof(buf), "%d C", app->chaos_settings.temp);
    }
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(
        app->variable_item_list,
        "Humidity:",
        SETTINGS_HUMIDITY_COUNT,
        weather_station_scene_settings_set_humidity,
        app);
    if(app->chaos_settings.humidity_random) {
        variable_item_set_current_value_index(item, SETTINGS_HUMIDITY_RANDOM_INDEX);
        snprintf(buf, sizeof(buf), "Random");
    } else {
        variable_item_set_current_value_index(
            item, (uint8_t)(app->chaos_settings.humidity - WEATHER_CHAOS_HUMIDITY_MIN));
        snprintf(buf, sizeof(buf), "%d%%", app->chaos_settings.humidity);
    }
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(
        app->variable_item_list,
        "Battery:",
        SETTINGS_BATTERY_COUNT,
        weather_station_scene_settings_set_battery,
        app);
    variable_item_set_current_value_index(item, (uint8_t)app->chaos_settings.battery);
    variable_item_set_current_value_text(item, settings_battery_text[app->chaos_settings.battery]);

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewVariableItemList);
}

bool weather_station_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void weather_station_scene_settings_on_exit(void* context) {
    WeatherStationApp* app = context;
    variable_item_list_set_selected_item(app->variable_item_list, 0);
    variable_item_list_reset(app->variable_item_list);
}
