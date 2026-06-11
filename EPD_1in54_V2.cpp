/*****************************************************************************
* | File      	:   EPD_1in54_V2.c
* | Author      :   Waveshare team
* | Function    :   1.54inch e-paper V2
******************************************************************************/
#include "EPD_1in54_V2.h"

#define EPD_BUSY_TIMING_DEBUG 1
#define EPD_BUSY_TIMEOUT_MS   35000
/** GPIO1 (BUSY) shared with battery ADC — when BUSY reads idle instantly, pad full/partial refresh. */
#define EPD_FULL_REFRESH_MIN_MS     4500
#define EPD_PARTIAL_REFRESH_MIN_MS  400
#define EPD_CLEAR_REFRESH_MIN_MS    22000

static uint32_t epd_busy_read_count = 0;

unsigned char WF_Full_1IN54[159] =
{
0x80,	0x48,	0x40,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x40,	0x48,	0x80,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x80,	0x48,	0x40,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x40,	0x48,	0x80,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0xA,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x8,	0x1,	0x0,	0x8,	0x1,	0x0,	0x2,
0xA,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x22,	0x22,	0x22,	0x22,	0x22,	0x22,	0x0,	0x0,	0x0,
0x22,	0x17,	0x41,	0x0,	0x32,	0x20
};

unsigned char WF_PARTIAL_1IN54_0[159] =
{
0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x80,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x40,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0xF,0x0,0x0,0x0,0x0,0x0,0x0,
0x1,0x1,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x22,0x22,0x22,0x22,0x22,0x22,0x0,0x0,0x0,
0x02,0x17,0x41,0xB0,0x32,0x28,
};

static void EPD_1IN54_V2_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
}

static void EPD_1IN54_V2_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_1IN54_V2_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_1IN54_V2_SendDataBuffer(UBYTE *Data, UDOUBLE Len)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_Write_nByte(Data, Len);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static uint32_t EPD_1IN54_V2_ReadBusy(const char *label)
{
    uint32_t start = millis();
    uint32_t samples = 0;
    int startLevel = DEV_Digital_Read(EPD_BUSY_PIN);

#if EPD_BUSY_TIMING_DEBUG
    Serial.printf("[EPD] BUSY wait: %s (start=%d)...\r\n", label, startLevel);
#endif

    while (DEV_Digital_Read(EPD_BUSY_PIN) == 1) {
        samples++;
        if (millis() - start >= EPD_BUSY_TIMEOUT_MS) {
#if EPD_BUSY_TIMING_DEBUG
            Serial.printf("[EPD] BUSY TIMEOUT %-20s stuck HIGH after %lums\r\n",
                label, (unsigned long)(millis() - start));
#endif
            return millis() - start;
        }
        if ((samples & 0x3F) == 0) {
            yield();
        }
        DEV_Delay_ms(1);
    }

    uint32_t elapsed = millis() - start;

#if EPD_BUSY_TIMING_DEBUG
    Serial.printf("[EPD] BUSY done  #%-3lu %-20s %lums\r\n",
        ++epd_busy_read_count, label, (unsigned long)elapsed);
#endif

    return elapsed;
}

static void EPD_1IN54_V2_WaitBusyMin(const char *label, uint32_t minMs)
{
    const uint32_t elapsed = EPD_1IN54_V2_ReadBusy(label);
    if (minMs == 0 || elapsed >= minMs) {
        return;
    }
    const uint32_t pad = minMs - elapsed;
#if EPD_BUSY_TIMING_DEBUG
    Serial.printf("[EPD] BUSY pad   #%-3lu %-20s +%lums (GPIO1)\r\n",
                  epd_busy_read_count, label, (unsigned long)pad);
#endif
    DEV_Delay_ms(pad);
}

static void EPD_1IN54_V2_TurnOnDisplay(void)
{
    EPD_1IN54_V2_SendCommand(0x22);
    EPD_1IN54_V2_SendData(0xc7);
    EPD_1IN54_V2_SendCommand(0x20);
    EPD_1IN54_V2_WaitBusyMin("display-full", EPD_FULL_REFRESH_MIN_MS);
}

static void EPD_1IN54_V2_TurnOnDisplayClear(void)
{
    EPD_1IN54_V2_SendCommand(0x22);
    EPD_1IN54_V2_SendData(0xc7);
    EPD_1IN54_V2_SendCommand(0x20);
    EPD_1IN54_V2_WaitBusyMin("display-clear", EPD_CLEAR_REFRESH_MIN_MS);
}

static void EPD_1IN54_V2_TurnOnDisplayPart(void)
{
    EPD_1IN54_V2_SendCommand(0x22);
    EPD_1IN54_V2_SendData(0xcF);
    EPD_1IN54_V2_SendCommand(0x20);
    EPD_1IN54_V2_WaitBusyMin("display-partial", EPD_PARTIAL_REFRESH_MIN_MS);
}

static void EPD_1IN54_V2_Lut(UBYTE *lut)
{
    EPD_1IN54_V2_SendCommand(0x32);
    for (UBYTE i = 0; i < 153; i++)
        EPD_1IN54_V2_SendData(lut[i]);
    EPD_1IN54_V2_ReadBusy("lut");
}

static void EPD_1IN54_V2_SetLut(UBYTE *lut)
{
    EPD_1IN54_V2_Lut(lut);

    EPD_1IN54_V2_SendCommand(0x3f);
    EPD_1IN54_V2_SendData(lut[153]);

    EPD_1IN54_V2_SendCommand(0x03);
    EPD_1IN54_V2_SendData(lut[154]);

    EPD_1IN54_V2_SendCommand(0x04);
    EPD_1IN54_V2_SendData(lut[155]);
    EPD_1IN54_V2_SendData(lut[156]);
    EPD_1IN54_V2_SendData(lut[157]);

    EPD_1IN54_V2_SendCommand(0x2c);
    EPD_1IN54_V2_SendData(lut[158]);
}

static void EPD_1IN54_V2_SetWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    EPD_1IN54_V2_SendCommand(0x44);
    EPD_1IN54_V2_SendData((Xstart >> 3) & 0xFF);
    EPD_1IN54_V2_SendData((Xend >> 3) & 0xFF);

    EPD_1IN54_V2_SendCommand(0x45);
    EPD_1IN54_V2_SendData(Ystart & 0xFF);
    EPD_1IN54_V2_SendData((Ystart >> 8) & 0xFF);
    EPD_1IN54_V2_SendData(Yend & 0xFF);
    EPD_1IN54_V2_SendData((Yend >> 8) & 0xFF);
}

static void EPD_1IN54_V2_SetCursor(UWORD Xstart, UWORD Ystart)
{
    EPD_1IN54_V2_SendCommand(0x4E);
    EPD_1IN54_V2_SendData(Xstart & 0xFF);

    EPD_1IN54_V2_SendCommand(0x4F);
    EPD_1IN54_V2_SendData(Ystart & 0xFF);
    EPD_1IN54_V2_SendData((Ystart >> 8) & 0xFF);
}

void EPD_1IN54_V2_Init(void)
{
    EPD_1IN54_V2_Reset();

    EPD_1IN54_V2_ReadBusy("init-reset");
    EPD_1IN54_V2_SendCommand(0x12);
    EPD_1IN54_V2_ReadBusy("init-swreset");

    EPD_1IN54_V2_SendCommand(0x01);
    EPD_1IN54_V2_SendData(0xC7);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x01);

  EPD_1IN54_V2_SendCommand(0x11);
  EPD_1IN54_V2_SendData(0x01);

    EPD_1IN54_V2_SetWindows(0, EPD_1IN54_V2_HEIGHT - 1, EPD_1IN54_V2_WIDTH - 1, 0);

    EPD_1IN54_V2_SendCommand(0x3C);
    EPD_1IN54_V2_SendData(0x01);

    EPD_1IN54_V2_SendCommand(0x18);
    EPD_1IN54_V2_SendData(0x80);

    EPD_1IN54_V2_SendCommand(0x22);
    EPD_1IN54_V2_SendData(0xB1);
    EPD_1IN54_V2_SendCommand(0x20);

    EPD_1IN54_V2_SetCursor(0, EPD_1IN54_V2_HEIGHT - 1);
    EPD_1IN54_V2_ReadBusy("init-load-temp");

    EPD_1IN54_V2_SetLut(WF_Full_1IN54);
}

void EPD_1IN54_V2_Init_Partial(void)
{
    EPD_1IN54_V2_Reset();
    EPD_1IN54_V2_ReadBusy("partial-init-reset");

    EPD_1IN54_V2_SetLut(WF_PARTIAL_1IN54_0);
    EPD_1IN54_V2_SendCommand(0x37);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x40);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);
    EPD_1IN54_V2_SendData(0x00);

    EPD_1IN54_V2_SendCommand(0x3C);
    EPD_1IN54_V2_SendData(0x80);

    EPD_1IN54_V2_SendCommand(0x22);
    EPD_1IN54_V2_SendData(0xc0);
    EPD_1IN54_V2_SendCommand(0x20);
    EPD_1IN54_V2_WaitBusyMin("partial-init-power", EPD_PARTIAL_REFRESH_MIN_MS);
}

void EPD_1IN54_V2_Clear(void)
{
    UWORD Width, Height;
    Width = (EPD_1IN54_V2_WIDTH % 8 == 0) ? (EPD_1IN54_V2_WIDTH / 8) : (EPD_1IN54_V2_WIDTH / 8 + 1);
    Height = EPD_1IN54_V2_HEIGHT;

#if EPD_BUSY_TIMING_DEBUG
    Serial.println("[EPD] Clear: sending 0x24 buffer...");
#endif
    EPD_1IN54_V2_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_1IN54_V2_SendData(0xFF);
        }
    }

#if EPD_BUSY_TIMING_DEBUG
    Serial.println("[EPD] Clear: sending 0x26 buffer...");
#endif
    EPD_1IN54_V2_SendCommand(0x26);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_1IN54_V2_SendData(0xFF);
        }
    }

#if EPD_BUSY_TIMING_DEBUG
    Serial.println("[EPD] Clear: full refresh (~22s min)...");
#endif
    EPD_1IN54_V2_TurnOnDisplayClear();
}

void EPD_1IN54_V2_Display(UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_1IN54_V2_WIDTH % 8 == 0) ? (EPD_1IN54_V2_WIDTH / 8) : (EPD_1IN54_V2_WIDTH / 8 + 1);
    Height = EPD_1IN54_V2_HEIGHT;

    EPD_1IN54_V2_SetCursor(0, EPD_1IN54_V2_HEIGHT - 1);
    EPD_1IN54_V2_SendCommand(0x24);
    EPD_1IN54_V2_SendDataBuffer(Image, Width * Height);
    EPD_1IN54_V2_TurnOnDisplay();
}

void EPD_1IN54_V2_DisplayPartBaseImage(UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_1IN54_V2_WIDTH % 8 == 0) ? (EPD_1IN54_V2_WIDTH / 8) : (EPD_1IN54_V2_WIDTH / 8 + 1);
    Height = EPD_1IN54_V2_HEIGHT;

    EPD_1IN54_V2_SetCursor(0, EPD_1IN54_V2_HEIGHT - 1);
    EPD_1IN54_V2_SendCommand(0x24);
    EPD_1IN54_V2_SendDataBuffer(Image, Width * Height);
    EPD_1IN54_V2_SendCommand(0x26);
    EPD_1IN54_V2_SendDataBuffer(Image, Width * Height);
    EPD_1IN54_V2_TurnOnDisplay();
}

void EPD_1IN54_V2_DisplayPart(UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_1IN54_V2_WIDTH % 8 == 0) ? (EPD_1IN54_V2_WIDTH / 8) : (EPD_1IN54_V2_WIDTH / 8 + 1);
    Height = EPD_1IN54_V2_HEIGHT;

    EPD_1IN54_V2_SetCursor(0, EPD_1IN54_V2_HEIGHT - 1);
    EPD_1IN54_V2_SendCommand(0x24);
    EPD_1IN54_V2_SendDataBuffer(Image, Width * Height);
    EPD_1IN54_V2_TurnOnDisplayPart();
}

void EPD_1IN54_V2_Sleep(void)
{
    EPD_1IN54_V2_SendCommand(0x10);
    EPD_1IN54_V2_SendData(0x01);
    DEV_Delay_ms(100);
}
