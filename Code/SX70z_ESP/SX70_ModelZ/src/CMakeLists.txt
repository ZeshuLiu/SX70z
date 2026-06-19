# 外设/控制代码组件
idf_component_register(SRCS "shutter_cal.c" "gpio_inputs.c" "display_manager.c" "fonts/font5x8.c" "fonts/font6x12.c" "fonts/font8x16.c" "fonts/flash_14_14.c" "ssd1306.c" "pcf8575.c" "opt4001.c" "devinfo.c" "camera_main.c" "ota_web.c" "pin_init.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server app_update driver esp_timer esp_driver_gptimer esp_lcd nvs_flash)
