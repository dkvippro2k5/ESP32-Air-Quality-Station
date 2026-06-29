idf_component_register(SRCS "sampling_proof.c" "hello_world_main.c" "oled_sleep.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_adc spiffs nvs_flash esp_wifi esp_netif esp_event esp_http_server mqtt ssd1306 dht esp_driver_uart wpa_supplicant
                       INCLUDE_DIRS ".")