#ifndef APP_CAN_CONFIG_H
#define APP_CAN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef enum {
  APP_CAN_MODE_CLASSIC = 0,
  APP_CAN_MODE_FD,
} app_can_mode_t;

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  uint32_t alternate_function;
} app_can_pin_config_t;

typedef struct {
  FDCAN_GlobalTypeDef *instance;
  app_can_pin_config_t rx_pin;
  app_can_pin_config_t tx_pin;
  app_can_mode_t mode;
} app_can_config_t;

const app_can_config_t *app_can_config_get(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_CONFIG_H */
