#include <application.h>

#define SECONDS 1000
#define MINUTES (60 * SECONDS)

#ifndef DEBUG

// Release configuration

#define UPDATE_INTERVAL_BATTERY (5 * MINUTES)
#define UPDATE_INTERVAL_THERMOMETER (1 * MINUTES)
#define UPDATE_INTERVAL_ACCELEROMETER (15 * SECONDS)

#define DOOR_TIMEOUT (10 * MINUTES)

#define SAMPLE_COUNT_VOLTAGE 12
#define SAMPLE_COUNT_TEMPERATURE 10
#define SAMPLE_COUNT_ACCELERATION 8

#else

// Debug configuration

#define UPDATE_INTERVAL_BATTERY (10 * SECONDS)
#define UPDATE_INTERVAL_THERMOMETER (10 * SECONDS)
#define UPDATE_INTERVAL_ACCELEROMETER (2 * SECONDS)

#define DOOR_TIMEOUT (10 * SECONDS)

#define SAMPLE_COUNT_VOLTAGE 6
#define SAMPLE_COUNT_TEMPERATURE 6
#define SAMPLE_COUNT_ACCELERATION 5

#endif

// LED instance
twr_led_t led;

// Button instance
twr_button_t button;

// Thermometer instance
twr_tmp112_t tmp112;

// Accelerometer instance
twr_lis2dh12_t lis2dh12;

// Dice instance
twr_dice_t dice;

// Sigfox Module instance
twr_module_sigfox_t sigfox;

// Data stream instances
twr_data_stream_t stream_v;
twr_data_stream_t stream_t;
twr_data_stream_t stream_x;
twr_data_stream_t stream_y;
twr_data_stream_t stream_z;

// Data stream buffer instances
TWR_DATA_STREAM_FLOAT_BUFFER(stream_buffer_v, SAMPLE_COUNT_VOLTAGE)
TWR_DATA_STREAM_FLOAT_BUFFER(stream_buffer_t, SAMPLE_COUNT_TEMPERATURE)
TWR_DATA_STREAM_FLOAT_BUFFER(stream_buffer_x, SAMPLE_COUNT_ACCELERATION)
TWR_DATA_STREAM_FLOAT_BUFFER(stream_buffer_y, SAMPLE_COUNT_ACCELERATION)
TWR_DATA_STREAM_FLOAT_BUFFER(stream_buffer_z, SAMPLE_COUNT_ACCELERATION)

twr_tick_t report;

// This function dispatches button events
void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_PRESS)
    {
        twr_log_info("APP: Button press");

        // Trigger immediate state transmission
        report = twr_tick_get();
    }
}

// This function dispatches thermometer events
void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        twr_log_debug("APP: Thermometer update event");

        float temperature;

        if (twr_tmp112_get_temperature_celsius(&tmp112, &temperature))
        {
            twr_log_debug("APP: Temperature = %.2f C", temperature);

            twr_data_stream_feed(&stream_t, &temperature);
        }
        else
        {
            twr_data_stream_reset(&stream_t);
        }
    }
    else if (event == TWR_TMP112_EVENT_ERROR)
    {
        twr_log_error("APP: Thermometer error event");

        twr_data_stream_reset(&stream_t);
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(twr_lis2dh12_t *self, twr_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == TWR_LIS2DH12_EVENT_UPDATE)
    {
        twr_log_debug("APP: Accelerometer update event");

        twr_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (twr_lis2dh12_get_result_g(self, &result))
        {
            twr_log_debug("APP: Acceleration = [%.2f, %.2f, %.2f] g", result.x_axis, result.y_axis, result.z_axis);

            twr_data_stream_feed(&stream_x, &result.x_axis);
            twr_data_stream_feed(&stream_y, &result.y_axis);
            twr_data_stream_feed(&stream_z, &result.z_axis);

            float x_axis;
            float y_axis;
            float z_axis;

            if (!twr_data_stream_get_median(&stream_x, &x_axis)) { return; }
            if (!twr_data_stream_get_median(&stream_y, &y_axis)) { return; }
            if (!twr_data_stream_get_median(&stream_z, &z_axis)) { return; }

            // Update dice with new vectors
            twr_dice_feed_vectors(&dice, x_axis, y_axis, z_axis);

            // This variable holds last dice face
            static twr_dice_face_t last_face = TWR_DICE_FACE_UNKNOWN;

            // Get current dice face
            twr_dice_face_t face = twr_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != face)
            {
                // Remember last dice face
                last_face = face;

                twr_log_info("APP: New orientation = %d", (int) face);

                report = (face == 1 || face == 6) ? twr_tick_get() + DOOR_TIMEOUT : 0;

                twr_led_pulse(&led, 200);

                twr_data_stream_reset(&stream_x);
                twr_data_stream_reset(&stream_y);
                twr_data_stream_reset(&stream_z);
            }
        }
    }
    // Error event?
    else if (event == TWR_LIS2DH12_EVENT_ERROR)
    {
        twr_log_error("APP: Accelerometer error event");

        twr_data_stream_reset(&stream_x);
        twr_data_stream_reset(&stream_y);
        twr_data_stream_reset(&stream_z);
    }
}

// This function dispatches Battery Module events
void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        twr_log_debug("APP: Battery update event");

        float voltage;

        if (twr_module_battery_get_voltage(&voltage))
        {
            twr_log_debug("APP: Voltage = %.2f V", voltage);

            twr_data_stream_feed(&stream_v, &voltage);
        }
    }
    else if (event == TWR_MODULE_BATTERY_EVENT_ERROR)
    {
        twr_log_error("APP: Battery error event");

        twr_data_stream_reset(&stream_v);
    }
}

// This function dispatches Sigfox Module events
void sigfox_event_handler(twr_module_sigfox_t *self, twr_module_sigfox_event_t event, void *event_param)
{
    if (event == TWR_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_START)
    {
        twr_log_info("APP: Sigfox transmission started event");
    }
    else if (event == TWR_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_DONE)
    {
        twr_log_info("APP: Sigfox transmission finished event");
    }
    else if (event == TWR_MODULE_SIGFOX_EVENT_ERROR)
    {
        twr_log_error("APP: Sigfox error event");
    }
    else if (event == TWR_MODULE_SIGFOX_EVENT_READY)
    {
        static bool is_device_id_read;

        if (!is_device_id_read)
        {
            is_device_id_read = true;

            twr_module_sigfox_read_device_id(&sigfox);
        }
    }
    else if (event == TWR_MODULE_SIGFOX_EVENT_READ_DEVICE_ID)
    {
        twr_log_info("APP: Sigfox Device ID read event");

        char buffer[16];

        twr_module_sigfox_get_device_id(&sigfox, buffer, sizeof(buffer));

        twr_log_info("APP: Sigfox Device ID = %s", buffer);
    }
}

void application_init(void)
{
    // Initialize data streams
    twr_data_stream_init(&stream_v, SAMPLE_COUNT_VOLTAGE, &stream_buffer_v);
    twr_data_stream_init(&stream_t, SAMPLE_COUNT_TEMPERATURE, &stream_buffer_t);
    twr_data_stream_init(&stream_x, SAMPLE_COUNT_ACCELERATION, &stream_buffer_x);
    twr_data_stream_init(&stream_y, SAMPLE_COUNT_ACCELERATION, &stream_buffer_y);
    twr_data_stream_init(&stream_z, SAMPLE_COUNT_ACCELERATION, &stream_buffer_z);

    // Initialize logging
    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_pulse(&led, 1 * SECONDS);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize thermometer
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&tmp112, UPDATE_INTERVAL_THERMOMETER);

    // Initialize accelerometer
    twr_lis2dh12_init(&lis2dh12, TWR_I2C_I2C0, 0x19);
    twr_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    twr_lis2dh12_set_update_interval(&lis2dh12, UPDATE_INTERVAL_ACCELEROMETER);

    // Initialize dice
    twr_dice_init(&dice, TWR_DICE_FACE_UNKNOWN);

    // Initialize Battery Module
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(UPDATE_INTERVAL_BATTERY);

    // Initialize Sigfox Module
    twr_module_sigfox_init(&sigfox, TWR_MODULE_SIGFOX_REVISION_R2);
    twr_module_sigfox_set_event_handler(&sigfox, sigfox_event_handler, NULL);

    twr_log_info("APP: Initialization finished");
}

bool transmit(void)
{
    uint8_t buffer[4];

    float average;

    if (twr_data_stream_get_average(&stream_v, &average))
    {
        uint16_t a = average * 1000;

        buffer[0] = a >> 8;
        buffer[1] = a;
    }
    else
    {
        buffer[0] = 0xff;
        buffer[1] = 0xff;
    }

    if (twr_data_stream_get_average(&stream_t, &average))
    {
        int16_t a = average * 100;

        buffer[2] = a >> 8;
        buffer[3] = a;
    }
    else
    {
        buffer[2] = 0x7f;
        buffer[3] = 0xff;
    }

    if (!twr_module_sigfox_send_rf_frame(&sigfox, buffer, sizeof(buffer)))
    {
        twr_log_warning("APP: Coult not start Sigfox transmission");

        return false;
    }

    return true;
}

void application_task(void)
{
    // Plan next run this function after 1 second
    twr_scheduler_plan_current_from_now(1 * SECONDS);

    if (report != 0)
    {
        if (twr_tick_get() >= report)
        {
            report = 0;

            twr_log_info("APP: Garage door open");

            if (transmit())
            {
                twr_led_pulse(&led, 2 * SECONDS);
            }
        }
    }
}
