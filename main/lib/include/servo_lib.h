#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

esp_err_t init_servo(void);
esp_err_t set_servo_angle(int16_t angle, bool relative);
