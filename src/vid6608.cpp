#include "vid6608.h"

#include "esp_log.h"

static constexpr uint32_t RMT_RES_HZ = 1'000'000;  // 1 тик = 1 мкс
static const char       *TAG         = "vid6608";

vid6608::vid6608(const Config &cfg) : config(cfg) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << this->config.dirPin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(cfg.dirPin, 0);

    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num          = this->config.stepPin;
    tx_cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz     = RMT_RES_HZ;
    tx_cfg.mem_block_symbols = 48;   // ESP32-C6: 48 = 1 HW-channel
    tx_cfg.trans_queue_depth = 4;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &this->chan));

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &this->enc));
    ESP_ERROR_CHECK(rmt_enable(chan));

    this->infoMutex = xSemaphoreCreateMutex();
    this->taskNotify = xSemaphoreCreateBinary();

    this->running = true;

    // Start task
    xTaskCreate(
        &vid6608::driverTaskStart,    /* Function to implement the task */
        "vid6608",                    /* Name of the task */
        1024,                         /* Stack size in words */
        this,                         /* Task input parameter */
        0,                            /* Priority of the task, lowest */
        &this->taskHandle             /* Task handle. */
    );
}

vid6608::~vid6608() {
    this->running = false;
    xSemaphoreGive(this->taskNotify);
    vTaskDelete(this->taskHandle);
    if (this->chan) {
        rmt_disable(this->chan);
        rmt_del_channel(this->chan);
    }
    if (this->enc) rmt_del_encoder(this->enc);
}

void vid6608::zero() {
    xSemaphoreTake(this->infoMutex, portMAX_DELAY);
    this->targetPositionNext = 0;
    this->targetPosition = 0;
    int32_t maxSteps = this->config.maxSteps;
    this->moveCommand(maxSteps, 1000);
    this->wait();
    this->moveCommand(-maxSteps, 1000);
    this->wait();
    this->moveCommand(-24, 100);
    this->wait();
    xSemaphoreGive(this->infoMutex);
}

void vid6608::wait() {
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(this->chan, -1));
}

void vid6608::setPos(int32_t steps) {
    xSemaphoreTake(this->infoMutex, portMAX_DELAY);
    this->targetPositionNext = steps;
    xSemaphoreGive(this->infoMutex);
    // Notify thread
    xSemaphoreGive(this->taskNotify);
}

/**
 * @brief Main control function, checks and feeds for new tasks from queue
 *
 */
void vid6608::driverTask() {
    do {
        int32_t targetMove = 0; // Now much steps we need to move?
        xSemaphoreTake(this->infoMutex, portMAX_DELAY);
        // Sanity check
        if (this->targetPositionNext < 0) {
            this->targetPositionNext = 0;
        }
        if (this->targetPositionNext > this->config.maxSteps) {
            this->targetPositionNext = this->config.maxSteps - 1;
        }
        if (this->targetPositionNext != this->targetPosition) {
            // We have scheduled as new, calculate new diff
            targetMove = this->targetPositionNext - this->targetPosition;
            this->targetPosition = this->targetPositionNext;
        }
        xSemaphoreGive(this->infoMutex);
        // We need move?
        if (targetMove) {
            this->moveCommand(targetMove, 1000);
            this->wait();
            continue; // New loop
        }
        // Nothing to do: wait for info updates
        xSemaphoreTake(this->taskNotify, portMAX_DELAY);
    } while (this->running);
}

void vid6608::driverTaskStart(void *arg) {
    static_cast<vid6608 *>(arg)->driverTask();
}

void vid6608::moveCommand(int32_t steps, int32_t speed_hz) {
    if (steps == 0) return;
    gpio_set_level(this->config.dirPin, steps > 0 ? 0 : 1);
    uint32_t n    = steps > 0 ? steps : -steps;
    ESP_LOGI(TAG, "D %d, Move: %d", this->config.stepPin, steps);
    uint16_t half = RMT_RES_HZ / (speed_hz * 2);

    rmt_symbol_word_t pulse = {
        .duration0 = half, .level0 = 1,
        .duration1 = half, .level1 = 0,
    };

    rmt_transmit_config_t tx = {};
    tx.loop_count = (int)n - 1;   // сам символ + (n-1) повторов = n импульсов
    ESP_ERROR_CHECK(rmt_transmit(this->chan, this->enc, &pulse, sizeof(pulse), &tx));
}
