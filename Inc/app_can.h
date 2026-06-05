#ifndef APP_CAN_H
#define APP_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t pack_voltage_deci_volts;
  uint16_t pack_summed_voltage_deci_volts;
  uint16_t lowest_cell_voltage_100uv;
  uint16_t average_cell_voltage_100uv;
  uint16_t highest_cell_voltage_100uv;
  int16_t pack_current_deci_amps;
  int16_t lowest_pack_current_deci_amps;
  int16_t average_pack_current_deci_amps;
  int16_t highest_pack_current_deci_amps;
  int16_t lowest_pack_power_kw;
  int16_t average_pack_power_kw;
  int16_t highest_pack_power_kw;
  uint8_t pack_state_of_charge;
  int8_t highest_temperature_c;
  int8_t average_temperature_c;
  int8_t lowest_temperature_c;
  uint8_t fault_flags;
} app_can_dashboard_data_t;

bool app_can_init(void);
bool app_can_poll_dashboard_data(app_can_dashboard_data_t *data);
void app_can_set_debug_enabled(bool enabled);
bool app_can_is_debug_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_H */
