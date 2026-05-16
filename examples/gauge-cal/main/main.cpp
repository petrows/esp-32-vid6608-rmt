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

#define GAUGE_RANGE_DEG_1 275
#define GAUGE_RANGE_DEG_2 272
#define GAUGE_TEST_STEPS 10

extern "C" void app_main(void)
{
    vid6608::Config m1Cfg {
        .stepPin   = GPIO_NUM_14,
        .dirPin    = GPIO_NUM_18,
        .maxSteps  = 3950, // 329.2°
    };
    vid6608 m1 = vid6608(m1Cfg);

    vid6608::Config m2Cfg {
        .stepPin   = GPIO_NUM_19,
        .dirPin    = GPIO_NUM_20,
        .maxSteps  = 3295, // 274.5°
    };
    vid6608 m2 = vid6608(m2Cfg);

    m1.zero();
    m2.zero();

    vTaskDelay(pdMS_TO_TICKS(2000));

    float steps_deg_1 = float(GAUGE_RANGE_DEG_1) / float(GAUGE_TEST_STEPS);
    float steps_step_1 = steps_deg_1 * 12;
    float steps_total_1 = 0;

    float steps_deg_2 = float(GAUGE_RANGE_DEG_2) / float(GAUGE_TEST_STEPS);
    float steps_step_2 = steps_deg_2 * 12;
    float steps_total_2 = 0;

    bool dir = true;

    for (int d=0; d<10; d++) {
        for (int x=0; x<GAUGE_TEST_STEPS; x++) {
            ESP_LOGI(TAG, "Step: %d", x);
            if (dir) {
                steps_total_1 += steps_step_1;
                steps_total_2 += steps_step_2;
            } else {
                steps_total_1 -= steps_step_1;
                steps_total_2 -= steps_step_2;
            }
            m1.setPos(static_cast<int32_t>(steps_total_1));
            vTaskDelay(pdMS_TO_TICKS(2000));
            m2.setPos(static_cast<int32_t>(steps_total_2));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
        dir = !dir;
    }
}
