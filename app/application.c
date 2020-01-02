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
bc_led_t led;

// Button instance
bc_button_t button;

// Thermometer instance
bc_tmp112_t tmp112;

// Accelerometer instance
bc_lis2dh12_t lis2dh12;

// Dice instance
bc_dice_t dice;

// Sigfox Module instance
bc_module_sigfox_t sigfox;

// Data stream instances
bc_data_stream_t stream_v;
bc_data_stream_t stream_t;
bc_data_stream_t stream_x;
bc_data_stream_t stream_y;
bc_data_stream_t stream_z;

// Data stream buffer instances
BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_v, SAMPLE_COUNT_VOLTAGE)
BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_t, SAMPLE_COUNT_TEMPERATURE)
BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_x, SAMPLE_COUNT_ACCELERATION)
BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_y, SAMPLE_COUNT_ACCELERATION)
BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_z, SAMPLE_COUNT_ACCELERATION)

bc_tick_t report;

// This function dispatches button events
void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_log_info("APP: Button press");

        // Trigger immediate state transmission
        report = bc_tick_get();
    }
}

// This function dispatches thermometer events
void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    if (event == BC_TMP112_EVENT_UPDATE)
    {
        bc_log_debug("APP: Thermometer update event");

        float temperature;

        if (bc_tmp112_get_temperature_celsius(&tmp112, &temperature))
        {
            bc_log_debug("APP: Temperature = %.2f C", temperature);

            bc_data_stream_feed(&stream_t, &temperature);
        }
        else
        {
            bc_data_stream_reset(&stream_t);
        }
    }
    else if (event == BC_TMP112_EVENT_ERROR)
    {
        bc_log_error("APP: Thermometer error event");

        bc_data_stream_reset(&stream_t);
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_LIS2DH12_EVENT_UPDATE)
    {
        bc_log_debug("APP: Accelerometer update event");

        bc_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (bc_lis2dh12_get_result_g(self, &result))
        {
            bc_log_debug("APP: Acceleration = [%.2f, %.2f, %.2f] g", result.x_axis, result.y_axis, result.z_axis);

            bc_data_stream_feed(&stream_x, &result.x_axis);
            bc_data_stream_feed(&stream_y, &result.y_axis);
            bc_data_stream_feed(&stream_z, &result.z_axis);

            float x_axis;
            float y_axis;
            float z_axis;

            if (!bc_data_stream_get_median(&stream_x, &x_axis)) { return; }
            if (!bc_data_stream_get_median(&stream_y, &y_axis)) { return; }
            if (!bc_data_stream_get_median(&stream_z, &z_axis)) { return; }

            // Update dice with new vectors
            bc_dice_feed_vectors(&dice, x_axis, y_axis, z_axis);

            // This variable holds last dice face
            static bc_dice_face_t last_face = BC_DICE_FACE_UNKNOWN;

            // Get current dice face
            bc_dice_face_t face = bc_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != face)
            {
                // Remember last dice face
                last_face = face;

                bc_log_info("APP: New orientation = %d", (int) face);

                report = (face == 1 || face == 6) ? bc_tick_get() + DOOR_TIMEOUT : 0;

                bc_led_pulse(&led, 200);

                bc_data_stream_reset(&stream_x);
                bc_data_stream_reset(&stream_y);
                bc_data_stream_reset(&stream_z);
            }
        }
    }
    // Error event?
    else if (event == BC_LIS2DH12_EVENT_ERROR)
    {
        bc_log_error("APP: Accelerometer error event");

        bc_data_stream_reset(&stream_x);
        bc_data_stream_reset(&stream_y);
        bc_data_stream_reset(&stream_z);
    }
}

// This function dispatches Battery Module events
void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        bc_log_debug("APP: Battery update event");

        float voltage;

        if (bc_module_battery_get_voltage(&voltage))
        {
            bc_log_debug("APP: Voltage = %.2f V", voltage);

            bc_data_stream_feed(&stream_v, &voltage);
        }
    }
    else if (event == BC_MODULE_BATTERY_EVENT_ERROR)
    {
        bc_log_error("APP: Battery error event");

        bc_data_stream_reset(&stream_v);
    }
}

// This function dispatches Sigfox Module events
void sigfox_event_handler(bc_module_sigfox_t *self, bc_module_sigfox_event_t event, void *event_param)
{
    if (event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_START)
    {
        bc_log_info("APP: Sigfox transmission started event");
    }
    else if (event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_DONE)
    {
        bc_log_info("APP: Sigfox transmission finished event");
    }
    else if (event == BC_MODULE_SIGFOX_EVENT_ERROR)
    {
        bc_log_error("APP: Sigfox error event");
    }
    else if (event == BC_MODULE_SIGFOX_EVENT_READY)
    {
        static bool is_device_id_read;

        if (!is_device_id_read)
        {
            is_device_id_read = true;

            bc_module_sigfox_read_device_id(&sigfox);
        }
    }
    else if (event == BC_MODULE_SIGFOX_EVENT_READ_DEVICE_ID)
    {
        bc_log_info("APP: Sigfox Device ID read event");

        char buffer[16];

        bc_module_sigfox_get_device_id(&sigfox, buffer, sizeof(buffer));

        bc_log_info("APP: Sigfox Device ID = %s", buffer);
    }
}

void application_init(void)
{
    // Initialize data streams
    bc_data_stream_init(&stream_v, SAMPLE_COUNT_VOLTAGE, &stream_buffer_v);
    bc_data_stream_init(&stream_t, SAMPLE_COUNT_TEMPERATURE, &stream_buffer_t);
    bc_data_stream_init(&stream_x, SAMPLE_COUNT_ACCELERATION, &stream_buffer_x);
    bc_data_stream_init(&stream_y, SAMPLE_COUNT_ACCELERATION, &stream_buffer_y);
    bc_data_stream_init(&stream_z, SAMPLE_COUNT_ACCELERATION, &stream_buffer_z);

    // Initialize logging
    bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_pulse(&led, 1 * SECONDS);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize thermometer
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&tmp112, UPDATE_INTERVAL_THERMOMETER);

    // Initialize accelerometer
    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    bc_lis2dh12_set_update_interval(&lis2dh12, UPDATE_INTERVAL_ACCELEROMETER);

    // Initialize dice
    bc_dice_init(&dice, BC_DICE_FACE_UNKNOWN);

    // Initialize Battery Module
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(UPDATE_INTERVAL_BATTERY);

    // Initialize Sigfox Module
    bc_module_sigfox_init(&sigfox, BC_MODULE_SIGFOX_REVISION_R2);
    bc_module_sigfox_set_event_handler(&sigfox, sigfox_event_handler, NULL);

    bc_log_info("APP: Initialization finished");
}

bool transmit(void)
{
    uint8_t buffer[4];

    float average;

    if (bc_data_stream_get_average(&stream_v, &average))
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

    if (bc_data_stream_get_average(&stream_t, &average))
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

    if (!bc_module_sigfox_send_rf_frame(&sigfox, buffer, sizeof(buffer)))
    {
        bc_log_warning("APP: Coult not start Sigfox transmission");

        return false;
    }

    return true;
}

void application_task(void)
{
    // Plan next run this function after 1 second
    bc_scheduler_plan_current_from_now(1 * SECONDS);

    if (report != 0)
    {
        if (bc_tick_get() >= report)
        {
            report = 0;

            bc_log_info("APP: Garage door open");

            if (transmit())
            {
                bc_led_pulse(&led, 2 * SECONDS);
            }
        }
    }
}
