#pragma once
#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_camera.h>

esp_err_t init_camera(void);
esp_err_t set_flash_intensity(int intensity);
void turn_on_flash(bool flash_on);
camera_fb_t *take_picture();
void free_picture(camera_fb_t **ptr_picture);
esp_err_t set_cam_sensor(char *setting, int value);
