#include "vid6608.h"

#include "esp_log.h"

static constexpr uint32_t RMT_RES_HZ = 1'000'000;  // 1 тик = 1 мкс
static const char       *TAG         = "vid6608";

std::array<uint16_t, vid6608::kAccelSteps> vid6608::buildAccelCurve() {
    // Linear ramp from startHz up to cruiseHz over kAccelSteps pulses.
    // Edit these two frequencies to retune the shipped profile.
    constexpr float startHz  = 250.0f;
    constexpr float cruiseHz = 1000.0f;
    std::array<uint16_t, kAccelSteps> out{};
    for (size_t i = 0; i < kAccelSteps; ++i) {
        float t  = float(i + 1) / float(kAccelSteps);
        float hz = startHz + (cruiseHz - startHz) * t;
        out[i]   = static_cast<uint16_t>(float(RMT_RES_HZ) / (hz * 2.0f));
    }
    return out;
}

const std::array<uint16_t, vid6608::kAccelSteps> vid6608::kAccelHalfPeriod =
    vid6608::buildAccelCurve();

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
    this->moveConst(maxSteps, 2000);
    this->wait();
    this->moveConst(-maxSteps, 1000);
    this->wait();
    // Gentle final strike against the mechanical stop — bypass the ramp so
    // the impact is soft and predictable.
    this->moveConst(-12, 100);
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
            this->moveRamp(targetMove);
            // this->moveConst(targetMove, 1000);
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

void vid6608::moveRamp(int32_t steps) {
    if (steps == 0) return;
    gpio_set_level(this->config.dirPin, steps > 0 ? 0 : 1);
    uint32_t n = static_cast<uint32_t>(steps > 0 ? steps : -steps);
    ESP_LOGI(TAG, "D %d, Ramp move: %d", this->config.stepPin, steps);

    // Triangular fallback when the move is too short for a full ramp pair.
    uint32_t rampSteps = static_cast<uint32_t>(kAccelSteps);
    if (n < 2 * rampSteps) {
        rampSteps = n / 2;
    }
    uint32_t cruiseSteps = n - 2 * rampSteps;

    for (uint32_t i = 0; i < rampSteps; ++i) {
        uint16_t h = kAccelHalfPeriod[i];
        accelBuf[i].duration0 = h; accelBuf[i].level0 = 1;
        accelBuf[i].duration1 = h; accelBuf[i].level1 = 0;
    }
    for (uint32_t i = 0; i < rampSteps; ++i) {
        uint16_t h = kAccelHalfPeriod[rampSteps - 1 - i];
        decelBuf[i].duration0 = h; decelBuf[i].level0 = 1;
        decelBuf[i].duration1 = h; decelBuf[i].level1 = 0;
    }

    rmt_transmit_config_t tx = {};

    if (rampSteps > 0) {
        tx.loop_count = 0;
        ESP_ERROR_CHECK(rmt_transmit(this->chan, this->enc, accelBuf,
                                     rampSteps * sizeof(rmt_symbol_word_t), &tx));
    }
    if (cruiseSteps > 0) {
        // For a partial (triangular) ramp the cruise speed is whatever peak
        // the partial ramp reached, not the full top of the curve.
        uint16_t h = rampSteps > 0 ? kAccelHalfPeriod[rampSteps - 1]
                                   : kAccelHalfPeriod[0];
        cruisePulse.duration0 = h; cruisePulse.level0 = 1;
        cruisePulse.duration1 = h; cruisePulse.level1 = 0;
        tx.loop_count = static_cast<int>(cruiseSteps) - 1;
        ESP_ERROR_CHECK(rmt_transmit(this->chan, this->enc, &cruisePulse,
                                     sizeof(cruisePulse), &tx));
    }
    if (rampSteps > 0) {
        tx.loop_count = 0;
        ESP_ERROR_CHECK(rmt_transmit(this->chan, this->enc, decelBuf,
                                     rampSteps * sizeof(rmt_symbol_word_t), &tx));
    }
}

void vid6608::moveConst(int32_t steps, int32_t speed_hz) {
    if (steps == 0) return;
    gpio_set_level(this->config.dirPin, steps > 0 ? 0 : 1);
    uint32_t n    = static_cast<uint32_t>(steps > 0 ? steps : -steps);
    ESP_LOGI(TAG, "D %d, Const move: %d @ %d Hz", this->config.stepPin, steps, speed_hz);
    uint16_t half = RMT_RES_HZ / (speed_hz * 2);

    cruisePulse.duration0 = half; cruisePulse.level0 = 1;
    cruisePulse.duration1 = half; cruisePulse.level1 = 0;

    rmt_transmit_config_t tx = {};
    tx.loop_count = static_cast<int>(n) - 1;   // сам символ + (n-1) повторов = n импульсов
    ESP_ERROR_CHECK(rmt_transmit(this->chan, this->enc, &cruisePulse, sizeof(cruisePulse), &tx));
}
