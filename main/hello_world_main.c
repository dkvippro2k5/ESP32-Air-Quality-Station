#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "mqtt_client.h"
#include "esp_rom_sys.h"

// ----------------- CẤU HÌNH HỆ THỐNG -----------------
#define WIFI_SSID      "455 Vu Tong Phan"
#define WIFI_PASS      "12345679@"
#define MQTT_BROKER    "mqtt://broker.hivemq.com"
#define MQTT_TOPIC     "hust/kien/air_quality"

#define DHT_PIN        GPIO_NUM_21
static const char *TAG = "SUPER_IOT_STATION";
esp_mqtt_client_handle_t mqtt_client;
void mqtt_app_start(void);

// ----------------- 1. QUẢN LÝ SPIFFS -----------------
void init_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = { .base_path = "/spiffs", .partition_label = "storage", .max_files = 5, .format_if_mount_failed = true };
    esp_vfs_spiffs_register(&conf);
}

void log_sensor_data(float temp, float hum) {
    FILE* f = fopen("/spiffs/sensor_log.csv", "a");
    if (f != NULL) {
        // Ghi kèm một mốc đếm (hoặc sau này bạn có thể cấy thêm thời gian thực RTC)
        fprintf(f, "%.1f,%.1f\n", temp, hum);
        fclose(f);
    }
}

// Endpoint xử lý trang chủ: http://<IP_ESP32>/
static esp_err_t root_get_handler(httpd_req_t *req) {
    // Mã HTML của trang web (Có màu sắc và nút bấm)
    const char* html_page = 
        "<html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Trạm Đo Không Khí</title>"
        "<style>body{font-family: Arial; text-align: center; margin-top: 50px; background-color: #f4f4f9;}"
        "button{padding: 15px 30px; font-size: 18px; color: white; background-color: #007bff; border: none; border-radius: 5px; cursor: pointer; box-shadow: 0 4px 6px rgba(0,0,0,0.1);}"
        "button:hover{background-color: #0056b3;}</style></head>"
        "<body><h1>Trạm Quan Trắc IoT Của Kiên</h1>"
        "<p>Dữ liệu đang được ghi liên tục vào bộ nhớ Flash SPIFFS.</p>"
        "<a href=\"/download\"><button>📥 Tải File CSV Lịch Sử</button></a>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ----------------- 2. WEB SERVER (TẢI FILE) -----------------
// Endpoint xử lý khi người dùng truy cập: http://<IP_ESP32>/download
static esp_err_t download_get_handler(httpd_req_t *req) {
    FILE* f = fopen("/spiffs/sensor_log.csv", "r");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found or empty");
        return ESP_FAIL;
    }

    // Ép trình duyệt hiểu đây là file CSV cần tải về
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"data_log.csv\"");

    char chunk[128];
    size_t chunk_size;
    // Đọc từng khúc nhỏ và gửi đi để không làm tràn RAM
    while ((chunk_size = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, chunk_size);
    }
    httpd_resp_send_chunk(req, NULL, 0); // Đánh dấu kết thúc
    fclose(f);
    ESP_LOGI(TAG, "Nguoi dung vua tai file CSV thanh cong!");
    return ESP_OK;
}

void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        // Đăng ký trang chủ '/'
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root_uri);

        // Đăng ký đường dẫn tải file '/download'
        httpd_uri_t download_uri = { .uri = "/download", .method = HTTP_GET, .handler = download_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &download_uri);
        
        ESP_LOGI(TAG, "Web Server da chay. Truy cap IP de xem trang chu.");
    }
}

// ----------------- 3. KẾT NỐI WIFI & MQTT -----------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi, dang thu lai...");
        esp_wifi_connect();
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Da ket noi Wi-Fi! DUC CHI IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver(); // Có mạng mới bật Web Server
        mqtt_app_start();  // <--- THÊM DÒNG NÀY VÀO ĐÂY: Có mạng mới bật MQTT
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS } };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = MQTT_BROKER };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);
}

// ----------------- 4. ĐỌC CẢM BIẾN DHT -----------------
// ----------------- 4. ĐỌC CẢM BIẾN DHT (PHIÊN BẢN CHỐNG NHIỄU) -----------------
int dht_read_data(float *humidity, float *temperature) {
    uint8_t data[5] = {0};
    
    // Tạo "ổ khóa" ngắt của hệ điều hành
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

    gpio_reset_pin(DHT_PIN);
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // Đợi 20ms cho DHT chuẩn bị
    gpio_set_level(DHT_PIN, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    // --- BẮT ĐẦU VÙNG ƯU TIÊN CAO: Tạm khóa ngắt Wi-Fi ---
    portENTER_CRITICAL(&mux);

    int timeout = 0;
    while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 500) { portEXIT_CRITICAL(&mux); return -1; } esp_rom_delay_us(1); }
    timeout = 0; while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 500) { portEXIT_CRITICAL(&mux); return -1; } esp_rom_delay_us(1); }
    timeout = 0; while(gpio_get_level(DHT_PIN) == 1) { if(++timeout > 500) { portEXIT_CRITICAL(&mux); return -1; } esp_rom_delay_us(1); }

    for (int i = 0; i < 40; i++) {
        timeout = 0; while(gpio_get_level(DHT_PIN) == 0) { if(++timeout > 500) { portEXIT_CRITICAL(&mux); return -1; } esp_rom_delay_us(1); }
        uint32_t t = 0; while(gpio_get_level(DHT_PIN) == 1) { esp_rom_delay_us(1); t++; if(t > 500) { portEXIT_CRITICAL(&mux); return -1; } }
        if (t > 40) data[i / 8] |= (1 << (7 - (i % 8)));
    }

    // --- KẾT THÚC VÙNG ƯU TIÊN: Mở lại ngắt cho Wi-Fi ---
    portEXIT_CRITICAL(&mux);

    // Kiểm tra mã lỗi (Checksum)
    if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        *humidity = data[0] + data[1] * 0.1;
        *temperature = data[2] + data[3] * 0.1;
        return 0; // Thành công
    } else {
        return -2; // Sai Checksum
    }
}

// ----------------- HÀM MAIN -----------------
void app_main(void) {
    // NVS Flash bắt buộc phải khởi tạo trước khi dùng Wi-Fi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    init_spiffs();
    wifi_init_sta();

    float temp, hum;
    char mqtt_payload[100];

    while (1) {
        if (dht_read_data(&hum, &temp) == 0) {
            ESP_LOGI(TAG, "DHT: Temp=%.1f C, Hum=%.1f %%", temp, hum);
            
            // 1. Lưu vào Flash (SPIFFS)
            log_sensor_data(temp, hum);

            // 2. Đóng gói chuỗi JSON và Đẩy lên MQTT
            sprintf(mqtt_payload, "{\"temperature\": %.1f, \"humidity\": %.1f}", temp, hum);
            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, mqtt_payload, 0, 1, 0);
            ESP_LOGI(TAG, "Da day du lieu len MQTT: %s", MQTT_TOPIC);
        } else {
            ESP_LOGE(TAG, "Loi doc DHT11, bo qua luot nay.");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Đọc và gửi mỗi 10 giây
    }
}