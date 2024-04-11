#include <stdint.h>
#include <stdbool.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include "esp_err_ext.h"

#define PWM_GPIO GPIO_NUM_14
#define LEDC_TIMER LEDC_TIMER_2
#define LEDC_CHANNEL LEDC_CHANNEL_7
#define LEDC_SPEED LEDC_LOW_SPEED_MODE
#define FREQ 50
#define MAX_ANGLE 180.0f
#define MIN_WIDTH_US 500
#define MAX_WIDTH_US 2500
#define DUTY_RESOLUTION LEDC_TIMER_14_BIT
#define SEC_TO_US 1000000.0f


static const char *TAG = "servo_lib";

static uint16_t g_full_duty = 0;
static uint16_t g_cur_angle = 0;


static uint32_t angle_to_duty(uint16_t angle)
{
	float angle_us = angle / MAX_ANGLE * (MAX_WIDTH_US - MIN_WIDTH_US) + MIN_WIDTH_US;

	return (uint32_t)(g_full_duty * angle_us * FREQ / SEC_TO_US);
}

esp_err_t init_servo(void)
{
	ledc_timer_config_t timer_conf = {
		.clk_cfg = LEDC_AUTO_CLK,
		.duty_resolution = DUTY_RESOLUTION,
		.freq_hz = FREQ,
		.speed_mode = LEDC_SPEED,
		.timer_num = LEDC_TIMER
	};

	ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

	ledc_channel_config_t channel_conf = {
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = LEDC_TIMER,
		.channel = LEDC_CHANNEL,
		.speed_mode = LEDC_SPEED,
		.gpio_num = PWM_GPIO,
		.duty = angle_to_duty(g_cur_angle),
		.hpoint = 0
	};

	ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

	g_full_duty = (1 << DUTY_RESOLUTION) - 1;

	ESP_LOGI(TAG, "Servo motor is initiated");

	return ESP_OK;
}

esp_err_t set_servo_angle(int16_t angle, bool relative)
{
	if (relative) {
		angle += g_cur_angle;
	}

	if (angle < 0 || angle > MAX_ANGLE) {
		return ESP_ERR_INVALID_ARG;
	}

	uint32_t duty = angle_to_duty((uint16_t)angle);

	ESP_ERROR_RETURN(ledc_set_duty(LEDC_SPEED, LEDC_CHANNEL, duty));
	ESP_ERROR_RETURN(ledc_update_duty(LEDC_SPEED, LEDC_CHANNEL));

	g_cur_angle = (uint16_t)angle;

	ESP_LOGI(TAG, "Servo angle changed: %u", g_cur_angle);

	return ESP_OK;
}
