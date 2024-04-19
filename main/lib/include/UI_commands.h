#pragma once
#include <esp_camera.h>

void shoot(camera_fb_t **ptr_picture);
void save(camera_fb_t *picture, const char *filename);
void flash(char *arg);
void flash_intensity(char *arg);
void rotate(char *arg);
void fetch(camera_fb_t *orig_picture);
void adjust_img_properties(char *setting, char *arg);
