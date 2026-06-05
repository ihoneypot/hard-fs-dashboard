#include "lvgl_port_lcd.h"

#include "main.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_sdram.h"
#include "lvgl/lvgl.h"

#include <string.h>

static LTDC_HandleTypeDef hltdc;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_disp_t *display;
static lv_color_t *const framebuffer_1 = (lv_color_t *)LCD_LAYER_0_ADDRESS;
static lv_color_t *const framebuffer_2 = (lv_color_t *)LCD_LAYER_1_ADDRESS;

static void lcd_error(void) {
  Error_Handler();
}

static void lcd_panel_prepare(void) {
  GPIO_InitTypeDef gpio = {0};

  LCD_DISP_EN_GPIO_CLK_ENABLE();
  LCD_BL_CTRL_GPIO_CLK_ENABLE();
  LCD_DISPLAY_MODE_GPIO_CLK_ENABLE();
  LCD_RESET_GPIO_CLK_ENABLE();

  gpio.Pin = LCD_DISP_EN_Pin | LCD_BL_CTRL_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_DISP_EN_GPIO_Port, &gpio);

  gpio.Pin = LCD_DISPLAY_MODE_Pin;
  HAL_GPIO_Init(LCD_DISPLAY_MODE_GPIO_Port, &gpio);

  gpio.Pin = LCD_RESET_Pin;
  HAL_GPIO_Init(LCD_RESET_GPIO_Port, &gpio);

  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_DISP_EN_GPIO_Port, LCD_DISP_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_DISPLAY_MODE_GPIO_Port, LCD_DISPLAY_MODE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
}

static void lcd_panel_power_on(void) {
  HAL_GPIO_WritePin(LCD_DISP_EN_GPIO_Port, LCD_DISP_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(20);

  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(120);
}

static void lcd_backlight_on(void) {
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
}

static void lcd_ltdc_msp_init(void) {
  GPIO_InitTypeDef gpio = {0};
  RCC_PeriphCLKInitTypeDef periph_clk = {0};

  periph_clk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  periph_clk.PLL3.PLL3M = 5;
  periph_clk.PLL3.PLL3N = 160;
  periph_clk.PLL3.PLL3P = 2;
  periph_clk.PLL3.PLL3Q = 2;
  periph_clk.PLL3.PLL3R = 83;
  periph_clk.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
  periph_clk.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  periph_clk.PLL3.PLL3FRACN = 0;

  if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
    lcd_error();
  }

  __HAL_RCC_LTDC_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_3 | GPIO_PIN_7 | GPIO_PIN_2;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOK, &gpio);

  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_0 | GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOI, &gpio);

  gpio.Pin = GPIO_PIN_15 | GPIO_PIN_14 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_11 | GPIO_PIN_10 |
             GPIO_PIN_9 | GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_1 |
             GPIO_PIN_5 | GPIO_PIN_3 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOJ, &gpio);

  gpio.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOH, &gpio);

  __HAL_RCC_LTDC_FORCE_RESET();
  __HAL_RCC_LTDC_RELEASE_RESET();
}

static void lcd_ltdc_init(void) {
  LTDC_LayerCfgTypeDef layer = {0};

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = RK043FN48H_HSYNC - 1U;
  hltdc.Init.AccumulatedHBP = RK043FN48H_HSYNC + (RK043FN48H_HBP - 11U) - 1U;
  hltdc.Init.AccumulatedActiveW = RK043FN48H_HSYNC + LCD_DEFAULT_WIDTH + RK043FN48H_HBP - 1U;
  hltdc.Init.TotalWidth =
      RK043FN48H_HSYNC + LCD_DEFAULT_WIDTH + (RK043FN48H_HBP - 11U) + RK043FN48H_HFP - 1U;
  hltdc.Init.VerticalSync = RK043FN48H_VSYNC - 1U;
  hltdc.Init.AccumulatedVBP = RK043FN48H_VSYNC + RK043FN48H_VBP - 1U;
  hltdc.Init.AccumulatedActiveH = RK043FN48H_VSYNC + LCD_DEFAULT_HEIGHT + RK043FN48H_VBP - 1U;
  hltdc.Init.TotalHeigh =
      RK043FN48H_VSYNC + LCD_DEFAULT_HEIGHT + RK043FN48H_VBP + RK043FN48H_VFP - 1U;
  hltdc.Init.Backcolor.Blue = 0x00;
  hltdc.Init.Backcolor.Green = 0x00;
  hltdc.Init.Backcolor.Red = 0x00;

  if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
    lcd_error();
  }

  layer.WindowX0 = 0;
  layer.WindowX1 = LCD_DEFAULT_WIDTH;
  layer.WindowY0 = 0;
  layer.WindowY1 = LCD_DEFAULT_HEIGHT;
  layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  layer.Alpha = 255;
  layer.Alpha0 = 0;
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layer.FBStartAdress = LCD_LAYER_0_ADDRESS;
  layer.ImageWidth = LCD_DEFAULT_WIDTH;
  layer.ImageHeight = LCD_DEFAULT_HEIGHT;
  layer.Backcolor.Blue = 0;
  layer.Backcolor.Green = 0;
  layer.Backcolor.Red = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) != HAL_OK) {
    lcd_error();
  }
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  LV_UNUSED(area);

  SCB_CleanInvalidateDCache();

  if (HAL_LTDC_SetAddress(&hltdc, (uint32_t)color_p, 0) != HAL_OK) {
    lcd_error();
  }

  lv_disp_flush_ready(drv);
}

void LCD_init(void) {
  if (display != NULL) {
    lcd_error();
  }

  if (BSP_SDRAM_Init(0) != BSP_ERROR_NONE) {
    lcd_error();
  }

  lcd_panel_prepare();
  lcd_ltdc_msp_init();
  lcd_panel_power_on();
  lcd_ltdc_init();

  memset(framebuffer_1, 0, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(lv_color_t));
  memset(framebuffer_2, 0, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(lv_color_t));
  SCB_CleanInvalidateDCache();

  lv_disp_draw_buf_init(
      &draw_buf, framebuffer_1, framebuffer_2, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_DEFAULT_WIDTH;
  disp_drv.ver_res = LCD_DEFAULT_HEIGHT;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.full_refresh = 1;
  display = lv_disp_drv_register(&disp_drv);

  if (display == NULL) {
    lcd_error();
  }

  lcd_backlight_on();
}
