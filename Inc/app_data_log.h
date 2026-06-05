#ifndef APP_DATA_LOG_H
#define APP_DATA_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_can.h"

typedef struct {
  uint32_t timestamp_ms;
  app_can_dashboard_data_t dashboard_data;
} app_data_log_sample_t;

void app_data_log_init(void);
void app_data_log_reset(void);
void app_data_log_record(uint32_t timestamp_ms, const app_can_dashboard_data_t *data);
uint32_t app_data_log_get_capacity(void);
uint32_t app_data_log_get_count(void);
bool app_data_log_is_wrapped(void);
bool app_data_log_get_sample(uint32_t index_from_oldest, app_data_log_sample_t *sample);
void app_data_log_dump_csv(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_LOG_H */
