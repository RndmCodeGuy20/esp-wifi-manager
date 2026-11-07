#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
#include <stdint.h>
/* Only the public SDK type is needed for this host-side C syntax check.
 * The firmware example compiles against the real ESP-IDF header. */
typedef int32_t esp_err_t;
#endif
