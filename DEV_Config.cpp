#include "DEV_Config.h"

static SPISettings epd_spi_settings(10000000, MSBFIRST, SPI_MODE0);

static void GPIO_Config(void) {
  pinMode(EPD_BUSY_PIN, INPUT);
  pinMode(EPD_RST_PIN, OUTPUT);
  pinMode(EPD_DC_PIN, OUTPUT);
  pinMode(EPD_SCK_PIN, OUTPUT);
  pinMode(EPD_MOSI_PIN, OUTPUT);
  pinMode(EPD_CS_PIN, OUTPUT);

  digitalWrite(EPD_CS_PIN, HIGH);
  digitalWrite(EPD_SCK_PIN, LOW);

  pinMode(BTN_A_PIN, INPUT_PULLUP);
  pinMode(BTN_B_PIN, INPUT_PULLUP);
}

UBYTE DEV_Module_Init(void) {
  GPIO_Config();
  SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
  return 0;
}

void DEV_EPD_BusyPinRestore(void) {
  pinMode(EPD_BUSY_PIN, INPUT);
}

void DEV_SPI_WriteByte(UBYTE data) {
  SPI.beginTransaction(epd_spi_settings);
  SPI.transfer(data);
  SPI.endTransaction();
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len) {
  SPI.beginTransaction(epd_spi_settings);
  SPI.writeBytes(pData, len);
  SPI.endTransaction();
}
