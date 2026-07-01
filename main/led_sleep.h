#ifndef LED_SLEEP_H
#define LED_SLEEP_H

#include "driver/gpio.h"

/**
 * @brief Initialize the indicator LEDs sleep/wake cycle.
 *
 * The LEDs will stay active for 10 seconds, then enter sleep mode for 5 minutes.
 * The same pattern is used as the OLED sleep cycle.
 *
 * @param board_led_pin GPIO pin connected to the ESP32 board LED.
 * @param sensor_led_pin GPIO pin connected to the DHT sensor indicator LED.
 */
void led_sleep_init(gpio_num_t board_led_pin, gpio_num_t sensor_led_pin);

/**
 * @brief Check whether the LEDs are currently in sleep state.
 */
bool led_sleep_is_sleeping(void);

#endif // LED_SLEEP_H
