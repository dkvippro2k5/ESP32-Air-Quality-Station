set(component_srcs "ssd1306.c" "ssd1306_spi.c")

# Lấy phiên bản IDF để quyết định thư viện I2C
set(idf_version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

if(idf_version VERSION_GREATER_EQUAL "5.2")
    if(CONFIG_LEGACY_DRIVER)
        list(APPEND component_srcs "ssd1306_i2c_legacy.c")
    else()
        list(APPEND component_srcs "ssd1306_i2c_new.c")
    endif()
else()
    list(APPEND component_srcs "ssd1306_i2c_legacy.c")
endif()

# Chuyển PRIV_REQUIRES thành REQUIRES và bỏ ngoặc kép để khai báo dạng danh sách rời rạc
idf_component_register(SRCS ${component_srcs} 
                       REQUIRES driver esp_driver_spi esp_driver_i2c esp_driver_gpio 
                       INCLUDE_DIRS ".")