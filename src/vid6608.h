#pragma once

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
    void    moveCommand(int32_t steps, int32_t speed_hz);
    void    driverTask();
    static void driverTaskStart(void *arg);
    bool                 running = false;
    Config               config;
    rmt_channel_handle_t chan = nullptr;
    rmt_encoder_handle_t enc  = nullptr;
    SemaphoreHandle_t    infoMutex = nullptr;
    SemaphoreHandle_t    taskNotify = nullptr;
    TaskHandle_t         taskHandle = nullptr;

    int32_t targetPosition = 0;        // Target position in steps
    int32_t targetPositionNext = 0;    // Target position in steps (scheduled for next move)
};
