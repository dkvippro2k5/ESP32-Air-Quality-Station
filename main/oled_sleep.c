#include "oled_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG_OLED_SLEEP "OLED_SLEEP"

// 5 minutes sleep in microseconds (5 * 60 * 1,000,000)
#define OLED_SLEEP_DURATION_US (5 * 60 * 1000000ULL)
// 10 seconds wake in microseconds (10 * 1,000,000)
#define OLED_WAKE_DURATION_US  (10 * 1000000ULL)

static esp_timer_handle_t oled_sleep_timer;
static SemaphoreHandle_t oled_sleep_sem;
static SSD1306_t * oled_dev;

static void oled_timer_callback(void* arg) {
    xSemaphoreGive(oled_sleep_sem);
}

static volatile bool is_sleeping_state = false;

bool oled_sleep_is_sleeping(void) {
    return is_sleeping_state;
}

static void oled_sleep_task(void *arg) {
    bool is_sleeping = false;
    
    // Start cycle: keep OLED active for the first 10 seconds
    esp_timer_start_once(oled_sleep_timer, OLED_WAKE_DURATION_US);
    ESP_LOGI(TAG_OLED_SLEEP, "OLED Sleep Task started. Screen active for 10 seconds.");

    while (1) {
        if (xSemaphoreTake(oled_sleep_sem, portMAX_DELAY) == pdTRUE) {
            if (!is_sleeping) {
                // Set flag before turning display off so other tasks stop I2C writes
                is_sleeping_state = true;
                ssd1306_display_off(oled_dev);
                ESP_LOGI(TAG_OLED_SLEEP, "OLED Display: SLEEP (5 minutes)");
                is_sleeping = true;
                esp_timer_start_once(oled_sleep_timer, OLED_SLEEP_DURATION_US);
            } else {
                // Turn display on first, then clear flag so other tasks can write
                ssd1306_display_on(oled_dev);
                is_sleeping_state = false;
                ESP_LOGI(TAG_OLED_SLEEP, "OLED Display: WAKE (10 seconds)");
                is_sleeping = false;
                esp_timer_start_once(oled_sleep_timer, OLED_WAKE_DURATION_US);
            }
        }
    }
}

void oled_sleep_init(SSD1306_t * dev) {
    oled_dev = dev;
    oled_sleep_sem = xSemaphoreCreateBinary();
    
    const esp_timer_create_args_t oled_timer_args = {
        .callback = &oled_timer_callback,
        .name = "oled_sleep_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&oled_timer_args, &oled_sleep_timer));

    // Register FreeRTOS task to safely execute I2C/SPI display ON/OFF commands
    xTaskCreatePinnedToCore(oled_sleep_task, "OLED_Sleep_Task", 3072, NULL, 3, NULL, 1);
}
