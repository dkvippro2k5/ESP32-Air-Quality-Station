idf_component_register(SRCS "hello_world_main.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio spiffs nvs_flash esp_wifi esp_netif esp_event esp_http_server mqtt ssd1306
                       INCLUDE_DIRS ".")