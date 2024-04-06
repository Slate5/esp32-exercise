#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_err.h>
#include <freertos/semphr.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdint.h>


static const char *TAG = "wifi_lib";

// Postpone execution of other processes (MQTT, FTP) that depend on networking
static SemaphoreHandle_t xSemaphore = NULL;


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
				int32_t event_id, void *event_data)
{
	static unsigned char reconnect_attempts = 0;

	switch (event_id) {
	case WIFI_EVENT_STA_START:
		ESP_LOGI(TAG, "WiFi connecting... Execution is locked by semaphore.");

		break;

	case WIFI_EVENT_STA_CONNECTED:
		ESP_LOGI(TAG, "WiFi connected");

		reconnect_attempts = 0;
		break;

	case WIFI_EVENT_STA_DISCONNECTED:
		ESP_LOGI(TAG, "WiFi disconnected.");

		if (reconnect_attempts < 180) {
			ESP_LOGI(TAG, "Reconnecting...");

			esp_wifi_connect();
			++reconnect_attempts;
		} else {
			ESP_LOGI(TAG, "Max number of reconnect attempts reached");
		}

		break;

	case IP_EVENT_STA_GOT_IP:
		xSemaphoreGive(xSemaphore);

		ESP_LOGI(TAG, "IP is assigned. Execution is unlocked.");
		break;

	}
}

esp_err_t connect_to_wifi(const char *ssid, const char *password)
{
	esp_err_t ret = nvs_flash_init();
	if (ESP_ERR_NVS_NO_FREE_PAGES == ret || ESP_ERR_NVS_NEW_VERSION_FOUND == ret) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
				&wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
				&wifi_event_handler, NULL));

	wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_initiation));

	wifi_config_t wifi_configuration = {
		.sta.ssid = "",
		.sta.password = ""
	};
	strcpy((char *)wifi_configuration.sta.ssid, ssid);
	strcpy((char *)wifi_configuration.sta.password, password);
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_ERROR_CHECK(esp_wifi_connect());

	xSemaphore = xSemaphoreCreateBinary();
	if (xSemaphore == NULL) {
		return ESP_FAIL;
	}

	// Semaphore will be released when the event IP_EVENT_STA_GOT_IP occurs
	xSemaphoreTake(xSemaphore, portMAX_DELAY);

	// IP_EVENT handler is not needed after a successful connection followed
	// by IP_EVENT_STA_GOT_IP. WIFI_EVENT handler will continue to run in
	// order to handle potential reconnections.
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT,
				IP_EVENT_STA_GOT_IP, &wifi_event_handler));

	return ESP_OK;
}
