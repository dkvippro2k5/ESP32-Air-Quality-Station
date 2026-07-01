#include "oled_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG_OLED_SLEEP "OLED_SLEEP"

// Thời gian ngủ: 5 phút = 300,000,000 micro giây
#define OLED_SLEEP_DURATION_US (300000000ULL)
// Thời gian thức: 20 giây = 20,000,000 micro giây (Cập nhật theo yêu cầu)
#define OLED_WAKE_DURATION_US  (20000000ULL)

#define BIT_TIMER_EXPIRED  (1 << 0)
#define BIT_BUTTON_PRESSED (1 << 1)

static esp_timer_handle_t oled_sleep_timer;
static TaskHandle_t oled_task_handle = NULL;
static SSD1306_t * oled_dev;
static volatile bool is_sleeping_state = false;
static volatile int64_t last_button_press = 0;

bool oled_sleep_is_sleeping(void) {
    return is_sleeping_state;
}

// Callback của Hardware Timer
static void oled_timer_callback(void* arg) {
    if (oled_task_handle != NULL) {
        xTaskNotify(oled_task_handle, BIT_TIMER_EXPIRED, eSetBits);
    }
}

// ISR (Interrupt Service Routine) cho nút bấm BOOT có chống nhiễu (Debounce)
void IRAM_ATTR oled_sleep_wake_up_isr(void* arg) {
    int64_t now = esp_timer_get_time();
    if (now - last_button_press > 500000) { // Bỏ qua nếu bấm liên tục dưới 500ms (0.5s)
        last_button_press = now;
        if (oled_task_handle != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xTaskNotifyFromISR(oled_task_handle, BIT_BUTTON_PRESSED, eSetBits, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

static void oled_sleep_task(void *arg) {
    bool is_sleeping = false;
    
    // Khởi động chu kỳ đầu tiên: Giữ màn hình sáng 20s
    ssd1306_display_on(oled_dev);
    esp_timer_start_once(oled_sleep_timer, OLED_WAKE_DURATION_US);
    ESP_LOGI(TAG_OLED_SLEEP, "OLED Sleep Task started. Screen active for 20 seconds.");

    while (1) {
        uint32_t notified_value = 0;
        // Chờ sự kiện từ Timer hoặc Nút bấm
        xTaskNotifyWait(0, ULONG_MAX, &notified_value, portMAX_DELAY);

        // Dừng timer an toàn trước khi xét logic
        esp_timer_stop(oled_sleep_timer);

        if (notified_value & BIT_BUTTON_PRESSED) {
            // NẾU CÓ NGƯỜI BẤM NÚT
            if (is_sleeping) {
                // Đang ngủ -> Đánh thức ngay lập tức
                ssd1306_display_on(oled_dev);
                is_sleeping_state = false;
                is_sleeping = false;
                ESP_LOGI(TAG_OLED_SLEEP, "OLED Display: WAKE (Button Pressed)");
            } else {
                // Đang sáng -> Reset lại thời gian 20s
                ESP_LOGI(TAG_OLED_SLEEP, "OLED Display: WAKE (Timer Extended by Button)");
            }
            esp_timer_start_once(oled_sleep_timer, OLED_WAKE_DURATION_US);
        } 
        else if (notified_value & BIT_TIMER_EXPIRED) {
            // NẾU HẾT THỜI GIAN ĐẾM CỦA TIMER (20s)
            if (!is_sleeping) {
                // Hết 20s -> Chuyển sang Ngủ vĩnh viễn cho đến khi bị bấm nút
                is_sleeping_state = true;
                ssd1306_display_off(oled_dev);
                ESP_LOGI(TAG_OLED_SLEEP, "OLED Display: SLEEP (Forever until button pressed)");
                is_sleeping = true;
                // KHÔNG start timer 5 phút nữa. Hệ thống sẽ đứng chờ xTaskNotifyWait vô thời hạn.
            } 
        }
    }
}

void oled_sleep_init(SSD1306_t * dev) {
    oled_dev = dev;
    
    // Khởi tạo Hardware Timer
    const esp_timer_create_args_t oled_timer_args = {
        .callback = &oled_timer_callback,
        .name = "oled_sleep_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&oled_timer_args, &oled_sleep_timer));

    // Tạo Task
    xTaskCreatePinnedToCore(oled_sleep_task, "OLED_Sleep_Task", 3072, NULL, 3, &oled_task_handle, 1);
}
