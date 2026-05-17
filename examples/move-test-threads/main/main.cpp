/**
 * @brief Simple test to move 2 gauges randomly with threads
 *
 * Threading allow to make better visual experience.
 *
 */

#include "vid6608.h"

#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "VID6608";

/**
 * @brief Thread function, to call drive movement in a loop
 *
 * @param arg drive object passed as argument
 */
void backgroundTask(void *arg)
{
    vid6608 *drive = static_cast<vid6608 *>(arg);
    for (int x=0; x<128; x++) {
        vTaskDelay(pdMS_TO_TICKS(esp_random() % 2000));

        // int32_t rndMove = esp_random() % drive->getMaxSteps();
        int32_t rndMove = esp_random() % (12 * 270);

        ESP_LOGI(TAG, "D%d: Move to %d", drive->getPinStep(), rndMove);
        drive->setPos(rndMove);
        ESP_LOGI(TAG, "D%d: Move sent", drive->getPinStep());
        drive->wait();
        ESP_LOGI(TAG, "D%d: Wait done", drive->getPinStep());
    }

    ESP_LOGI(TAG, "D%d: Thread done", drive->getPinStep());
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    esp_log_level_set("vid6608", ESP_LOG_DEBUG);

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

    ESP_LOGI(TAG, "Zero drives");

    m1.zero();
    m2.zero();

    ESP_LOGI(TAG, "Zero done");

    // Start tasks
    xTaskCreate(
        &backgroundTask,              /* Function to implement the task */
        "vid6608-1",                  /* Name of the task */
        1024,                         /* Stack size in words */
        &m1,                          /* Task input parameter */
        0,                            /* Priority of the task, lowest */
        nullptr                       /* Task handle. */
    );
    xTaskCreate(
        &backgroundTask,              /* Function to implement the task */
        "vid6608-2",                  /* Name of the task */
        1024,                         /* Stack size in words */
        &m2,                          /* Task input parameter */
        0,                            /* Priority of the task, lowest */
        nullptr                       /* Task handle. */
    );

    while (true) { vTaskDelay(pdMS_TO_TICKS(2000)); }
}
