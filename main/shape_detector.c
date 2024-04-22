#include <esp_log.h>
#include <esp_system.h>
#include <esp_err.h>
#include <string.h>
#include <esp_camera.h>
#include "servo_lib.h"
#include "camera_lib.h"
#include "wifi_lib.h"
#include "ftp_lib.h"
#include "mqtt_lib.h"
#include "UI_commands.h"

#define SSID "WiFi SSID"
#define PASSWORD "WiFi PASS"

#define MQTT_URI "mqtt://"
#define ENQ 5
#define ACK 6

#define FTP_SERVER "IP address"
#define FTP_PORT "21"
#define FTP_USER "ESP32-CAM"
#define FTP_PASS "ESP32-CAM-PASS"
#define FTP_PICTURE_PATH "~/original.bmp"


static const char *TAG = "shape_detector";


static void mqtt_data_handler(char *payload);


void app_main(void)
{
	ESP_ERROR_CHECK(init_servo());

	ESP_ERROR_CHECK(init_camera());

	ESP_ERROR_CHECK(connect_to_wifi(SSID, PASSWORD));

	ESP_ERROR_CHECK(init_ftp_client(FTP_SERVER, FTP_PORT, FTP_USER, FTP_PASS));

	ESP_ERROR_CHECK(start_mqtt_client(MQTT_URI, mqtt_data_handler));

	ESP_LOGI(TAG, "Free memory: %.2f MiB",
		esp_get_free_heap_size() / 1024.0 / 1024);
}


static void mqtt_data_handler(char *payload)
{
	static camera_fb_t *original_picture = NULL;

	char *command = strtok(payload, " ");
	if (!command) {
		return;
	}

	if (!strcmp(command, "shoot")) {
		shoot(&original_picture);

	} else if (!strcmp(command, "flash")) {
		flash(strtok(NULL, " "));

	} else if (!strcmp(command, "intensity")) {
		flash_intensity(strtok(NULL, " "));

	} else if (!strcmp(command, "save")) {
		save(original_picture, FTP_PICTURE_PATH);

	} else if (!strcmp(command, "saveas")) {
		save(original_picture, strtok(NULL, " "));

	} else if (!strcmp(command, "rotate")) {
		rotate(strtok(NULL, " "));

	} else if (!strcmp(command, "fetch")) {
		fetch(original_picture);

	} else if (!strcmp(command, "brightness") ||
		!strcmp(command, "contrast") ||
		!strcmp(command, "saturation")) {
		adjust_img_properties(command, strtok(NULL, " "));

	} else if (!strcmp(command, "reboot")) {
		esp_restart();

	} else if (command[0] == ENQ && command[1] == '\0') {
		char ack[2] = {ACK, 0};
		mqtt_publish(ack);

	} else {
		mqtt_publish("Unknown command, %s", payload);

	}
}
