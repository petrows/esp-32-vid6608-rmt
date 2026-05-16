#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"

#include "vid6608.h"

#define STEP_PIN     GPIO_NUM_14
#define DIR_PIN      GPIO_NUM_18
#define RMT_RES_HZ   1000000   // 1 тик = 1 мкс

static const char *TAG = "stepper";

#define GAUGE_RANGE_DEG 270
#define GAUGE_TEST_STEPS 10

extern "C" void app_main(void)
{
    vid6608::Config m1Cfg {
        .stepPin   = GPIO_NUM_14,
        .dirPin    = GPIO_NUM_18,
        .maxSteps  = 12*325,
    };
    vid6608 m1 = vid6608(m1Cfg);

    vid6608::Config m2Cfg {
        .stepPin   = GPIO_NUM_19,
        .dirPin    = GPIO_NUM_20,
        .maxSteps  = 12*275,
    };
    vid6608 m2 = vid6608(m2Cfg);

    m1.zero();
    m2.zero();

    vTaskDelay(pdMS_TO_TICKS(2000));

    int32_t steps_deg = GAUGE_RANGE_DEG / GAUGE_TEST_STEPS;
    int32_t steps_step = steps_deg * 12;
    int32_t steps_total = 0;

    for (int x=0; x<=GAUGE_TEST_STEPS; x++) {
        ESP_LOGI(TAG, "Step: %d", x);
        m1.setPos(steps_total);
        vTaskDelay(pdMS_TO_TICKS(2000));
        m2.setPos(steps_total);
        vTaskDelay(pdMS_TO_TICKS(2000));
        steps_total += steps_step;
    }
}
