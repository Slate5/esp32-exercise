#include <mqtt_client.h>
#include <esp_log.h>
#include <esp_err.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "esp_err_ext.h"

#define MQTT_TOPIC_SUB "ESP32/shape_detector/input"
#define MQTT_TOPIC_PUB "ESP32/shape_detector/output"


static const char *TAG = "mqtt_lib";

static esp_mqtt_client_handle_t client;


/*
 * This wrapper function is called from mqtt_event_handler() to execute
 * mqtt_data_handler() with the payload as an argument. The mqtt_data_handler()
 * function is defined in shape_detector.c. The purpose is to facilitate
 * handling MQTT data (payload) within shape_detector.c, enabling the processing
 * of "UI commands" from that module rather than directly from the MQTT library.
 */
static void data_handler_wrapper(void (*mqtt_data_handler)(char *), char *payload)
{
	mqtt_data_handler(payload);
}

static void mqtt_event_handler(void *event_handler_arg, esp_event_base_t event_base,
				int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t event = event_data;
	char payload[event->data_len + 1];

	switch ((esp_mqtt_event_id_t)event_id) {
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

		esp_mqtt_client_subscribe(client, MQTT_TOPIC_SUB, 0);
		break;

	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		break;

	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED");

		esp_mqtt_client_publish(client, MQTT_TOPIC_PUB, "ESP32-CAM is"
					" ready to receive input", 0, 0, 1);
		break;

	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED");
		break;

	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");

		strncpy(payload, event->data, event->data_len);
		payload[event->data_len] = '\0';

		data_handler_wrapper(event_handler_arg, payload);
		break;

	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR, description: %s",
			strerror(event->error_handle->error_type));
		break;

	default:
		break;

	}
}

esp_err_t start_mqtt_client(const char *URI, void (*mqtt_data_handler)(char *))
{
	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = URI
	};

	client = esp_mqtt_client_init(&mqtt_cfg);

	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
					mqtt_event_handler, mqtt_data_handler);

	ESP_ERROR_CHECK(esp_mqtt_client_start(client));

	return ESP_OK;
}

esp_err_t mqtt_publish(const char *format, ...)
{
	char payload[256];
	va_list args;

	va_start(args, format);
	vsnprintf(payload, sizeof(payload), format, args);
	va_end(args);

	ESP_ERROR_RETURN(esp_mqtt_client_publish(client, MQTT_TOPIC_PUB,
						payload, 0, 0, 0));
	return ESP_OK;
}
