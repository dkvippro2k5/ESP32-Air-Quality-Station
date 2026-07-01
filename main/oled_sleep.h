#ifndef OLED_SLEEP_H
#define OLED_SLEEP_H

#include "ssd1306.h"

/**
 * @brief Initialize the OLED display sleep/wake cycle.
 * 
 * Sets up a hardware timer and a FreeRTOS task to cycle the OLED screen
 * through 10 seconds of wake state followed by 5 minutes of sleep.
 * 
 * @param dev Pointer to the initialized SSD1306_t device handle.
 */
void oled_sleep_init(SSD1306_t * dev);
bool oled_sleep_is_sleeping(void);
void oled_sleep_wake_up_isr(void* arg);

#endif // OLED_SLEEP_H
