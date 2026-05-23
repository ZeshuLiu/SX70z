#pragma once

// I2c部分引脚定义
#define SCL0_PIN GPIO_NUM_21
#define SDA0_PIN GPIO_NUM_22

#define SCL1_PIN GPIO_NUM_8
#define SDA1_PIN GPIO_NUM_7

// #define SCL1_PIN GPIO_NUM_12 // 这个只用于开发板测试， 实际需要用上面注释掉的引脚 7 和 8
// #define SDA1_PIN GPIO_NUM_13 // 这个只用于开发板测试， 实际需要用上面注释掉的引脚 7 和 8

// 外设（输出）引脚定义
#define LED1_PIN GPIO_NUM_20 // 板载LED灯 低电平点亮

#define LED_TEST_PIN GPIO_NUM_2 // 开发板LED灯，实际系统里面没有 低电平点亮

// 相机机身（输入）引脚定义
#define S1T_PIN GPIO_NUM_14 // 机身快门按钮 低电平有效，外部拉高

#define S2_PIN GPIO_NUM_4   // 闪光灯检测引脚 低电平表示有闪光灯，初始化时需要芯片拉高
#define S3_PIN GPIO_NUM_34  // 机身反光板位置监测1 有外部拉高
#define S5_PIN GPIO_NUM_35  // 机身反光板位置监测2 有外部拉高

// 相机机身（输出）引脚定义
#define SOL1_PIN GPIO_NUM_33    // 快门动作 高电平动作，初始化为低
#define SOL2_PIN GPIO_NUM_13    // 光圈动作 高电平动作，初始化为低
#define MOTOR_PIN GPIO_NUM_25   // 机身动作 高电平动作，初始化为低
#define FF_PIN GPIO_NUM_32      // 闪光灯闪光 高电平动作，初始化为低

// PCF8575 3D 按键引脚（位偏移，对应 RP2040 pins.h）
#define PCF_BUTTON3D_DOWN   10  // 下键
#define PCF_BUTTON3D_UP     8   // 上键
#define PCF_BUTTON3D_PUSH   9   // 按下键
