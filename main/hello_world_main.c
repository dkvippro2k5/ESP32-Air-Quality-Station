#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Định nghĩa chân LED D2 trên mạch ESP32
#define BLINK_GPIO   GPIO_NUM_2

static const char *TAG = "Blink_Project";

void app_main(void)
{
    ESP_LOGI(TAG, "Dang khoi tao cau hinh led chop tat chân D2...");

    /* 1. Cau hinh pin GPIO_NUM_2 làm Output */
    gpio_reset_pin(BLINK_GPIO); // Reset pin ve trang thai mac dinh an toan
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT); // Dat huong la Ngõ Ra (Output)

    uint8_t led_state = 0;

    while (1) {
        // Dao trang thai LED (0 thanh 1, 1 thanh 0)
        led_state = !led_state;
        
        /* 2. Xuat muc logic ra chan GPIO */
        gpio_set_level(BLINK_GPIO, led_state);
        
        // In log ra Terminal de theo doi trang thai
        ESP_LOGI(TAG, "Led dang: %s", led_state ? "BAT (ON)" : "TAT (OFF)");

        /* 3. Delay trong FreeRTOS (Don vi la Tick)
           pdMS_TO_TICKS(1000) giup chuyen doi tu 1000ms (1 giay) sang so tick tuong ung cua he dieu hanh */
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}