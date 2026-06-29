#include "sampling_proof.h"

#include <stdio.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG_SAMPLING "SAMPLING_PROOF"

static uint32_t s_expected_period_us = 0;
static uint32_t s_measurement_window_us = 0;
static uint32_t s_sample_count = 0;
static uint32_t s_expected_sample_count = 0;
static int64_t s_first_sample_time_us = 0;
static int64_t s_last_sample_time_us = 0;
static int64_t s_total_early_us = 0;
static int64_t s_total_late_us = 0;
static int64_t s_max_early_us = 0;
static int64_t s_max_late_us = 0;
static SemaphoreHandle_t s_sampling_mutex = NULL;
static bool s_measurement_started = false;
static bool s_summary_printed = false;

static void sampling_proof_reset(void) {
    s_sample_count = 0;
    s_expected_sample_count = 0;
    s_first_sample_time_us = 0;
    s_last_sample_time_us = 0;
    s_total_early_us = 0;
    s_total_late_us = 0;
    s_max_early_us = 0;
    s_max_late_us = 0;
    s_measurement_started = false;
    s_summary_printed = false;
}

void sampling_proof_init(uint32_t expected_period_us, uint32_t measurement_window_us) {
    s_expected_period_us = expected_period_us;
    s_measurement_window_us = measurement_window_us;
    s_sampling_mutex = xSemaphoreCreateMutex();
    sampling_proof_reset();
    ESP_LOGI(TAG_SAMPLING, "Sampling proof initialized: period=%lu us, window=%lu us", 
             (unsigned long)s_expected_period_us, (unsigned long)s_measurement_window_us);
}

void sampling_proof_record_sample(void) {
    if (s_sampling_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_sampling_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    int64_t now_us = esp_timer_get_time();

    if (!s_measurement_started) {
        s_measurement_started = true;
        s_first_sample_time_us = now_us;
        s_last_sample_time_us = now_us;
        s_sample_count = 1;
        s_expected_sample_count = (s_measurement_window_us / s_expected_period_us) + 1;
        xSemaphoreGive(s_sampling_mutex);
        return;
    }

    int64_t interval_us = now_us - s_last_sample_time_us;
    int64_t diff_us = interval_us - (int64_t)s_expected_period_us;

    if (diff_us < 0) {
        s_total_early_us += (-diff_us);
        if (-diff_us > s_max_early_us) {
            s_max_early_us = -diff_us;
        }
    } else {
        s_total_late_us += diff_us;
        if (diff_us > s_max_late_us) {
            s_max_late_us = diff_us;
        }
    }

    s_last_sample_time_us = now_us;
    s_sample_count++;

    bool should_print_summary = false;
    if (!s_summary_printed && (now_us - s_first_sample_time_us) >= s_measurement_window_us) {
        s_summary_printed = true;
        should_print_summary = true;
    }

    xSemaphoreGive(s_sampling_mutex);

    if (should_print_summary) {
        sampling_proof_print_summary();
    }
}

void sampling_proof_print_summary(void) {
    if (s_sampling_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_sampling_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if (!s_measurement_started) {
        xSemaphoreGive(s_sampling_mutex);
        ESP_LOGW(TAG_SAMPLING, "No sample recorded yet.");
        return;
    }

    int64_t elapsed_us = s_last_sample_time_us - s_first_sample_time_us;
    int64_t expected_duration_us = (int64_t)s_expected_sample_count * (int64_t)s_expected_period_us;

    ESP_LOGI(TAG_SAMPLING, "================ SAMPLING PROOF SUMMARY ================");
    ESP_LOGI(TAG_SAMPLING, "Configured period: %lu us (%lu s)", (unsigned long)s_expected_period_us, (unsigned long)(s_expected_period_us / 1000000UL));
    ESP_LOGI(TAG_SAMPLING, "Measurement window: %lu us (%lu s)", (unsigned long)s_measurement_window_us, (unsigned long)(s_measurement_window_us / 1000000UL));
    ESP_LOGI(TAG_SAMPLING, "Expected sample count: %lu", (unsigned long)s_expected_sample_count);
    ESP_LOGI(TAG_SAMPLING, "Actual sample count: %lu", (unsigned long)s_sample_count);
    ESP_LOGI(TAG_SAMPLING, "Elapsed time from first->last: %lld us (%lld s)", (long long)elapsed_us, (long long)(elapsed_us / 1000000LL));
    ESP_LOGI(TAG_SAMPLING, "Expected duration: %lld us (%lld s)", (long long)expected_duration_us, (long long)(expected_duration_us / 1000000LL));
    ESP_LOGI(TAG_SAMPLING, "Total early deviation: %lld us", (long long)s_total_early_us);
    ESP_LOGI(TAG_SAMPLING, "Total late deviation: %lld us", (long long)s_total_late_us);
    ESP_LOGI(TAG_SAMPLING, "Max early deviation: %lld us", (long long)s_max_early_us);
    ESP_LOGI(TAG_SAMPLING, "Max late deviation: %lld us", (long long)s_max_late_us);
    ESP_LOGI(TAG_SAMPLING, "======================================================");

    xSemaphoreGive(s_sampling_mutex);
}
