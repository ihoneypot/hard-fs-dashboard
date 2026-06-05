#include "app_data_log.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define APP_DATA_LOG_CAPACITY 4096U

typedef struct {
  app_data_log_sample_t samples[APP_DATA_LOG_CAPACITY];
  uint32_t head;
  uint32_t count;
  bool wrapped;
} app_data_log_state_t;

static app_data_log_state_t app_data_log_state = {0};

static uint32_t app_data_log_get_oldest_index(void) {
  if (app_data_log_state.count < APP_DATA_LOG_CAPACITY) {
    return 0U;
  }

  return app_data_log_state.head;
}

void app_data_log_init(void) {
  app_data_log_reset();
}

void app_data_log_reset(void) {
  taskENTER_CRITICAL();
  memset(&app_data_log_state, 0, sizeof(app_data_log_state));
  taskEXIT_CRITICAL();
}

void app_data_log_record(uint32_t timestamp_ms, const app_can_dashboard_data_t *data) {
  if (data == NULL) {
    return;
  }

  taskENTER_CRITICAL();
  app_data_log_state.samples[app_data_log_state.head].timestamp_ms = timestamp_ms;
  app_data_log_state.samples[app_data_log_state.head].dashboard_data = *data;
  app_data_log_state.head = (app_data_log_state.head + 1U) % APP_DATA_LOG_CAPACITY;

  if (app_data_log_state.count < APP_DATA_LOG_CAPACITY) {
    app_data_log_state.count++;
  } else {
    app_data_log_state.wrapped = true;
  }
  taskEXIT_CRITICAL();
}

uint32_t app_data_log_get_capacity(void) {
  return APP_DATA_LOG_CAPACITY;
}

uint32_t app_data_log_get_count(void) {
  uint32_t count = 0U;

  taskENTER_CRITICAL();
  count = app_data_log_state.count;
  taskEXIT_CRITICAL();
  return count;
}

bool app_data_log_is_wrapped(void) {
  bool wrapped = false;

  taskENTER_CRITICAL();
  wrapped = app_data_log_state.wrapped;
  taskEXIT_CRITICAL();
  return wrapped;
}

bool app_data_log_get_sample(uint32_t index_from_oldest, app_data_log_sample_t *sample) {
  uint32_t oldest_index = 0U;
  uint32_t sample_index = 0U;

  if (sample == NULL) {
    return false;
  }

  taskENTER_CRITICAL();
  if (index_from_oldest >= app_data_log_state.count) {
    taskEXIT_CRITICAL();
    return false;
  }

  oldest_index = app_data_log_get_oldest_index();
  sample_index = (oldest_index + index_from_oldest) % APP_DATA_LOG_CAPACITY;
  *sample = app_data_log_state.samples[sample_index];
  taskEXIT_CRITICAL();
  return true;
}

void app_data_log_dump_csv(void) {
  app_data_log_sample_t sample = {0};
  uint32_t count = app_data_log_get_count();
  uint32_t index = 0U;

  printf("timestamp_ms,pack_voltage_deci_v,pack_summed_voltage_deci_v,"
         "lowest_cell_voltage_100uv,average_cell_voltage_100uv,highest_cell_voltage_100uv,"
         "pack_current_deci_a,lowest_pack_current_deci_a,average_pack_current_deci_a,"
         "highest_pack_current_deci_a,lowest_pack_power_kw,average_pack_power_kw,"
         "highest_pack_power_kw,pack_soc,highest_temp_c,average_temp_c,lowest_temp_c,"
         "fault_flags\r\n");

  for (index = 0U; index < count; index++) {
    if (!app_data_log_get_sample(index, &sample)) {
      continue;
    }

    printf("%lu,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%u\r\n",
           (unsigned long)sample.timestamp_ms,
           (unsigned int)sample.dashboard_data.pack_voltage_deci_volts,
           (unsigned int)sample.dashboard_data.pack_summed_voltage_deci_volts,
           (unsigned int)sample.dashboard_data.lowest_cell_voltage_100uv,
           (unsigned int)sample.dashboard_data.average_cell_voltage_100uv,
           (unsigned int)sample.dashboard_data.highest_cell_voltage_100uv,
           (int)sample.dashboard_data.pack_current_deci_amps,
           (int)sample.dashboard_data.lowest_pack_current_deci_amps,
           (int)sample.dashboard_data.average_pack_current_deci_amps,
           (int)sample.dashboard_data.highest_pack_current_deci_amps,
           (int)sample.dashboard_data.lowest_pack_power_kw,
           (int)sample.dashboard_data.average_pack_power_kw,
           (int)sample.dashboard_data.highest_pack_power_kw,
           (unsigned int)sample.dashboard_data.pack_state_of_charge,
           (int)sample.dashboard_data.highest_temperature_c,
           (int)sample.dashboard_data.average_temperature_c,
           (int)sample.dashboard_data.lowest_temperature_c,
           (unsigned int)sample.dashboard_data.fault_flags);
  }
}
