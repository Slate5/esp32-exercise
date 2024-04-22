#include <esp_err.h>

/*
 * This macro is similar to ESP_ERROR_CHECK. The difference is that it
 * does not call abort(), but instead it returns the esp_err_t value.
 */
#define ESP_ERROR_RETURN(x)                 \
	do {                                \
		esp_err_t _ret = (x);       \
		if (_ret != ESP_OK) {       \
			return _ret;        \
		}                           \
	} while (0)

