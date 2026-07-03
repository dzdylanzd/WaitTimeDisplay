#include "lcd_st7789.h"

// ---------------------------------------------------------------------------
// Low-level SPI helpers
// ---------------------------------------------------------------------------

static inline void _cs_low()  { digitalWrite(LCD_PIN_CS, LOW);  }
static inline void _cs_high() { digitalWrite(LCD_PIN_CS, HIGH); }
static inline void _dc_cmd()  { digitalWrite(LCD_PIN_DC, LOW);  }
static inline void _dc_data() { digitalWrite(LCD_PIN_DC, HIGH); }

static void _write_cmd(uint8_t cmd) {
    SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
    _cs_low(); _dc_cmd();
    SPI.transfer(cmd);
    _cs_high();
    SPI.endTransaction();
}

static void _write_byte(uint8_t d) {
    SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
    _cs_low(); _dc_data();
    SPI.transfer(d);
    _cs_high();
    SPI.endTransaction();
}

static void _write_cmd_data(uint8_t cmd, const uint8_t* data, size_t len) {
    _write_cmd(cmd);
    SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
    _cs_low(); _dc_data();
    for (size_t i = 0; i < len; i++) SPI.transfer(data[i]);
    _cs_high();
    SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// LCD_SetWindow — define the write area for LCD_WritePixels
// Landscape mode: X=0..319, Y=0..171 (Y gets +34 hardware offset)
// ---------------------------------------------------------------------------
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint16_t yr0 = y0 + LCD_ROW_OFFSET;
    uint16_t yr1 = y1 + LCD_ROW_OFFSET;

    // Column address (X, no offset in landscape)
    const uint8_t caset[] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    _write_cmd_data(0x2A, caset, sizeof(caset));

    // Row address (Y, +34 offset)
    const uint8_t raset[] = {
        (uint8_t)(yr0 >> 8), (uint8_t)(yr0 & 0xFF),
        (uint8_t)(yr1 >> 8), (uint8_t)(yr1 & 0xFF)
    };
    _write_cmd_data(0x2B, raset, sizeof(raset));

    // Memory write command — pixels follow
    _write_cmd(0x2C);
}

// ---------------------------------------------------------------------------
// LCD_WritePixels — bulk transfer a pixel buffer into the current window
// ---------------------------------------------------------------------------
void LCD_WritePixels(uint16_t* color, uint32_t numPixels) {
    uint32_t numBytes = numPixels * sizeof(uint16_t);

    SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
    _cs_low(); _dc_data();

    // writeBytes() is write-only (no receive buffer), so there is no danger of
    // overflowing a bounce buffer. Chunk to stay within SPI driver limits.
    const uint32_t CHUNK = 4096;
    uint8_t* p = (uint8_t*)color;
    while (numBytes > 0) {
        uint32_t n = (numBytes > CHUNK) ? CHUNK : numBytes;
        SPI.writeBytes(p, n);
        p += n;
        numBytes -= n;
    }

    _cs_high();
    SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// Rotation — both values are landscape; flip180 turns the image upside-down
// for mounting the device the other way up.
//   normal : MADCTL 0x60 (MX=1, MV=1, RGB order)
//   flipped: MADCTL 0xA0 (MY=1, MV=1, RGB order)
// The BGR bit (0x08) must stay CLEAR on this panel: with it set, red and
// blue swap (a crimson header renders dark blue — seen on hardware).
// Waveshare-community reference configs default to BGR but note the panels
// ship in both orders and to flip the setting when colours are wrong.
// The 34-row offset is symmetric ((240-172)/2), so LCD_SetWindow works
// unchanged in either orientation.
// ---------------------------------------------------------------------------
void LCD_SetRotation(bool flip180) {
    const uint8_t madctl = flip180 ? 0xA0 : 0x60;
    _write_cmd_data(0x36, &madctl, 1);
}

// ---------------------------------------------------------------------------
// Backlight
// ---------------------------------------------------------------------------
void LCD_SetBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * ((1u << LCD_BL_RES) - 1) / 100;
    ledcWrite(LCD_PIN_BL, duty);
}

// ---------------------------------------------------------------------------
// LCD_Init — full ST7789 initialisation sequence, landscape mode
// ---------------------------------------------------------------------------
void LCD_Init(void) {
    // Pin modes
    pinMode(LCD_PIN_CS,  OUTPUT); digitalWrite(LCD_PIN_CS,  HIGH);
    pinMode(LCD_PIN_DC,  OUTPUT); digitalWrite(LCD_PIN_DC,  HIGH);
    pinMode(LCD_PIN_RST, OUTPUT);

    // Backlight via LEDC
    ledcAttach(LCD_PIN_BL, LCD_BL_FREQ, LCD_BL_RES);
    LCD_SetBacklight(0);

    // SPI bus
    SPI.begin(LCD_PIN_SCLK, LCD_PIN_MISO, LCD_PIN_MOSI);

    // Hardware reset
    digitalWrite(LCD_PIN_RST, HIGH); delay(10);
    digitalWrite(LCD_PIN_RST, LOW);  delay(10);
    digitalWrite(LCD_PIN_RST, HIGH); delay(50);

    // ST7789 initialisation sequence
    _write_cmd(0x11);   // Sleep out
    delay(120);

    // MADCTL: landscape (320 wide × 172 tall), see LCD_SetRotation (RGB
    //   order — the BGR bit swapped red/blue on this panel).
    //   AppStateManager re-applies the user's flip setting after config load.
    LCD_SetRotation(false);

    // COLMOD: 16-bit colour (RGB565)
    _write_cmd_data(0x3A, (const uint8_t[]){0x05}, 1);

    // Porching (PORCTRK)
    _write_cmd_data(0xB2, (const uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5);

    // Gate control
    _write_cmd_data(0xB7, (const uint8_t[]){0x35}, 1);

    // VCOM
    _write_cmd_data(0xBB, (const uint8_t[]){0x35}, 1);

    // LCM control
    _write_cmd_data(0xC0, (const uint8_t[]){0x2C}, 1);

    // VDV/VRH command enable
    _write_cmd_data(0xC2, (const uint8_t[]){0x01}, 1);

    // VRH set
    _write_cmd_data(0xC3, (const uint8_t[]){0x13}, 1);

    // VDV set
    _write_cmd_data(0xC4, (const uint8_t[]){0x20}, 1);

    // Frame rate control (60 Hz)
    _write_cmd_data(0xC6, (const uint8_t[]){0x0F}, 1);

    // Power control 1
    _write_cmd_data(0xD0, (const uint8_t[]){0xA4, 0xA1}, 2);

    // Positive voltage gamma
    _write_cmd_data(0xE0, (const uint8_t[]){
        0xF0, 0x00, 0x04, 0x04, 0x04, 0x05,
        0x29, 0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30
    }, 14);

    // Negative voltage gamma
    _write_cmd_data(0xE1, (const uint8_t[]){
        0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07,
        0x28, 0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32
    }, 14);

    // Display inversion on (needed for correct colours on ST7789)
    _write_cmd(0x21);

    // Normal display mode on
    _write_cmd(0x13);

    // Display on
    _write_cmd(0x29);
    delay(10);

    LCD_SetBacklight(100);
}
