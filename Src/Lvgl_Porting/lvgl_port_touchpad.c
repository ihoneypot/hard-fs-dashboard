#include "lvgl_port_touchpad.h"
#include "lvgl/lvgl.h"

#include "stm32h745i_discovery.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_ts.h"

#define TS_INSTANCE (0U)

static TS_State_t ts_state;
static lv_indev_drv_t indev_drv;
static lv_indev_t *touch_indev = NULL;

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    LV_UNUSED(drv);

    BSP_TS_GetState(TS_INSTANCE, &ts_state);
    if(ts_state.TouchDetected) {
        data->point.x = ts_state.TouchX;
        data->point.y = ts_state.TouchY;
        last_x = data->point.x;
        last_y = data->point.y;
        data->state = LV_INDEV_STATE_PR;
    }
    else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;
    }
}

void touchpad_init(void)
{
    TS_Init_t ts_init;

    ts_init.Width = LCD_DEFAULT_WIDTH;
    ts_init.Height = LCD_DEFAULT_HEIGHT;
    ts_init.Orientation = TS_SWAP_XY;
    ts_init.Accuracy = 5;

    if(BSP_TS_Init(TS_INSTANCE, &ts_init) != BSP_ERROR_NONE) {
        return;
    }

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    touch_indev = lv_indev_drv_register(&indev_drv);
    LV_UNUSED(touch_indev);
}
