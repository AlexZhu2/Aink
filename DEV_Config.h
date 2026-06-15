#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

// Seeed Studio XIAO ESP32S3 — 与 CameraWebServer 一致
// D10 (GPIO9/MOSI) -> DIN, D8 (GPIO7/SCK) -> CLK,
// D9 (GPIO8) -> CS, D3 (GPIO4) -> DC,
// D1 (GPIO2) -> RST, D0 (GPIO1) -> BUSY
#define EPD_SCK_PIN  7
#define EPD_MOSI_PIN 9
#define EPD_CS_PIN   8
#define EPD_RST_PIN  2
#define EPD_DC_PIN   4
#define EPD_BUSY_PIN 1

// 用户按键：A=D6/GPIO43，B=D7/GPIO44，内部上拉，按下为 LOW
#define BTN_A_PIN  43
#define BTN_B_PIN  44

// 按键注册反馈灯：XIAO ESP32S3 Sense 的板载/相机 LED 默认在 GPIO21。
// 如移植到无此 LED 的硬件，可在编译参数中设为 -1 禁用。
#ifndef KEY_INDICATOR_LED_PIN
#define KEY_INDICATOR_LED_PIN 21
#endif

#ifndef KEY_INDICATOR_LED_ACTIVE_HIGH
#define KEY_INDICATOR_LED_ACTIVE_HIGH 0
#endif

#define GPIO_PIN_SET   1
#define GPIO_PIN_RESET 0

#define DEV_Digital_Write(_pin, _value) digitalWrite(_pin, (_value) == 0 ? LOW : HIGH)
#define DEV_Digital_Read(_pin) digitalRead(_pin)
#define DEV_Delay_ms(__xms) delay(__xms)

UBYTE DEV_Module_Init(void);
void DEV_SPI_WriteByte(UBYTE data);
void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len);

#endif
