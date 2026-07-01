/*
 * Mô tả: Hệ thống Trạm Quan Trắc Không Khí IoT hoàn chỉnh (Wi-Fi + MQTT HiveMQ + OLED + DHT11 + PMS7003)
 * Phiên bản tối ưu hóa: Hardware Timer & UART Interrupts. Tần số lấy mẫu 5s.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

// Thư viện ngoại vi
#include "ssd1306.h"
#include "dht.h"
#include "oled_sleep.h"
#include "sampling_proof.h"

#define WIFI_SSID       "102"
#define WIFI_PASS       "B131021234"
#define MQTT_URI        "mqtts://2cf8a119a7c74d3891fc09c9ca7136f9.s1.eu.hivemq.cloud:8883"
#define MQTT_TOPIC      "hust/kien/air_quality"

#define PMS_UART_PORT   UART_NUM_2
#define PMS_TX_PIN      GPIO_NUM_17 
#define PMS_RX_PIN      GPIO_NUM_16 
#define BUF_SIZE        (1024)
#define DHT_PIN         GPIO_NUM_4
#define BOOT_BUTTON_PIN GPIO_NUM_0
#define I2C_MASTER_SDA  21
#define I2C_MASTER_SCL  22

// Cấu hình chu kỳ lấy mẫu (5 giây = 5.000.000 micro giây)
#define SAMPLING_PERIOD_US 5000000
#define SAMPLING_WINDOW_US (30 * 60 * 1000000ULL)

static const char *TAG = "TRAM_IOT_MAIN";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

volatile int global_pm2_5 = 0;
volatile int global_pm10  = 0;

SSD1306_t dev;
esp_mqtt_client_handle_t mqtt_client = NULL;

// RTOS Sync Objects
static QueueHandle_t uart_queue;
static QueueHandle_t data_queue;
static SemaphoreHandle_t timer_sem;

typedef struct {
    float temp;
    float hum;
    int pm25;
    int pm10;
    int32_t jitter;
} sensor_data_t;

// --- QUẢN LÝ SỰ KIỆN WIFI ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi, dang thu lai...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Da ket noi Wi-Fi, cap duoc IP!");
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Giảm công suất phát Wi-Fi xuống mức 8.5 dBm (34 * 0.25) để tiết kiệm pin
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(34));
    
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// --- QUẢN LÝ SỰ KIỆN MQTT ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to HiveMQ Cloud!");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected!");
            break;
        default:
            break;
    }
}

void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = "esp32_kien",
        .credentials.authentication.password = "Kien123456@",
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// --- CALLBACK CHO HARDWARE TIMER ---
static void timer_callback(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(timer_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// --- TÁC VỤ ĐỌC PMS7003 (SỬ DỤNG UART EVENT/INTERRUPT) ---
void pms7003_task(void *arg) {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(PMS_UART_PORT, &uart_config);
    uart_set_pin(PMS_UART_PORT, PMS_TX_PIN, PMS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    // Cài đặt driver UART với hàng đợi sự kiện (uart_queue)
    uart_driver_install(PMS_UART_PORT, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart_queue, 0);

    uart_event_t event;
    uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    
    while (1) {
        // Task ngủ, chỉ thức dậy khi có sự kiện UART (Ngắt đẩy vào Queue)
        if (xQueueReceive(uart_queue, (void * )&event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(PMS_UART_PORT, data, event.size, portMAX_DELAY);
                if (len > 0) {
                    for (int i = 0; i < len - 31; i++) {
                        if (data[i] == 0x42 && data[i+1] == 0x4D) {
                            global_pm2_5 = (data[i+12] << 8) | data[i+13];
                            global_pm10  = (data[i+14] << 8) | data[i+15];
                            i += 31;
                        }
                    }
                }
            } else {
                uart_flush_input(PMS_UART_PORT);
            }
        }
    }
    free(data);
    vTaskDelete(NULL);
}

// --- TÁC VỤ THU THẬP DỮ LIỆU ĐỊNH KỲ (ĐÁNH THỨC BỞI TIMER) ---
void data_collection_task(void *arg) {
    int16_t temperature = 0, humidity = 0;
    int64_t last_time = esp_timer_get_time();
    gpio_set_pull_mode(DHT_PIN, GPIO_PULLUP_ONLY);

    sampling_proof_init(SAMPLING_PERIOD_US, SAMPLING_WINDOW_US);

    while (1) {
        // Chờ tín hiệu từ Hardware Timer (Đúng 5 giây 1 lần)
        if (xSemaphoreTake(timer_sem, portMAX_DELAY) == pdTRUE) {
            int64_t current_time = esp_timer_get_time();
            int64_t elapsed = current_time - last_time;
            int64_t jitter = elapsed - SAMPLING_PERIOD_US;
            last_time = current_time;

            ESP_LOGI(TAG, "[SAMPLING PROOF] Interval: %lld us | Jitter: %lld us", elapsed, jitter);

            sampling_proof_record_sample();

            sensor_data_t new_data = {0};
            new_data.jitter = (int32_t)jitter;
            
            // Đọc DHT11
            if (dht_read_data(DHT_TYPE_DHT11, DHT_PIN, &humidity, &temperature) == ESP_OK) {
                new_data.temp = temperature / 10.0;
                new_data.hum = humidity / 10.0;
            }

            // Lấy dữ liệu PMS7003 mới nhất từ biến toàn cục
            new_data.pm25 = global_pm2_5;
            new_data.pm10 = global_pm10;

            // Đẩy vào Queue cho Task hiển thị & mạng
            xQueueSend(data_queue, &new_data, portMAX_DELAY);
        }
    }
}

// --- TÁC VỤ HIỂN THỊ OLED VÀ GỬI MQTT ---
void display_mqtt_task(void *arg) {
    sensor_data_t data;
    char text_buffer[64];
    char json_payload[128];

    while (1) {
        // Chờ nhận dữ liệu từ Data Queue
        if (xQueueReceive(data_queue, &data, portMAX_DELAY) == pdTRUE) {
            // 1. Cập nhật màn hình OLED (chỉ khi không ở chế độ sleep)
            if (!oled_sleep_is_sleeping()) {
                ssd1306_clear_screen(&dev, false);
                sprintf(text_buffer, "Nhiet: %.1f C", data.temp);
                ssd1306_display_text(&dev, 0, text_buffer, strlen(text_buffer), false);
                sprintf(text_buffer, "Do Am: %.1f %%", data.hum);
                ssd1306_display_text(&dev, 2, text_buffer, strlen(text_buffer), false);
                sprintf(text_buffer, "PM2.5: %d ug", data.pm25);
                ssd1306_display_text(&dev, 4, text_buffer, strlen(text_buffer), false);
                sprintf(text_buffer, "PM10 : %d ug", data.pm10);
                ssd1306_display_text(&dev, 6, text_buffer, strlen(text_buffer), false);
            }

            // 2. Gửi MQTT
            sprintf(json_payload, "{\"temperature\": %.1f, \"humidity\": %.1f, \"pm25\": %d, \"pm10\": %d, \"jitter\": %ld}",
                    data.temp, data.hum, data.pm25, data.pm10, (long)data.jitter);
            
            if (mqtt_client != NULL) {
                // Gửi với QoS 0 (Tham số thứ 5) để không phải chờ ACK, tiết kiệm pin Wi-Fi
                esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_payload, 0, 0, 0);
                ESP_LOGI(TAG, "Sent JSON: %s", json_payload);
            }
        }
    }
}

void app_main(void) {
    // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo hiển thị OLED
    i2c_master_init(&dev, I2C_MASTER_SDA, I2C_MASTER_SCL, -1);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_display_text(&dev, 2, "Connecting WiFi...", 18, false);

    // Kết nối Wi-Fi & MQTT
    wifi_init_sta();
    mqtt_app_start();

    // Khởi tạo tính năng sleep cho màn hình
    oled_sleep_init(&dev);

    // Cấu hình nút BOOT (GPIO 0) làm Ngắt ngoài để đánh thức OLED
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
    
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(isr_err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(BOOT_BUTTON_PIN, oled_sleep_wake_up_isr, NULL));

    // Khởi tạo RTOS Objects
    data_queue = xQueueCreate(10, sizeof(sensor_data_t));
    timer_sem = xSemaphoreCreateBinary();

    // Khởi tạo Hardware Timer (chu kỳ 5 giây)
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "dht_timer"
    };
    esp_timer_handle_t dht_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &dht_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(dht_timer, SAMPLING_PERIOD_US));

    // Tạo các luồng đa nhiệm
    xTaskCreatePinnedToCore(pms7003_task, "PMS_Task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(data_collection_task, "Data_Task", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(display_mqtt_task, "Disp_MQTT_Task", 4096, NULL, 4, NULL, 1);
}