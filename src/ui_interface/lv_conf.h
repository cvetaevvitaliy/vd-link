/**
 * @file lv_conf.h
 * Configuration file for LVGL v8.3
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* DRM Display Buffer Resolution */
#define LV_HOR_RES_MAX          1280
#define LV_VER_RES_MAX          720

/* Color depth */
#define LV_COLOR_DEPTH          32

/* Color format settings for 32-bit ARGB */
#define LV_COLOR_16_SWAP        0
#define LV_COLOR_SCREEN_TRANSP  1
#define LV_COLOR_MIX_ROUND_OFS  128

/* Display buffer settings */
#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 16

/* Dot Per Inch: used to initialize default sizes */
#define LV_DPI_DEF              130

/* Memory manager settings */
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (128U * 1024U)

/* Enable GPU */
#define LV_USE_GPU_ARM2D        0
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

/* Default theme */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   0

/* Enable global alpha blending */
#define LV_USE_DRAW_MASKS       1

/* Enable blending and transparency */
#define LV_USE_BLEND_MODES      1
#define LV_DRAW_COMPLEX         1

/* Font settings */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1

/* Animation settings */
#define LV_USE_ANIMATION        1

/* Performance optimizations */
#define LV_DISP_ROT_MAX_BUF     1
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_REFR_DEBUG       0

/* Reduce unnecessary invalidations */
#define LV_USE_USER_DATA        1
#define LV_ATTRIBUTE_FLUSH_READY

/* Optimize drawing */
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0

/* Additional LVGL modules */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_USE_IMGFONT        1

#endif /* LV_CONF_H */
