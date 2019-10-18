#include <application.h>

#define DEBUG 1

#if DEBUG == 0
#define UPDATE 10 * 1000
#define TIMEOUT 10 * 60 * 1000
#else
#define UPDATE 1 * 1000
#define TIMEOUT 10 * 1000
#endif

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

// Accelerometer instance
bc_lis2dh12_t lis2dh12;

// Dice instance
bc_dice_t dice;

// Sigfox Module instance
bc_module_sigfox_t sigfox_module;

bc_tick_t report;

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_log_info("APP: Button press");
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_LIS2DH12_EVENT_UPDATE)
    {
        bc_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (bc_lis2dh12_get_result_g(self, &result))
        {
            #if 0
            bc_log_info("APP: Acceleration = [%.2f,%.2f,%.2f]", result.x_axis, result.y_axis, result.z_axis);
            #endif

            // Update dice with new vectors
            bc_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

            // This variable holds last dice face
            static bc_dice_face_t last_face = BC_DICE_FACE_UNKNOWN;

            // Get current dice face
            bc_dice_face_t face = bc_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != face)
            {
                // Remember last dice face
                last_face = face;

                // Convert dice face to integer
                int orientation = face;

                bc_log_info("APP: New orientation = %d", orientation);

                report = (face == 1 || face == 6) ? bc_tick_get() + TIMEOUT : 0;

                bc_led_pulse(&led, 200);
            }
        }
    }
    // Error event?
    else if (event == BC_LIS2DH12_EVENT_ERROR)
    {
        bc_log_error("APP: Accelerometer error");
    }
}

void sigfox_module_event_handler(bc_module_sigfox_t *self, bc_module_sigfox_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_MODULE_SIGFOX_EVENT_ERROR)
    {
        // TODO LED?
        bc_log_info("APP: Sigfox error");
    }
    else if (event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_DONE)
    {
        bc_log_info("APP: Sigfox transmission finished");

        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
}

void application_init(void)
{
    // Initialize logging
    bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_pulse(&led, 1000);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize accelerometer
    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    bc_lis2dh12_set_update_interval(&lis2dh12, UPDATE);

    // Initialize dice
    bc_dice_init(&dice, BC_DICE_FACE_UNKNOWN);

    // Initialize Sigfox Module
    bc_module_sigfox_init(&sigfox_module, BC_MODULE_SIGFOX_REVISION_R2);
    bc_module_sigfox_set_event_handler(&sigfox_module, sigfox_module_event_handler, NULL);
}

void application_task(void)
{
    // Plan next run this function after 1000 ms
    bc_scheduler_plan_current_from_now(1000);

    if (report != 0)
    {
        if (bc_tick_get() >= report)
        {
            report = 0;

            bc_log_info("APP: Garage door open");

            uint8_t buffer[1] = { 0 };

            if (bc_module_sigfox_send_rf_frame(&sigfox_module, buffer, sizeof(buffer)))
            {
                bc_log_info("APP: Sending Sigfox frame...");

                bc_led_pulse(&led, 2000);
            }
        }
    }
}

// TODO Report battery voltage in regular intervals
