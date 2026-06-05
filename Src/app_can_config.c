#include "app_can_config.h"

static const app_can_config_t app_can_config = {
    .instance = FDCAN1,
    /*
     * FDCAN routing:
     *   D16 -> PH13 -> FDCAN1_TX
     *   B17 -> PH14 -> FDCAN1_RX
     */
    .rx_pin =
        {
            .port = GPIOH,
            .pin = GPIO_PIN_14,
            .alternate_function = GPIO_AF9_FDCAN1,
        },
    .tx_pin =
        {
            .port = GPIOH,
            .pin = GPIO_PIN_13,
            .alternate_function = GPIO_AF9_FDCAN1,
        },
    .mode = APP_CAN_MODE_CLASSIC,
};

const app_can_config_t *app_can_config_get(void) {
  return &app_can_config;
}
