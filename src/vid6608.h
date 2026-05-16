#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class vid6608 {
public:
    struct Config {
        gpio_num_t  stepPin;
        gpio_num_t  dirPin;
        uint16_t    maxSteps;
    };

    explicit vid6608(const Config &cfg);
    ~vid6608();

    vid6608(const vid6608 &)            = delete;
    vid6608 &operator=(const vid6608 &) = delete;

    void        zero();
    void        wait();
    void        setPos(int32_t steps);

    uint16_t    getMaxSteps() { return this->config.maxSteps; }
    int32_t     getCurrentPosition() { return this->targetPosition; }

private:
    // Acceleration profile baked into the library — not user-tunable.
    // Each entry is the half-period of one pulse, in RMT ticks (1 µs).
    // Index 0 is the slowest pulse (start of motion); the last index
    // is the fastest pulse (cruise). Deceleration walks the table in reverse.
    // A move shorter than 2*kAccelSteps uses a triangular profile and never
    // reaches the cruise rate.
    static constexpr size_t kAccelSteps = 48;

    static std::array<uint16_t, kAccelSteps> buildAccelCurve();
    static const std::array<uint16_t, kAccelSteps> kAccelHalfPeriod;

    void        moveRamp(int32_t steps);
    void        moveConst(int32_t steps, int32_t speed_hz);
    void        driverTask();
    static void driverTaskStart(void *arg);

    bool                 running    = false;
    Config               config;
    rmt_channel_handle_t chan       = nullptr;
    rmt_encoder_handle_t enc        = nullptr;
    SemaphoreHandle_t    infoMutex  = nullptr;
    SemaphoreHandle_t    taskNotify = nullptr;
    TaskHandle_t         taskHandle = nullptr;

    int32_t targetPosition     = 0;    // Target position in steps
    int32_t targetPositionNext = 0;    // Target position in steps (scheduled for next move)

    // Scratch RMT symbol buffers. The copy encoder pulls bytes from the
    // source pointer asynchronously, so they must outlive each transmission
    // — every move is followed by wait() before the next reuses them.
    rmt_symbol_word_t accelBuf[kAccelSteps]{};
    rmt_symbol_word_t decelBuf[kAccelSteps]{};
    rmt_symbol_word_t cruisePulse{};
};
