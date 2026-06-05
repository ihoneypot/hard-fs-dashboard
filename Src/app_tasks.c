#include "app_tasks.h"

#include <stdio.h>
#include <string.h>

#include "app_can.h"
#include "app_data_log.h"
#include "app_emmc_log.h"
#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "lvgl/lvgl.h"
#include "lvgl_port_lcd.h"
#include "lvgl_port_touchpad.h"
#include "ui/screens/ui_MainScreen.h"
#include "ui/ui.h"

static QueueHandle_t app_ui_queue = NULL;

static const app_can_dashboard_data_t app_default_dashboard_data = {
    .pack_voltage_deci_volts = 0U,
    .pack_summed_voltage_deci_volts = 0U,
    .lowest_cell_voltage_100uv = 0U,
    .average_cell_voltage_100uv = 0U,
    .highest_cell_voltage_100uv = 0U,
    .pack_current_deci_amps = 0,
    .lowest_pack_current_deci_amps = 0,
    .average_pack_current_deci_amps = 0,
    .highest_pack_current_deci_amps = 0,
    .lowest_pack_power_kw = 0,
    .average_pack_power_kw = 0,
    .highest_pack_power_kw = 0,
    .pack_state_of_charge = 0U,
    .highest_temperature_c = 0,
    .average_temperature_c = 0,
    .lowest_temperature_c = 0,
    .fault_flags = 0U,
};

#define APP_GUI_TASK_STACK_WORDS 2048U
#define APP_GUI_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_GUI_TASK_PERIOD_MS 5U

#define APP_CAN_TASK_STACK_WORDS 1024U
#define APP_CAN_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)
#define APP_CAN_TASK_POLL_MS 10U
#define APP_CAN_RETRY_MS 1000U

#define APP_CONSOLE_TASK_STACK_WORDS 1024U
#define APP_CONSOLE_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define APP_CONSOLE_TASK_POLL_MS 5U
#define APP_CONSOLE_UART_RX_TIMEOUT_MS 50U

/*
 * These inputs do not have their final transport yet:
 * - speed / drive mode will later come from a separate CAN device
 * - IMD / FAULT / RTD / TSAL / BSPD / SDC / READY TO DRIVE will later come
 *   from digital inputs
 *
 * For now keep the UI wiring and behavior in place with placeholder data.
 */
#define APP_AUX_CAN_PLACEHOLDER_UPDATE_MS 200U
#define APP_DRIVE_MODE_PLACEHOLDER_STEP_MS 5000U
#define APP_STATUS_BOOT_DISPLAY_MS 3000U
#define APP_STATUS_OK_COLOR 0x52BA00U
#define APP_STATUS_FAULT_COLOR 0xF40D0DU

typedef enum {
  APP_DRIVE_MODE_NEUTRAL = 0,
  APP_DRIVE_MODE_DRIVE,
  APP_DRIVE_MODE_REVERSE,
  APP_DRIVE_MODE_COUNT
} app_drive_mode_t;

typedef enum {
  APP_STATUS_SIGNAL_IMD = 0,
  APP_STATUS_SIGNAL_FAULT,
  APP_STATUS_SIGNAL_RTD,
  APP_STATUS_SIGNAL_TSAL,
  APP_STATUS_SIGNAL_BSPD,
  APP_STATUS_SIGNAL_SDC,
  APP_STATUS_SIGNAL_READY_TO_DRIVE,
  APP_STATUS_SIGNAL_COUNT
} app_status_signal_id_t;

typedef struct {
  bool fault_active;
} app_status_signal_state_t;

typedef struct {
  uint16_t speed_kph;
  app_drive_mode_t drive_mode;
  bool speed_valid;
  bool drive_mode_valid;
  bool status_inputs_initialized;
  app_status_signal_state_t status_signals[APP_STATUS_SIGNAL_COUNT];
} app_aux_ui_state_t;

static int32_t app_ui_abs_int32(int32_t value) {
  return (value < 0) ? -value : value;
}

static int32_t app_ui_slider_from_temperature(int8_t temperature_c) {
  if (temperature_c < 1) {
    return 1;
  }

  if (temperature_c > 100) {
    return 100;
  }

  return temperature_c;
}

static int32_t app_ui_slider_from_current(int16_t current_deci_amps) {
  int32_t current_amps = (app_ui_abs_int32(current_deci_amps) + 5) / 10;

  if (current_amps < 1) {
    return 1;
  }

  if (current_amps > 100) {
    return 100;
  }

  return current_amps;
}

static void app_ui_set_voltage_mv_label(lv_obj_t *label, uint16_t voltage_100uv) {
  uint16_t millivolts = 0U;

  if (label == NULL) {
    return;
  }

  millivolts = (uint16_t)((voltage_100uv + 5U) / 10U);
  lv_label_set_text_fmt(label, "%u", (unsigned int)millivolts);
}

static void app_ui_set_signed_integer_label(lv_obj_t *label, int16_t value) {
  if (label == NULL) {
    return;
  }

  lv_label_set_text_fmt(label, "%d", (int)value);
}

static uint32_t app_ui_calculate_usage_percent(uint32_t used, uint32_t total) {
  if (total == 0U) {
    return 0U;
  }

  return (used * 100U + (total / 2U)) / total;
}

static lv_obj_t *app_ui_get_status_panel(app_status_signal_id_t signal_id) {
  switch (signal_id) {
  case APP_STATUS_SIGNAL_IMD:
    return ui_panelIMD;
  case APP_STATUS_SIGNAL_FAULT:
    return ui_panelFault;
  case APP_STATUS_SIGNAL_RTD:
    return ui_panelRTD;
  case APP_STATUS_SIGNAL_TSAL:
    return ui_panelTSAL;
  case APP_STATUS_SIGNAL_BSPD:
    return ui_panelBSPD;
  case APP_STATUS_SIGNAL_SDC:
    return ui_panelSDC;
  case APP_STATUS_SIGNAL_READY_TO_DRIVE:
    return ui_panleReadyToDrive;
  case APP_STATUS_SIGNAL_COUNT:
  default:
    return NULL;
  }
}

static lv_obj_t *app_ui_get_status_label(app_status_signal_id_t signal_id) {
  switch (signal_id) {
  case APP_STATUS_SIGNAL_IMD:
    return ui_labelIMD;
  case APP_STATUS_SIGNAL_FAULT:
    return ui_labelFault;
  case APP_STATUS_SIGNAL_RTD:
    return ui_labelRTD;
  case APP_STATUS_SIGNAL_TSAL:
    return ui_labelTSAL;
  case APP_STATUS_SIGNAL_BSPD:
    return ui_labelBSPD;
  case APP_STATUS_SIGNAL_SDC:
    return ui_labelSDC;
  case APP_STATUS_SIGNAL_READY_TO_DRIVE:
    return ui_labelReadyToDrive;
  case APP_STATUS_SIGNAL_COUNT:
  default:
    return NULL;
  }
}

static void app_ui_set_object_hidden(lv_obj_t *obj, bool hidden) {
  if (obj == NULL) {
    return;
  }

  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

static void app_ui_set_status_indicator_color(app_status_signal_id_t signal_id, lv_color_t color) {
  lv_obj_t *panel = app_ui_get_status_panel(signal_id);
  lv_obj_t *label = app_ui_get_status_label(signal_id);

  if (panel != NULL) {
    lv_obj_set_style_border_color(panel, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (label != NULL) {
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

static const char *app_ui_drive_mode_to_text(app_drive_mode_t drive_mode) {
  switch (drive_mode) {
  case APP_DRIVE_MODE_NEUTRAL:
    return "N";
  case APP_DRIVE_MODE_DRIVE:
    return "D";
  case APP_DRIVE_MODE_REVERSE:
    return "R";
  case APP_DRIVE_MODE_COUNT:
  default:
    return "-";
  }
}

static void app_ui_update_aux_placeholders(app_aux_ui_state_t *state, uint32_t now_ms) {
  static uint32_t last_speed_update_ms = 0U;
  static uint32_t last_drive_mode_update_ms = 0U;
  static int32_t speed_value = 0;
  static int32_t speed_direction = 1;

  if (state == NULL) {
    return;
  }

  if ((now_ms - last_speed_update_ms) >= APP_AUX_CAN_PLACEHOLDER_UPDATE_MS) {
    speed_value += (speed_direction * 3);
    if (speed_value >= 160) {
      speed_value = 160;
      speed_direction = -1;
    } else if (speed_value <= 0) {
      speed_value = 0;
      speed_direction = 1;
    }

    state->speed_kph = (uint16_t)speed_value;
    state->speed_valid = true;
    last_speed_update_ms = now_ms;
  }

  if (!state->drive_mode_valid) {
    state->drive_mode = APP_DRIVE_MODE_NEUTRAL;
    state->drive_mode_valid = true;
    last_drive_mode_update_ms = now_ms;
  } else if ((now_ms - last_drive_mode_update_ms) >= APP_DRIVE_MODE_PLACEHOLDER_STEP_MS) {
    state->drive_mode =
        (app_drive_mode_t)(((uint32_t)state->drive_mode + 1U) % (uint32_t)APP_DRIVE_MODE_COUNT);
    last_drive_mode_update_ms = now_ms;
  }

  /*
   * No GPIO map is defined yet, so keep all digital statuses in the healthy
   * state. Once the real pins are known this function is the single place to
   * replace with GPIO reads and final polarity rules.
   */
  state->status_inputs_initialized = (now_ms >= APP_STATUS_BOOT_DISPLAY_MS);
  for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
    state->status_signals[i].fault_active = false;
  }
}

static void app_ui_apply_aux_state(const app_aux_ui_state_t *state, uint32_t now_ms) {
  bool show_status_indicators = false;

  if (state == NULL) {
    return;
  }

  if (state->speed_valid && (ui_labelSpeed != NULL)) {
    lv_label_set_text_fmt(ui_labelSpeed, "%u", (unsigned int)state->speed_kph);
  }

  if (state->drive_mode_valid && (ui_labelDriveMode != NULL)) {
    lv_label_set_text(ui_labelDriveMode, app_ui_drive_mode_to_text(state->drive_mode));
  }

  if (ui_panelDriveMode != NULL) {
    lv_obj_set_style_border_color(
        ui_panelDriveMode, lv_color_hex(APP_STATUS_OK_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (ui_labelDriveMode != NULL) {
    lv_obj_set_style_text_color(
        ui_labelDriveMode, lv_color_hex(APP_STATUS_OK_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
  }

  if (!state->status_inputs_initialized || (now_ms < APP_STATUS_BOOT_DISPLAY_MS)) {
    show_status_indicators = true;
  } else {
    for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
      if (state->status_signals[i].fault_active) {
        show_status_indicators = true;
        break;
      }
    }
  }

  for (uint32_t i = 0U; i < (uint32_t)APP_STATUS_SIGNAL_COUNT; ++i) {
    lv_color_t color = state->status_signals[i].fault_active ? lv_color_hex(APP_STATUS_FAULT_COLOR)
                                                             : lv_color_hex(APP_STATUS_OK_COLOR);
    app_status_signal_id_t signal_id = (app_status_signal_id_t)i;

    app_ui_set_status_indicator_color(signal_id, color);
    app_ui_set_object_hidden(app_ui_get_status_panel(signal_id), !show_status_indicators);
    app_ui_set_object_hidden(app_ui_get_status_label(signal_id), !show_status_indicators);
  }
}

static void app_ui_apply_dashboard_data(const app_can_dashboard_data_t *data) {
  app_emmc_log_status_t emmc_status = {0};
  uint32_t ram_usage_percent = 0U;
  uint32_t emmc_used_bytes = 0U;
  uint32_t emmc_total_bytes = 0U;
  uint32_t emmc_usage_percent = 0U;

  if (data == NULL) {
    return;
  }

  ram_usage_percent =
      app_ui_calculate_usage_percent(app_data_log_get_count(), app_data_log_get_capacity());
  app_emmc_log_get_status(&emmc_status);
  if (emmc_status.reserved_blocks > 1U) {
    emmc_used_bytes = (emmc_status.written_blocks * 512U) + emmc_status.staged_bytes;
    emmc_total_bytes = (emmc_status.reserved_blocks - 1U) * 512U;
    emmc_usage_percent = app_ui_calculate_usage_percent(emmc_used_bytes, emmc_total_bytes);
  }

  if (ui_labelBatteryTempHigh != NULL) {
    lv_label_set_text_fmt(ui_labelBatteryTempHigh, "%d", (int)data->highest_temperature_c);
  }

  if (ui_labelBatteryTempAvg != NULL) {
    lv_label_set_text_fmt(ui_labelBatteryTempAvg, "%d", (int)data->average_temperature_c);
  }

  if (ui_labelBatteryTempLow != NULL) {
    lv_label_set_text_fmt(ui_labelBatteryTempLow, "%d", (int)data->lowest_temperature_c);
  }

  app_ui_set_voltage_mv_label(ui_labelBatteryVoltLow, data->lowest_cell_voltage_100uv);
  app_ui_set_voltage_mv_label(ui_labelBatteryVoltAvg, data->average_cell_voltage_100uv);
  app_ui_set_voltage_mv_label(ui_labelBatteryVoltHigh, data->highest_cell_voltage_100uv);

  app_ui_set_signed_integer_label(ui_labelBatteryPowerLow, data->lowest_pack_power_kw);
  app_ui_set_signed_integer_label(ui_labelBatteryPowerAvg, data->average_pack_power_kw);
  app_ui_set_signed_integer_label(ui_labelBatteryPowerHigh, data->highest_pack_power_kw);

  if (ui_labelDebugSpeedHigh != NULL) {
    lv_label_set_text_fmt(ui_labelDebugSpeedHigh, "%lu%%", (unsigned long)ram_usage_percent);
  }

  if (ui_labelDebugSpeedAvg != NULL) {
    lv_label_set_text_fmt(ui_labelDebugSpeedAvg, "%lu%%", (unsigned long)emmc_usage_percent);
  }

  ui_set_batt_temp_slider_value(app_ui_slider_from_temperature(data->highest_temperature_c));
  ui_set_power_consumption_slider_value(app_ui_slider_from_current(data->pack_current_deci_amps));
  ui_set_batt_level_slider_value((int32_t)data->pack_state_of_charge);
}

static void app_console_print_help(void) {
  printf("[console] commands: h=help i=ram-log-info d=ram-dump c=ram-clear "
         "e=emmc-info p=emmc-dump-csv b=script-bin-dump x=emmc-reset "
         "f=emmc-flush v=toggle-can-debug\r\n");
}

static void app_console_print_log_info(void) {
  printf("[console] log count=%lu capacity=%lu wrapped=%u\r\n",
         (unsigned long)app_data_log_get_count(),
         (unsigned long)app_data_log_get_capacity(),
         app_data_log_is_wrapped() ? 1U : 0U);
}

static void app_console_print_emmc_log_info(void) {
  app_emmc_log_status_t status = {0};

  app_emmc_log_get_status(&status);
  printf("[console] emmc ready=%u session=%lu records=%lu blocks=%lu/%lu overflow=%u\r\n",
         status.ready ? 1U : 0U,
         (unsigned long)status.session_id,
         (unsigned long)status.record_count,
         (unsigned long)status.written_blocks,
         (unsigned long)status.reserved_blocks,
         status.overflow ? 1U : 0U);
}

static void app_console_handle_command(uint8_t command) {
  bool restore_can_debug = false;
  bool can_debug_was_enabled = false;

  switch (command) {
  case 'h':
  case 'H':
  case '?':
    app_console_print_help();
    break;

  case 'i':
  case 'I':
    app_console_print_log_info();
    break;

  case 'd':
  case 'D':
    printf("[console] dump begin\r\n");
    app_data_log_dump_csv();
    printf("[console] dump end\r\n");
    break;

  case 'c':
  case 'C':
    app_data_log_reset();
    printf("[console] log cleared\r\n");
    break;

  case 'e':
  case 'E':
    app_console_print_emmc_log_info();
    break;

  case 'f':
  case 'F':
    if (app_emmc_log_flush()) {
      printf("[console] emmc flush ok\r\n");
    } else {
      printf("[console] emmc flush failed\r\n");
    }
    break;

  case 'x':
  case 'X':
    if (app_emmc_log_reset_session()) {
      printf("[console] emmc session reset\r\n");
      app_console_print_emmc_log_info();
    } else {
      printf("[console] emmc session reset failed\r\n");
    }
    break;

  case 'p':
  case 'P':
    can_debug_was_enabled = app_can_is_debug_enabled();
    if (can_debug_was_enabled) {
      app_can_set_debug_enabled(false);
      restore_can_debug = true;
    }
    printf("[console] emmc dump begin\r\n");
    app_emmc_log_dump_csv();
    printf("[console] emmc dump end\r\n");
    if (restore_can_debug) {
      app_can_set_debug_enabled(true);
    }
    break;

  case 'b':
  case 'B':
    can_debug_was_enabled = app_can_is_debug_enabled();
    if (can_debug_was_enabled) {
      app_can_set_debug_enabled(false);
      restore_can_debug = true;
    }
    (void)app_emmc_log_dump_binary();
    if (restore_can_debug) {
      app_can_set_debug_enabled(true);
    }
    break;

  case 'v':
  case 'V':
    app_can_set_debug_enabled(!app_can_is_debug_enabled());
    printf("[console] can debug=%u\r\n", app_can_is_debug_enabled() ? 1U : 0U);
    break;

  case '\r':
  case '\n':
    break;

  default:
    printf("[console] unknown command '%c'\r\n", (char)command);
    app_console_print_help();
    break;
  }
}

static void app_gui_task(void *argument) {
  app_aux_ui_state_t aux_state = {0};
  app_aux_ui_state_t last_applied_aux_state = {0};
  app_can_dashboard_data_t data = app_default_dashboard_data;
  TickType_t last_wake = xTaskGetTickCount();
  bool aux_state_applied = false;
  uint32_t now_ms = 0U;

  (void)argument;

  lv_init();
  LCD_init();
  touchpad_init();
  ui_init();
  app_ui_apply_dashboard_data(&data);
  app_ui_update_aux_placeholders(&aux_state, 0U);
  app_ui_apply_aux_state(&aux_state, 0U);
  last_applied_aux_state = aux_state;
  aux_state_applied = true;

  for (;;) {
    while (xQueueReceive(app_ui_queue, &data, 0U) == pdPASS) {
      app_ui_apply_dashboard_data(&data);
    }

    now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    app_ui_update_aux_placeholders(&aux_state, now_ms);
    if (!aux_state_applied ||
        (memcmp(&aux_state, &last_applied_aux_state, sizeof(aux_state)) != 0)) {
      app_ui_apply_aux_state(&aux_state, now_ms);
      last_applied_aux_state = aux_state;
      aux_state_applied = true;
    }

    lv_tick_inc(APP_GUI_TASK_PERIOD_MS);
    lv_timer_handler();
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_GUI_TASK_PERIOD_MS));
  }
}

static void app_can_rx_task(void *argument) {
  app_can_dashboard_data_t data = {0};
  bool can_ready = false;
  uint32_t timestamp_ms = 0U;
  bool init_attempt_logged = false;

  (void)argument;

  if (app_can_is_debug_enabled()) {
    printf("[can] task start\r\n");
  }

  for (;;) {
    if (!can_ready) {
      if (app_can_is_debug_enabled() && !init_attempt_logged) {
        printf("[can] init attempt\r\n");
        init_attempt_logged = true;
      }
      can_ready = app_can_init();
      if (!can_ready) {
        vTaskDelay(pdMS_TO_TICKS(APP_CAN_RETRY_MS));
        continue;
      }

      init_attempt_logged = false;
    }

    if (app_can_poll_dashboard_data(&data)) {
      timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
      app_data_log_record(timestamp_ms, &data);
      (void)app_emmc_log_append(timestamp_ms, &data);
      (void)xQueueOverwrite(app_ui_queue, &data);
    }

    vTaskDelay(pdMS_TO_TICKS(APP_CAN_TASK_POLL_MS));
  }
}

static void app_console_task(void *argument) {
  uint8_t command = 0U;
  HAL_StatusTypeDef uart_status = HAL_OK;

  (void)argument;

  printf("[console] task start\r\n");
  app_console_print_help();
  app_console_print_log_info();
  app_console_print_emmc_log_info();

  for (;;) {
    uart_status = HAL_UART_Receive(&hcom_uart[COM1], &command, 1U, APP_CONSOLE_UART_RX_TIMEOUT_MS);
    if (uart_status == HAL_OK) {
      if ((command != 'b') && (command != 'B')) {
        printf("[console] rx=0x%02X '%c'\r\n",
               (unsigned int)command,
               ((command >= 0x20U) && (command <= 0x7EU)) ? (char)command : '.');
      }
      app_console_handle_command(command);
    }

    vTaskDelay(pdMS_TO_TICKS(APP_CONSOLE_TASK_POLL_MS));
  }
}

bool app_tasks_start(void) {
  app_data_log_init();
  (void)app_emmc_log_init();

  app_ui_queue = xQueueCreate(1U, sizeof(app_can_dashboard_data_t));
  if (app_ui_queue == NULL) {
    return false;
  }

  if (xTaskCreate(
          app_gui_task, "gui", APP_GUI_TASK_STACK_WORDS, NULL, APP_GUI_TASK_PRIORITY, NULL) !=
      pdPASS) {
    return false;
  }

  if (xTaskCreate(
          app_can_rx_task, "can_rx", APP_CAN_TASK_STACK_WORDS, NULL, APP_CAN_TASK_PRIORITY, NULL) !=
      pdPASS) {
    return false;
  }

  if (xTaskCreate(app_console_task,
                  "console",
                  APP_CONSOLE_TASK_STACK_WORDS,
                  NULL,
                  APP_CONSOLE_TASK_PRIORITY,
                  NULL) != pdPASS) {
    return false;
  }

  return true;
}
