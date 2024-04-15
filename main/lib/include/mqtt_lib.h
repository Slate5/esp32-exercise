#pragma once
#include <esp_err.h>
#include <stdarg.h>

esp_err_t start_mqtt_client(const char *URI, void (*mqtt_data_handler)(char *));
esp_err_t mqtt_publish(const char *format, ...);
