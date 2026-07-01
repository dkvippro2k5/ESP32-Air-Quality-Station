#include "led_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG_LED_SLEEP "LED_SLEEP"

// 5 minutes sleep in microseconds (5 * 60 * 1,000,000)
#define LED_SLEEP_DURATION_US (5 * 60 * 1000000ULL)
// 10 seconds wake in microseconds (10 * 1,000,000)
#define LED_WAKE_DURATION_US  (10 * 1000000ULL)

static esp_timer_handle_t led_sleep_timer;
static SemaphoreHandle_t led_sleep_sem;
static gpio_num_t board_led_pin;
static gpio_num_t sensor_led_pin;
static volatile bool is_sleeping_state = false;

static void led_timer_callback(void* arg) {
    xSemaphoreGive(led_sleep_sem);
}

bool led_sleep_is_sleeping(void) {
    return is_sleeping_state;
}

static void led_sleep_task(void *arg) {
    bool is_sleeping = false;

    esp_timer_start_once(led_sleep_timer, LED_WAKE_DURATION_US);
    ESP_LOGI(TAG_LED_SLEEP, "LED Sleep Task started. LEDs active for 10 seconds.");

    while (1) {
        if (xSemaphoreTake(led_sleep_sem, portMAX_DELAY) == pdTRUE) {
            if (!is_sleeping) {
                is_sleeping_state = true;
                gpio_set_level(board_led_pin, 0);
                gpio_set_level(sensor_led_pin, 0);
                ESP_LOGI(TAG_LED_SLEEP, "LEDs: SLEEP (5 minutes)");
                is_sleeping = true;
                esp_timer_start_once(led_sleep_timer, LED_SLEEP_DURATION_US);
            } else {
                is_sleeping_state = false;
                gpio_set_level(board_led_pin, 1);
                gpio_set_level(sensor_led_pin, 1);
                ESP_LOGI(TAG_LED_SLEEP, "LEDs: WAKE (10 seconds)");
                is_sleeping = false;
                esp_timer_start_once(led_sleep_timer, LED_WAKE_DURATION_US);
            }
        }
    }
}

void led_sleep_init(gpio_num_t board_led_pin_in, gpio_num_t sensor_led_pin_in) {
    board_led_pin = board_led_pin_in;
    sensor_led_pin = sensor_led_pin_in;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << board_led_pin) | (1ULL << sensor_led_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(board_led_pin, 1);
    gpio_set_level(sensor_led_pin, 1);

    led_sleep_sem = xSemaphoreCreateBinary();

    const esp_timer_create_args_t led_timer_args = {
        .callback = &led_timer_callback,
        .name = "led_sleep_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&led_timer_args, &led_sleep_timer));

    xTaskCreatePinnedToCore(led_sleep_task, "LED_Sleep_Task", 3072, NULL, 3, NULL, 1);
}
