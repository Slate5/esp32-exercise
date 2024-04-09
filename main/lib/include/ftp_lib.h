#pragma once
#include <stdint.h>
#include <esp_err.h>

esp_err_t init_ftp_client(const char *host, const char *port, const char *user, const char *pass);
esp_err_t ftp_upload_data(const char *data_path, const uint8_t *data, size_t size);
