#include <string.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_camera.h>
#include <hal/ledc_types.h>
#include <driver/ledc.h>
#include "esp_err_ext.h"

// Configuration for OV2640 sensor
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

// Flash LED configuration
#define FLASH_GPIO GPIO_NUM_4
#define FLASH_TIMER LEDC_TIMER_1
#define FLASH_CHANNEL LEDC_CHANNEL_4
#define MIN_FLASH_INTENSITY 0
#define MAX_FLASH_INTENSITY 255


static const char *TAG = "camera_lib";

static struct flash_config {
	bool on;
	uint8_t intensity;
} g_flash;

static camera_config_t camera_config = {
	.pin_pwdn = CAM_PIN_PWDN,
	.pin_reset = CAM_PIN_RESET,
	.pin_xclk = CAM_PIN_XCLK,
	.pin_sccb_sda = CAM_PIN_SIOD,
	.pin_sccb_scl = CAM_PIN_SIOC,
	.pin_d7 = CAM_PIN_D7,
	.pin_d6 = CAM_PIN_D6,
	.pin_d5 = CAM_PIN_D5,
	.pin_d4 = CAM_PIN_D4,
	.pin_d3 = CAM_PIN_D3,
	.pin_d2 = CAM_PIN_D2,
	.pin_d1 = CAM_PIN_D1,
	.pin_d0 = CAM_PIN_D0,
	.pin_vsync = CAM_PIN_VSYNC,
	.pin_href = CAM_PIN_HREF,
	.pin_pclk = CAM_PIN_PCLK,

	.ledc_timer = LEDC_TIMER_0,
	.ledc_channel = LEDC_CHANNEL_0,

	.xclk_freq_hz = 20000000,
	.pixel_format = PIXFORMAT_RGB565,  // PIXFORMAT_ + GRAYSCALE|RGB565|JPEG
	.frame_size = FRAMESIZE_240X240,  // FRAMESIZE_ + 96X96|QQVGA|240X240|QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
	.jpeg_quality = 10,
	.fb_count = 2,
	.grab_mode = CAMERA_GRAB_LATEST,
	.fb_location = CAMERA_FB_IN_PSRAM
};


static esp_err_t setup_flash_led(void)
{
	ledc_timer_config_t timer_conf = {
		.clk_cfg = LEDC_AUTO_CLK,
		.duty_resolution = LEDC_TIMER_8_BIT,
		.freq_hz = 2000,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.timer_num = FLASH_TIMER
	};
	ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

	ledc_channel_config_t channel_conf = {
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = FLASH_TIMER,
		.channel = FLASH_CHANNEL,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.gpio_num = FLASH_GPIO,
		.duty = 0,
		.hpoint = 0
	};
	ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

	g_flash.on = false;
	g_flash.intensity = MIN_FLASH_INTENSITY;

	return ESP_OK;
}

static esp_err_t set_flash_brightness(uint8_t duty) {
	ESP_ERROR_RETURN(ledc_set_duty(LEDC_LOW_SPEED_MODE, FLASH_CHANNEL, duty));
	ESP_ERROR_RETURN(ledc_update_duty(LEDC_LOW_SPEED_MODE, FLASH_CHANNEL));

	return ESP_OK;
}

esp_err_t init_camera(void)
{
	ESP_ERROR_CHECK(esp_camera_init(&camera_config));
	ESP_ERROR_CHECK(setup_flash_led());

	ESP_LOGI(TAG, "Camera and flash LED are initialized");

	return ESP_OK;
}

esp_err_t set_flash_intensity(int intensity)
{
	if (intensity >= MIN_FLASH_INTENSITY && intensity <= MAX_FLASH_INTENSITY) {
		g_flash.intensity = (uint8_t)intensity;

		return ESP_OK;
	}

	return ESP_FAIL;
}

void turn_on_flash(bool flash_on)
{
	g_flash.on = flash_on;

	if (flash_on && g_flash.intensity == 0) {
		int initial_intensity = MIN_FLASH_INTENSITY + MAX_FLASH_INTENSITY * 0.02;

		set_flash_intensity(initial_intensity);
	}
}

camera_fb_t *take_picture()
{
	camera_fb_t *picture;

	if (g_flash.on) {
		for (uint8_t i = 0; i < 5; ++i) {
			if (set_flash_brightness(g_flash.intensity) == ESP_OK) {
				break;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(100));

		picture = esp_camera_fb_get();

		for (uint8_t i = 0; i < 5; ++i) {
			if (set_flash_brightness(MIN_FLASH_INTENSITY) == ESP_OK) {
				break;
			}
		}

	} else {
		picture = esp_camera_fb_get();

	}

	if (!picture) {
		ESP_LOGE(TAG, "Failed to take a picture");

		return NULL;
	}

	ESP_LOGI(TAG, "Picture taken, size: %zu bytes", picture->len);

	return picture;
}

void free_picture(camera_fb_t **ptr_picture)
{
	esp_camera_fb_return(*ptr_picture);
	*ptr_picture = NULL;

	ESP_LOGI(TAG, "Picture frame buffer is freed");
}

esp_err_t set_cam_sensor(char *setting, int value)
{
	if (value < -2 || value > 2) {
		return ESP_ERR_INVALID_ARG;
	}

	sensor_t *cam_sensor = esp_camera_sensor_get();

	esp_err_t ret = ESP_FAIL;

	if (!strcmp(setting, "brightness")) {
		ret = cam_sensor->set_brightness(cam_sensor, value);

	} else if (!strcmp(setting, "contrast")) {
		ret = cam_sensor->set_contrast(cam_sensor, value);

	} else if (!strcmp(setting, "saturation")) {
		ret = cam_sensor->set_saturation(cam_sensor, value);

	}

	return ret;
}
