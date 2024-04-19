#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_random.h>
#include <esp_camera.h>
#include "servo_lib.h"
#include "camera_lib.h"
#include "mqtt_lib.h"
#include "ftp_lib.h"

#define RED "\033[31m"
#define GRN "\033[32m"
#define NO_COLOR "\033[39m"


static int conv_arg_to_int(char *arg);


void shoot(camera_fb_t **ptr_picture)
{
	if (*ptr_picture) {
		free_picture(ptr_picture);
	}

	*ptr_picture = take_picture();

	if (*ptr_picture) {
		mqtt_publish(GRN "New picture taken (%.2f KiB)" NO_COLOR,
			(*ptr_picture)->len / 1024.0);
	} else {
		mqtt_publish(RED "Failed to take a picture" NO_COLOR);
	}
}

void save(camera_fb_t *picture, const char *filename)
{
	if (!picture) {
		mqtt_publish(RED "No picture in buffer, did you take a shot?" NO_COLOR);
		return;

	} else if (!filename) {
		mqtt_publish(RED "File name is missing" NO_COLOR);
		return;

	}

	esp_err_t ret;

	switch (picture->format) {
	case PIXFORMAT_RGB565:
	case PIXFORMAT_GRAYSCALE:
		uint8_t *bmp = NULL;
		size_t bmp_size = 0;

		if (!frame2bmp(picture, &bmp, &bmp_size)) {
			mqtt_publish(RED "Conversion to BMP failed" NO_COLOR);
			return;
		}

		ret = ftp_upload_data(filename, bmp, bmp_size);
		free(bmp);
		bmp = NULL;

		break;
	case PIXFORMAT_JPEG:
		ret = ftp_upload_data(filename, picture->buf, picture->len);

		break;
	default:
		mqtt_publish(RED "Picture format not supported" NO_COLOR);

		return;
	}

	if (ret == ESP_OK) {
		mqtt_publish(GRN "New picture (%.2f KiB) taken and stored "
			"locally over FTP" NO_COLOR, picture->len / 1024.0);
	} else if (ret == ESP_ERR_NOT_FOUND) {
		mqtt_publish(RED "Failed to connect to the FTP server" NO_COLOR);
	} else {
		mqtt_publish(RED "Failed in uploading picture over FTP" NO_COLOR);
	}
}

void flash(char *arg)
{
	if (!arg) {
		mqtt_publish(RED "`flash` requires argument (on/off)" NO_COLOR);

	} else if (!strcmp(arg, "on")) {
		turn_on_flash(true);
		mqtt_publish(GRN "Flash LED is ON" NO_COLOR);

	} else if (!strcmp(arg, "off")) {
		turn_on_flash(false);
		mqtt_publish(GRN "Flash LED is OFF" NO_COLOR);

	} else {
		mqtt_publish(RED "Invalid argument (on/off)" NO_COLOR);

	}
}

void flash_intensity(char *arg)
{
	int value = conv_arg_to_int(arg);
	if (value == INT_MIN) {
		return;
	}

	esp_err_t ret = set_flash_intensity(value);

	if (ESP_OK == ret) {
		mqtt_publish(GRN "Flash LED intensity is changed" NO_COLOR);
	} else {
		mqtt_publish(RED "Value is out of range (0-255)" NO_COLOR);
	}
}

void rotate(char *arg)
{
	if (!arg) {
		mqtt_publish(RED "`rotate` requires argument (int or rand)" NO_COLOR);
		return;
	}

	int angle;
	bool relative_angle = false;
	char success_msg[50];

	if (!strcmp(arg, "rand")) {
		angle = esp_random() % 181;

		for (uint8_t i = 0; i < 2; ++i) {
			set_servo_angle(95, relative_angle);
			vTaskDelay(pdMS_TO_TICKS(100));
			set_servo_angle(85, relative_angle);
			vTaskDelay(pdMS_TO_TICKS(100));
		}
		vTaskDelay(pdMS_TO_TICKS(150));

		snprintf(success_msg, 50, GRN "Angle is changed to %d°" NO_COLOR, angle);
	} else {
		angle = conv_arg_to_int(arg);
		if (angle == INT_MIN) {
			return;
		}

		if (arg[0] == '-' || arg[0] == '+') {
			relative_angle = true;
		}

		sprintf(success_msg, GRN "Angle is changed" NO_COLOR);
	}

	esp_err_t ret = set_servo_angle(angle, relative_angle);

	if (ret == ESP_OK) {
		mqtt_publish(success_msg);

	} else if (ret == ESP_ERR_INVALID_ARG) {
		mqtt_publish(RED "Angle given is outside of the range" NO_COLOR);

	} else {
		mqtt_publish(RED "Changing angle failed" NO_COLOR);

	}
}

void fetch(camera_fb_t *orig_picture)
{
	mqtt_publish(RED "Not yet implemented" NO_COLOR);
}

void adjust_img_properties(char *setting, char *arg)
{
	int value = conv_arg_to_int(arg);
	if (value == INT_MIN) {
		return;
	}

	esp_err_t ret = set_cam_sensor(setting, value);

	if (ESP_OK == ret) {
		mqtt_publish(GRN "Setting applied" NO_COLOR);

	} else if (ESP_ERR_INVALID_ARG == ret) {
		mqtt_publish(RED "Value has to be between -2 and 2" NO_COLOR);

	} else {
		mqtt_publish(RED "Failed to apply setting" NO_COLOR);

	}
}


static int conv_arg_to_int(char *arg)
{
	if (!arg) {
		mqtt_publish(RED "Value is missing" NO_COLOR);
		return INT_MIN;
	}

	char *endptr;

	int value = (int)strtol(arg, &endptr, 10);

	if (*endptr != '\0') {
		mqtt_publish(RED "Integer conversion failed" NO_COLOR);

		return INT_MIN;
	}

	return value;
}
