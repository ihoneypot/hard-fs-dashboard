#include "app_can.h"

#include <stdio.h>

#include "app_can_config.h"
#include "main.h"

static FDCAN_HandleTypeDef app_hfdcan;
static bool app_can_initialized = false;
#define APP_CAN_ORION_REQUEST_ID 0x7E3U
#define APP_CAN_ORION_RESPONSE_ID 0x7EBU
#define APP_CAN_OBD2_MODE_READ_DATA_BY_IDENTIFIER 0x22U
#define APP_CAN_OBD2_MODE_READ_DATA_RESPONSE 0x62U

#define APP_CAN_PID_PACK_VOLTAGE 0xF00DU
#define APP_CAN_PID_PACK_SUMMED_VOLTAGE 0xF014U
#define APP_CAN_PID_PACK_SOC 0xF00FU
#define APP_CAN_PID_PACK_CURRENT 0xF015U
#define APP_CAN_PID_HIGHEST_TEMPERATURE 0xF028U
#define APP_CAN_PID_LOWEST_TEMPERATURE 0xF029U
#define APP_CAN_PID_AVERAGE_TEMPERATURE 0xF02AU
#define APP_CAN_PID_LOWEST_CELL_VOLTAGE 0xF032U
#define APP_CAN_PID_HIGHEST_CELL_VOLTAGE 0xF033U
#define APP_CAN_PID_AVERAGE_CELL_VOLTAGE 0xF034U

#define APP_CAN_REQUEST_PERIOD_MS 50U
#define APP_CAN_REQUEST_TIMEOUT_MS 100U

typedef enum {
  APP_CAN_REQUEST_PACK_VOLTAGE = 0,
  APP_CAN_REQUEST_PACK_SUMMED_VOLTAGE,
  APP_CAN_REQUEST_PACK_CURRENT,
  APP_CAN_REQUEST_PACK_SOC,
  APP_CAN_REQUEST_HIGHEST_TEMPERATURE,
  APP_CAN_REQUEST_LOWEST_TEMPERATURE,
  APP_CAN_REQUEST_AVERAGE_TEMPERATURE,
  APP_CAN_REQUEST_LOWEST_CELL_VOLTAGE,
  APP_CAN_REQUEST_HIGHEST_CELL_VOLTAGE,
  APP_CAN_REQUEST_AVERAGE_CELL_VOLTAGE,
  APP_CAN_REQUEST_COUNT
} app_can_request_index_t;

typedef struct {
  uint16_t pid;
  uint8_t expected_obd_length;
} app_can_request_definition_t;

static const app_can_request_definition_t app_can_request_table[APP_CAN_REQUEST_COUNT] = {
    [APP_CAN_REQUEST_PACK_VOLTAGE] =
        {
            .pid = APP_CAN_PID_PACK_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_SUMMED_VOLTAGE] =
        {
            .pid = APP_CAN_PID_PACK_SUMMED_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_CURRENT] =
        {
            .pid = APP_CAN_PID_PACK_CURRENT,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_PACK_SOC] =
        {
            .pid = APP_CAN_PID_PACK_SOC,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_HIGHEST_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_HIGHEST_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_LOWEST_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_LOWEST_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_AVERAGE_TEMPERATURE] =
        {
            .pid = APP_CAN_PID_AVERAGE_TEMPERATURE,
            .expected_obd_length = 4U,
        },
    [APP_CAN_REQUEST_LOWEST_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_LOWEST_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_HIGHEST_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_HIGHEST_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
    [APP_CAN_REQUEST_AVERAGE_CELL_VOLTAGE] =
        {
            .pid = APP_CAN_PID_AVERAGE_CELL_VOLTAGE,
            .expected_obd_length = 5U,
        },
};

static app_can_dashboard_data_t app_can_latest_data = {0};
static app_can_request_index_t app_can_next_request = APP_CAN_REQUEST_PACK_VOLTAGE;
static uint16_t app_can_pending_pid = 0U;
static uint32_t app_can_last_request_ms = 0U;
#ifdef DEBUG
static bool app_can_debug_enabled = true;
#else
static bool app_can_debug_enabled = false;
#endif
static bool app_can_current_stats_valid = false;
static int16_t app_can_current_min_deci_amps = 0;
static int16_t app_can_current_max_deci_amps = 0;
static int64_t app_can_current_sum_deci_amps = 0;
static uint32_t app_can_current_sample_count = 0U;
static bool app_can_power_stats_valid = false;
static int16_t app_can_power_min_kw = 0;
static int16_t app_can_power_max_kw = 0;
static int64_t app_can_power_sum_kw = 0;
static uint32_t app_can_power_sample_count = 0U;

static int16_t app_can_decode_orion_current_deci_amps(uint16_t raw_value);
static void app_can_update_current_stats(app_can_dashboard_data_t *data);
static void app_can_update_power_stats(app_can_dashboard_data_t *data);

static bool app_can_log_init_failure(const char *stage) {
  if (app_can_debug_enabled && (stage != NULL)) {
    printf("[can] init failed stage=%s fdcan_error=0x%08lX\r\n",
           stage,
           (unsigned long)app_hfdcan.ErrorCode);
  }

  return false;
}

static int32_t app_can_abs_int32(int32_t value) {
  return (value < 0) ? -value : value;
}

static int16_t app_can_clamp_int64_to_int16(int64_t value) {
  if (value > INT16_MAX) {
    return INT16_MAX;
  }

  if (value < INT16_MIN) {
    return INT16_MIN;
  }

  return (int16_t)value;
}

static void app_can_print_unsigned_deci_volts(const char *label, uint16_t deci_volts) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%u.%uV", label, (unsigned int)(deci_volts / 10U), (unsigned int)(deci_volts % 10U));
}

static void app_can_print_unsigned_100uv_volts(const char *label, uint16_t voltage_100uv) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%u.%04uV",
         label,
         (unsigned int)(voltage_100uv / 10000U),
         (unsigned int)(voltage_100uv % 10000U));
}

static void app_can_print_signed_deci_amps(const char *label, int16_t deci_amps) {
  int32_t abs_deci_amps = 0;

  if (label == NULL) {
    return;
  }

  abs_deci_amps = app_can_abs_int32((int32_t)deci_amps);
  printf(" %s=%s%ld.%ldA",
         label,
         (deci_amps < 0) ? "-" : "",
         (long)(abs_deci_amps / 10),
         (long)(abs_deci_amps % 10));
}

static uint16_t app_can_get_display_voltage_deci_volts(const app_can_dashboard_data_t *data) {
  if (data == NULL) {
    return 0U;
  }

  if (data->pack_voltage_deci_volts != 0U) {
    return data->pack_voltage_deci_volts;
  }

  return data->pack_summed_voltage_deci_volts;
}

static int16_t app_can_compute_power_kw(int16_t current_deci_amps, uint16_t voltage_deci_volts) {
  int64_t numerator = (int64_t)current_deci_amps * (int64_t)voltage_deci_volts;

  /* deci-amps * deci-volts = 0.01 W per count, divided by 100000 for kW.
   * Add/subtract half the divisor first to round to the nearest integer kW. */

  if (numerator >= 0) {
    return app_can_clamp_int64_to_int16((numerator + 50000) / 100000);
  }

  return app_can_clamp_int64_to_int16((numerator - 50000) / 100000);
}

static void app_can_print_signed_integer(const char *label, int16_t value) {
  if (label == NULL) {
    return;
  }

  printf(" %s=%d", label, (int)value);
}

static void app_can_debug_log_request(uint16_t pid) {
  if (!app_can_debug_enabled) {
    return;
  }
  printf("[can] tx pid=0x%04X\r\n", pid);
}

static void app_can_debug_log_timeout(uint16_t pid) {
  if (!app_can_debug_enabled) {
    return;
  }
  printf("[can] timeout pid=0x%04X\r\n", pid);
}

static void app_can_debug_log_response(uint16_t pid,
                                       const uint8_t *rx_data,
                                       const app_can_dashboard_data_t *data) {
  if ((rx_data == NULL) || (data == NULL)) {
    return;
  }

  if (!app_can_debug_enabled) {
    return;
  }

  printf("[can] rx pid=0x%04X raw=%02X %02X %02X %02X %02X %02X %02X %02X",
         pid,
         rx_data[0],
         rx_data[1],
         rx_data[2],
         rx_data[3],
         rx_data[4],
         rx_data[5],
         rx_data[6],
         rx_data[7]);

  switch (pid) {
  case APP_CAN_PID_PACK_VOLTAGE:
    app_can_print_unsigned_deci_volts("pack_voltage", data->pack_voltage_deci_volts);
    break;

  case APP_CAN_PID_PACK_SUMMED_VOLTAGE:
    app_can_print_unsigned_deci_volts("pack_summed_voltage", data->pack_summed_voltage_deci_volts);
    break;

  case APP_CAN_PID_PACK_CURRENT:
    app_can_print_signed_deci_amps("pack_current", data->pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_low", data->lowest_pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_avg", data->average_pack_current_deci_amps);
    app_can_print_signed_deci_amps("current_high", data->highest_pack_current_deci_amps);
    app_can_print_signed_integer("power_low_kw", data->lowest_pack_power_kw);
    app_can_print_signed_integer("power_avg_kw", data->average_pack_power_kw);
    app_can_print_signed_integer("power_high_kw", data->highest_pack_power_kw);
    break;

  case APP_CAN_PID_PACK_SOC:
    printf(" soc=%u%%", (unsigned int)data->pack_state_of_charge);
    break;

  case APP_CAN_PID_HIGHEST_TEMPERATURE:
    printf(" highest_temp=%dC", (int)data->highest_temperature_c);
    break;

  case APP_CAN_PID_LOWEST_TEMPERATURE:
    printf(" lowest_temp=%dC", (int)data->lowest_temperature_c);
    break;

  case APP_CAN_PID_AVERAGE_TEMPERATURE:
    printf(" average_temp=%dC", (int)data->average_temperature_c);
    break;

  case APP_CAN_PID_LOWEST_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_low", data->lowest_cell_voltage_100uv);
    break;

  case APP_CAN_PID_HIGHEST_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_high", data->highest_cell_voltage_100uv);
    break;

  case APP_CAN_PID_AVERAGE_CELL_VOLTAGE:
    app_can_print_unsigned_100uv_volts("cell_voltage_avg", data->average_cell_voltage_100uv);
    break;

  default:
    break;
  }

  printf(" flags=0x%02X\r\n", data->fault_flags);
}

static bool app_can_enable_gpio_clock(GPIO_TypeDef *port) {
  if (port == GPIOA) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    return true;
  }
  if (port == GPIOB) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    return true;
  }
  if (port == GPIOC) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    return true;
  }
  if (port == GPIOD) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    return true;
  }
  if (port == GPIOE) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
    return true;
  }
  if (port == GPIOF) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
    return true;
  }
  if (port == GPIOG) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
    return true;
  }
  if (port == GPIOH) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
    return true;
  }
  if (port == GPIOI) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
    return true;
  }

  return false;
}

void app_can_set_debug_enabled(bool enabled) {
  app_can_debug_enabled = enabled;
}

bool app_can_is_debug_enabled(void) {
  return app_can_debug_enabled;
}

static bool app_can_config_is_supported(const app_can_config_t *config) {
  if (config == NULL) {
    return false;
  }

  return config->mode == APP_CAN_MODE_CLASSIC;
}

static uint8_t app_can_clamp_percentage(uint8_t value) {
  if (value > 100U) {
    return 100U;
  }

  return value;
}

static int16_t app_can_decode_orion_current_deci_amps(uint16_t raw_value) {
  int32_t current_deci_amps = (int32_t)raw_value - 32767;

  if (current_deci_amps > INT16_MAX) {
    return INT16_MAX;
  }

  if (current_deci_amps < INT16_MIN) {
    return INT16_MIN;
  }

  return (int16_t)current_deci_amps;
}

static void app_can_update_current_stats(app_can_dashboard_data_t *data) {
  int64_t average_deci_amps = 0;

  if (data == NULL) {
    return;
  }

  if (!app_can_current_stats_valid) {
    app_can_current_stats_valid = true;
    app_can_current_min_deci_amps = data->pack_current_deci_amps;
    app_can_current_max_deci_amps = data->pack_current_deci_amps;
    app_can_current_sum_deci_amps = (int64_t)data->pack_current_deci_amps;
    app_can_current_sample_count = 1U;
  } else {
    if (data->pack_current_deci_amps < app_can_current_min_deci_amps) {
      app_can_current_min_deci_amps = data->pack_current_deci_amps;
    }

    if (data->pack_current_deci_amps > app_can_current_max_deci_amps) {
      app_can_current_max_deci_amps = data->pack_current_deci_amps;
    }

    app_can_current_sum_deci_amps += (int64_t)data->pack_current_deci_amps;
    app_can_current_sample_count++;
  }

  average_deci_amps = app_can_current_sum_deci_amps;
  if (average_deci_amps >= 0) {
    average_deci_amps = (average_deci_amps + ((int64_t)app_can_current_sample_count / 2)) /
                        (int64_t)app_can_current_sample_count;
  } else {
    average_deci_amps = (average_deci_amps - ((int64_t)app_can_current_sample_count / 2)) /
                        (int64_t)app_can_current_sample_count;
  }

  data->lowest_pack_current_deci_amps = app_can_current_min_deci_amps;
  data->highest_pack_current_deci_amps = app_can_current_max_deci_amps;
  data->average_pack_current_deci_amps = app_can_clamp_int64_to_int16(average_deci_amps);
}

static void app_can_update_power_stats(app_can_dashboard_data_t *data) {
  uint16_t voltage_deci_volts = 0U;
  int16_t power_kw = 0;
  int64_t average_power_kw = 0;

  if (data == NULL) {
    return;
  }

  voltage_deci_volts = app_can_get_display_voltage_deci_volts(data);
  if (voltage_deci_volts == 0U) {
    return;
  }

  power_kw = app_can_compute_power_kw(data->pack_current_deci_amps, voltage_deci_volts);

  if (!app_can_power_stats_valid) {
    app_can_power_stats_valid = true;
    app_can_power_min_kw = power_kw;
    app_can_power_max_kw = power_kw;
    app_can_power_sum_kw = (int64_t)power_kw;
    app_can_power_sample_count = 1U;
  } else {
    if (power_kw < app_can_power_min_kw) {
      app_can_power_min_kw = power_kw;
    }

    if (power_kw > app_can_power_max_kw) {
      app_can_power_max_kw = power_kw;
    }

    app_can_power_sum_kw += (int64_t)power_kw;
    app_can_power_sample_count++;
  }

  average_power_kw = app_can_power_sum_kw;
  if (average_power_kw >= 0) {
    average_power_kw = (average_power_kw + ((int64_t)app_can_power_sample_count / 2)) /
                       (int64_t)app_can_power_sample_count;
  } else {
    average_power_kw = (average_power_kw - ((int64_t)app_can_power_sample_count / 2)) /
                       (int64_t)app_can_power_sample_count;
  }

  data->lowest_pack_power_kw = app_can_power_min_kw;
  data->highest_pack_power_kw = app_can_power_max_kw;
  data->average_pack_power_kw = app_can_clamp_int64_to_int16(average_power_kw);
}

static uint16_t app_can_get_next_request_pid(void) {
  return app_can_request_table[app_can_next_request].pid;
}

static void app_can_advance_request_sequence(void) {
  app_can_next_request =
      (app_can_request_index_t)((app_can_next_request + 1U) % APP_CAN_REQUEST_COUNT);
}

static uint8_t app_can_dlc_to_bytes(uint32_t data_length_code) {
  switch (data_length_code) {
  case FDCAN_DLC_BYTES_0:
    return 0U;
  case FDCAN_DLC_BYTES_1:
    return 1U;
  case FDCAN_DLC_BYTES_2:
    return 2U;
  case FDCAN_DLC_BYTES_3:
    return 3U;
  case FDCAN_DLC_BYTES_4:
    return 4U;
  case FDCAN_DLC_BYTES_5:
    return 5U;
  case FDCAN_DLC_BYTES_6:
    return 6U;
  case FDCAN_DLC_BYTES_7:
    return 7U;
  case FDCAN_DLC_BYTES_8:
    return 8U;
  case FDCAN_DLC_BYTES_12:
    return 12U;
  case FDCAN_DLC_BYTES_16:
    return 16U;
  case FDCAN_DLC_BYTES_20:
    return 20U;
  case FDCAN_DLC_BYTES_24:
    return 24U;
  case FDCAN_DLC_BYTES_32:
    return 32U;
  case FDCAN_DLC_BYTES_48:
    return 48U;
  case FDCAN_DLC_BYTES_64:
    return 64U;
  default:
    return 0U;
  }
}

static bool app_can_send_pid_request(uint16_t pid) {
  FDCAN_TxHeaderTypeDef tx_header = {0};
  uint8_t tx_data[8] = {0};

  if (!app_can_initialized) {
    return false;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&app_hfdcan) == 0U) {
    return false;
  }

  tx_header.Identifier = APP_CAN_ORION_REQUEST_ID;
  tx_header.IdType = FDCAN_STANDARD_ID;
  tx_header.TxFrameType = FDCAN_DATA_FRAME;
  tx_header.DataLength = FDCAN_DLC_BYTES_8;
  tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  tx_header.BitRateSwitch = FDCAN_BRS_OFF;
  tx_header.FDFormat = FDCAN_CLASSIC_CAN;
  tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  tx_header.MessageMarker = 0U;

  tx_data[0] = 0x03U;
  tx_data[1] = APP_CAN_OBD2_MODE_READ_DATA_BY_IDENTIFIER;
  tx_data[2] = (uint8_t)(pid >> 8U);
  tx_data[3] = (uint8_t)(pid & 0xFFU);

  return HAL_FDCAN_AddMessageToTxFifoQ(&app_hfdcan, &tx_header, tx_data) == HAL_OK;
}

static bool app_can_decode_orion_response(const FDCAN_RxHeaderTypeDef *rx_header,
                                          const uint8_t *rx_data,
                                          app_can_dashboard_data_t *data) {
  uint16_t pid = 0U;
  uint16_t raw_value = 0U;

  if ((rx_header == NULL) || (rx_data == NULL) || (data == NULL)) {
    return false;
  }

  if (rx_header->Identifier != APP_CAN_ORION_RESPONSE_ID) {
    return false;
  }

  if (app_can_dlc_to_bytes(rx_header->DataLength) < 8U) {
    return false;
  }

  if (rx_data[1] != APP_CAN_OBD2_MODE_READ_DATA_RESPONSE) {
    return false;
  }

  pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];

  switch (pid) {
  case APP_CAN_PID_PACK_VOLTAGE:
    if (rx_data[0] < 5U) {
      return false;
    }
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_voltage_deci_volts = raw_value;
    return true;

  case APP_CAN_PID_PACK_SUMMED_VOLTAGE:
    if (rx_data[0] < 5U) {
      return false;
    }
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_summed_voltage_deci_volts = (uint16_t)((raw_value + 5U) / 10U);
    return true;

  case APP_CAN_PID_PACK_CURRENT:
    if (rx_data[0] < 5U) {
      return false;
    }
    raw_value = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    data->pack_current_deci_amps = app_can_decode_orion_current_deci_amps(raw_value);
    app_can_update_current_stats(data);
    app_can_update_power_stats(data);
    return true;

  case APP_CAN_PID_PACK_SOC:
    if (rx_data[0] < 4U) {
      return false;
    }
    data->pack_state_of_charge = app_can_clamp_percentage((uint8_t)((rx_data[4] + 1U) / 2U));
    return true;

  case APP_CAN_PID_HIGHEST_TEMPERATURE:
    if (rx_data[0] < 4U) {
      return false;
    }
    data->highest_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_LOWEST_TEMPERATURE:
    if (rx_data[0] < 4U) {
      return false;
    }
    data->lowest_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_AVERAGE_TEMPERATURE:
    if (rx_data[0] < 4U) {
      return false;
    }
    data->average_temperature_c = (int8_t)rx_data[4];
    return true;

  case APP_CAN_PID_LOWEST_CELL_VOLTAGE:
    if (rx_data[0] < 5U) {
      return false;
    }
    data->lowest_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  case APP_CAN_PID_HIGHEST_CELL_VOLTAGE:
    if (rx_data[0] < 5U) {
      return false;
    }
    data->highest_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  case APP_CAN_PID_AVERAGE_CELL_VOLTAGE:
    if (rx_data[0] < 5U) {
      return false;
    }
    data->average_cell_voltage_100uv = ((uint16_t)rx_data[4] << 8U) | (uint16_t)rx_data[5];
    return true;

  default:
    return false;
  }
}

static bool app_can_handle_rx_message(const FDCAN_RxHeaderTypeDef *rx_header,
                                      const uint8_t *rx_data) {
  app_can_dashboard_data_t next_data = app_can_latest_data;
  uint16_t pid = 0U;

  if (!app_can_decode_orion_response(rx_header, rx_data, &next_data)) {
    return false;
  }

  pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];
  app_can_latest_data = next_data;
  app_can_debug_log_response(pid, rx_data, &app_can_latest_data);
  return true;
}

bool app_can_init(void) {
  const app_can_config_t *config = app_can_config_get();
  FDCAN_FilterTypeDef filter = {0};
  RCC_PeriphCLKInitTypeDef periph_clk = {0};

  if (app_can_initialized) {
    return true;
  }

  if (config == NULL) {
    return app_can_log_init_failure("config-null");
  }

  if (!app_can_config_is_supported(config)) {
    return app_can_log_init_failure("config-unsupported");
  }

  periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
  periph_clk.FdcanClockSelection = RCC_FDCANCLKSOURCE_HSE;
  if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
    return app_can_log_init_failure("fdcan-clock");
  }

  app_hfdcan.Instance = config->instance;
  app_hfdcan.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  app_hfdcan.Init.Mode = FDCAN_MODE_NORMAL;
  app_hfdcan.Init.AutoRetransmission = ENABLE;
  app_hfdcan.Init.TransmitPause = DISABLE;
  app_hfdcan.Init.ProtocolException = ENABLE;
  app_hfdcan.Init.NominalPrescaler = 5U;
  app_hfdcan.Init.NominalSyncJumpWidth = 1U;
  app_hfdcan.Init.NominalTimeSeg1 = 8U;
  app_hfdcan.Init.NominalTimeSeg2 = 1U;
  app_hfdcan.Init.DataPrescaler = 1U;
  app_hfdcan.Init.DataSyncJumpWidth = 1U;
  app_hfdcan.Init.DataTimeSeg1 = 1U;
  app_hfdcan.Init.DataTimeSeg2 = 1U;
  app_hfdcan.Init.MessageRAMOffset = 0U;
  app_hfdcan.Init.StdFiltersNbr = 1U;
  app_hfdcan.Init.ExtFiltersNbr = 0U;
  app_hfdcan.Init.RxFifo0ElmtsNbr = 8U;
  app_hfdcan.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.RxFifo1ElmtsNbr = 0U;
  app_hfdcan.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.RxBuffersNbr = 0U;
  app_hfdcan.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
  app_hfdcan.Init.TxEventsNbr = 0U;
  app_hfdcan.Init.TxBuffersNbr = 0U;
  app_hfdcan.Init.TxFifoQueueElmtsNbr = 1U;
  app_hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  app_hfdcan.Init.TxElmtSize = FDCAN_DATA_BYTES_8;

  if (HAL_FDCAN_Init(&app_hfdcan) != HAL_OK) {
    return app_can_log_init_failure("hal-fdcan-init");
  }

  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0U;
  filter.FilterType = FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = APP_CAN_ORION_RESPONSE_ID;
  filter.FilterID2 = 0x7FFU;

  if (HAL_FDCAN_ConfigFilter(&app_hfdcan, &filter) != HAL_OK) {
    return app_can_log_init_failure("config-filter");
  }

  if (HAL_FDCAN_ConfigGlobalFilter(
          &app_hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) !=
      HAL_OK) {
    return app_can_log_init_failure("global-filter");
  }

  if (HAL_FDCAN_Start(&app_hfdcan) != HAL_OK) {
    return app_can_log_init_failure("fdcan-start");
  }

  app_can_initialized = true;
  app_can_pending_pid = 0U;
  app_can_last_request_ms = 0U;
  app_can_next_request = APP_CAN_REQUEST_PACK_VOLTAGE;
  if (app_can_debug_enabled) {
    printf("[can] init ok fdcan1 request=0x%03X response=0x%03X bitrate=%lu\r\n",
           APP_CAN_ORION_REQUEST_ID,
           APP_CAN_ORION_RESPONSE_ID,
           500000UL);
  }
  return true;
}

bool app_can_poll_dashboard_data(app_can_dashboard_data_t *data) {
  FDCAN_RxHeaderTypeDef rx_header = {0};
  uint8_t rx_data[8] = {0};
  bool updated = false;
  uint32_t now = HAL_GetTick();
  uint16_t request_pid = 0U;

  if ((data == NULL) || !app_can_initialized) {
    return false;
  }

  if ((app_can_pending_pid != 0U) &&
      ((now - app_can_last_request_ms) > APP_CAN_REQUEST_TIMEOUT_MS)) {
    app_can_debug_log_timeout(app_can_pending_pid);
    app_can_pending_pid = 0U;
    app_can_advance_request_sequence();
  }

  while (HAL_FDCAN_GetRxFifoFillLevel(&app_hfdcan, FDCAN_RX_FIFO0) != 0U) {
    if (HAL_FDCAN_GetRxMessage(&app_hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
      break;
    }

    if (app_can_handle_rx_message(&rx_header, rx_data)) {
      updated = true;
      request_pid = ((uint16_t)rx_data[2] << 8U) | (uint16_t)rx_data[3];
      if (request_pid == app_can_pending_pid) {
        app_can_pending_pid = 0U;
        app_can_advance_request_sequence();
      }
    }
  }

  if ((app_can_pending_pid == 0U) &&
      ((now - app_can_last_request_ms) >= APP_CAN_REQUEST_PERIOD_MS)) {
    request_pid = app_can_get_next_request_pid();
    if (app_can_send_pid_request(request_pid)) {
      app_can_debug_log_request(request_pid);
      app_can_pending_pid = request_pid;
      app_can_last_request_ms = now;
    }
  }

  if (updated) {
    *data = app_can_latest_data;
    return true;
  }

  return false;
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan) {
  GPIO_InitTypeDef gpio = {0};
  const app_can_config_t *config = app_can_config_get();

  if ((hfdcan == NULL) || (config == NULL) || (hfdcan->Instance != config->instance)) {
    return;
  }

  if (!app_can_enable_gpio_clock(config->rx_pin.port) ||
      !app_can_enable_gpio_clock(config->tx_pin.port)) {
    return;
  }

  if (config->instance == FDCAN1) {
    __HAL_RCC_FDCAN_CLK_ENABLE();
  }

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = (uint8_t)config->rx_pin.alternate_function;
  gpio.Pin = config->rx_pin.pin;
  HAL_GPIO_Init(config->rx_pin.port, &gpio);

  gpio.Alternate = (uint8_t)config->tx_pin.alternate_function;
  gpio.Pin = config->tx_pin.pin;
  HAL_GPIO_Init(config->tx_pin.port, &gpio);
}

void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef *hfdcan) {
  const app_can_config_t *config = app_can_config_get();

  if ((hfdcan == NULL) || (config == NULL) || (hfdcan->Instance != config->instance)) {
    return;
  }

  HAL_GPIO_DeInit(config->rx_pin.port, config->rx_pin.pin);
  HAL_GPIO_DeInit(config->tx_pin.port, config->tx_pin.pin);

  if (config->instance == FDCAN1) {
    __HAL_RCC_FDCAN_CLK_DISABLE();
  }
}
