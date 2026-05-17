/**
 * @brief Simple test to move 2 gauges randomly
 *
 */

#include "esp32_vid6608_rmt.h"

#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "VID6608";

extern "C" void app_main(void)
{
    esp32_vid6608_rmt::Config m1Cfg {
        .stepPin   = GPIO_NUM_14,
        .dirPin    = GPIO_NUM_18,
        .maxSteps  = 12*325,
    };
    esp32_vid6608_rmt m1 = esp32_vid6608_rmt(m1Cfg);

    esp32_vid6608_rmt::Config m2Cfg {
        .stepPin   = GPIO_NUM_19,
        .dirPin    = GPIO_NUM_20,
        .maxSteps  = 12*275,
    };
    esp32_vid6608_rmt m2 = esp32_vid6608_rmt(m2Cfg);

    ESP_LOGI(TAG, "Zero 1");

    m1.zero();
    m2.zero();

    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "Zero 2");

    m1.zero();
    m2.zero();

    ESP_LOGI(TAG, "Zero done");

    for (int x=0; x<128; x++) {
        int32_t rndMove = 0;
        vTaskDelay(pdMS_TO_TICKS(2000));
        rndMove = esp_random() % m1.getMaxSteps();
        m1.setPos(rndMove);
        rndMove = esp_random() % m2.getMaxSteps();
        m2.setPos(rndMove);
    }
}
