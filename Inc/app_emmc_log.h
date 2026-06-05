#ifndef APP_EMMC_LOG_H
#define APP_EMMC_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_can.h"

typedef struct {
  bool ready;
  bool overflow;
  uint32_t session_id;
  uint32_t record_count;
  uint32_t written_blocks;
  uint32_t reserved_blocks;
  uint32_t staged_bytes;
} app_emmc_log_status_t;

bool app_emmc_log_init(void);
bool app_emmc_log_append(uint32_t timestamp_ms, const app_can_dashboard_data_t *data);
bool app_emmc_log_flush(void);
bool app_emmc_log_reset_session(void);
void app_emmc_log_get_status(app_emmc_log_status_t *status);
void app_emmc_log_dump_csv(void);
bool app_emmc_log_dump_binary(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_EMMC_LOG_H */
