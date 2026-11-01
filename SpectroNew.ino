#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <math.h>

extern "C" {
  void setup(void);
  void loop(void);
}

// ============================================================================
// 1. DEFINICAO DOS PINOS
// ============================================================================
// Display ST7735 (8 pinos da direita - GPIOB)
#define TFT_CS        PB12
#define TFT_RST       PB14
#define TFT_DC        PB15
#define TFT_MOSI      PB13
#define TFT_SCLK      PB10

// Microfone Digital I2S3 INMP441
#define MIC_WS_PIN    PA15
#define MIC_SCK_PIN   PB3
#define MIC_SD_PIN    PB5

// Botao KEY, Buzzer e LED interno da Black Pill
#define KEY_BTN_PIN   PA0
#define BUZZER_PIN    PB0
#define REC_LED_PIN   PC13

// Leitor MicroSD Embutido no Display (4 pinos da esquerda -> SPI1)
#define SD_CS_PIN     PA4
#define SD_SCK_PIN    PA5
#define SD_MISO_PIN   PA6
#define SD_MOSI_PIN   PA7

// ============================================================================
// 2. PALETA DE CORES RGB565
// ============================================================================
#define C_BLACK       0x0000
#define C_WHITE       0xFFFF
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_BLUE        0x001F
#define C_CYAN        0x07FF
#define C_MAGENTA     0xF81F
#define C_YELLOW      0xFFE0
#define C_ORANGE      0xFD20
#define C_DARK_BG     0x0003
#define C_HEADER_BG   0x0843
#define C_FOOTER_BG   0x0842
#define C_GRID_DIM    0x10A2
#define C_GRID_AXIS   0x2965

// ============================================================================
// 3. DRIVER ULTRA-RAPIDO ST7735 (GPIOB->BSRR)
// ============================================================================
#define BSRR_MOSI_SET (1UL << 13)
#define BSRR_MOSI_CLR (1UL << (13 + 16))
#define BSRR_SCLK_SET (1UL << 10)
#define BSRR_SCLK_CLR (1UL << (10 + 16))
#define BSRR_DC_SET   (1UL << 15)
#define BSRR_DC_CLR   (1UL << (15 + 16))
#define BSRR_CS_SET   (1UL << 12)
#define BSRR_CS_CLR   (1UL << (12 + 16))

#define FAST_BIT(val, bit) \
  GPIOB->BSRR = ((val) & (1 << (bit))) ? (BSRR_MOSI_SET | BSRR_SCLK_CLR) : (BSRR_MOSI_CLR | BSRR_SCLK_CLR); \
  __NOP(); \
  GPIOB->BSRR = BSRR_SCLK_SET;

class FastST7735 : public Adafruit_GFX {
public:
  FastST7735() : Adafruit_GFX(160, 128) {}

  inline void spiWrite8(uint8_t val) __attribute__((always_inline)) {
    FAST_BIT(val, 7); FAST_BIT(val, 6); FAST_BIT(val, 5); FAST_BIT(val, 4);
    FAST_BIT(val, 3); FAST_BIT(val, 2); FAST_BIT(val, 1); FAST_BIT(val, 0);
  }

  inline void spiWrite16(uint16_t val) __attribute__((always_inline)) {
    FAST_BIT(val, 15); FAST_BIT(val, 14); FAST_BIT(val, 13); FAST_BIT(val, 12);
    FAST_BIT(val, 11); FAST_BIT(val, 10); FAST_BIT(val, 9);  FAST_BIT(val, 8);
    FAST_BIT(val, 7);  FAST_BIT(val, 6);  FAST_BIT(val, 5);  FAST_BIT(val, 4);
    FAST_BIT(val, 3);  FAST_BIT(val, 2);  FAST_BIT(val, 1);  FAST_BIT(val, 0);
  }

  void writeCmd(uint8_t cmd) {
    GPIOB->BSRR = BSRR_DC_CLR | BSRR_CS_CLR;
    spiWrite8(cmd);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  void writeData8(uint8_t data) {
    GPIOB->BSRR = BSRR_DC_SET | BSRR_CS_CLR;
    spiWrite8(data);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  inline void setAddrWindowFast(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    GPIOB->BSRR = BSRR_DC_CLR; spiWrite8(0x2A);
    GPIOB->BSRR = BSRR_DC_SET; spiWrite16(x0); spiWrite16(x1);
    GPIOB->BSRR = BSRR_DC_CLR; spiWrite8(0x2B);
    GPIOB->BSRR = BSRR_DC_SET; spiWrite16(y0); spiWrite16(y1);
    GPIOB->BSRR = BSRR_DC_CLR; spiWrite8(0x2C);
    GPIOB->BSRR = BSRR_DC_SET;
  }

  void initR() {
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_MOSI, OUTPUT);
    pinMode(TFT_SCLK, OUTPUT);

    GPIOB->BSRR = BSRR_CS_SET | BSRR_DC_SET | BSRR_SCLK_CLR;
    digitalWrite(TFT_RST, HIGH); delay(10);
    digitalWrite(TFT_RST, LOW);  delay(20);
    digitalWrite(TFT_RST, HIGH); delay(120);

    writeCmd(0x01); delay(120);
    writeCmd(0x11); delay(120);
    writeCmd(0xB1); writeData8(0x01); writeData8(0x2C); writeData8(0x2D);
    writeCmd(0xB2); writeData8(0x01); writeData8(0x2C); writeData8(0x2D);
    writeCmd(0xB3); writeData8(0x01); writeData8(0x2C); writeData8(0x2D);
                    writeData8(0x01); writeData8(0x2C); writeData8(0x2D);
    writeCmd(0xB4); writeData8(0x07);
    writeCmd(0xC0); writeData8(0xA2); writeData8(0x02); writeData8(0x84);
    writeCmd(0xC1); writeData8(0xC5);
    writeCmd(0xC2); writeData8(0x0A); writeData8(0x00);
    writeCmd(0xC3); writeData8(0x8A); writeData8(0x2A);
    writeCmd(0xC4); writeData8(0x8A); writeData8(0xEE);
    writeCmd(0xC5); writeData8(0x0E);
    writeCmd(0x20);
    writeCmd(0x36); writeData8(0xA0);
    writeCmd(0x3A); writeData8(0x05);
    writeCmd(0x29); delay(50);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= 160 || y < 0 || y >= 128) return;
    GPIOB->BSRR = BSRR_CS_CLR;
    setAddrWindowFast((uint8_t)x, (uint8_t)y, (uint8_t)x, (uint8_t)y);
    spiWrite16(color);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (x < 0 || x >= 160 || y >= 128) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > 128) h = 128 - y;
    if (h <= 0) return;
    GPIOB->BSRR = BSRR_CS_CLR;
    setAddrWindowFast((uint8_t)x, (uint8_t)y, (uint8_t)x, (uint8_t)(y + h - 1));
    while (h--) spiWrite16(color);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (y < 0 || y >= 128 || x >= 160) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > 160) w = 160 - x;
    if (w <= 0) return;
    GPIOB->BSRR = BSRR_CS_CLR;
    setAddrWindowFast((uint8_t)x, (uint8_t)y, (uint8_t)(x + w - 1), (uint8_t)y);
    while (w--) spiWrite16(color);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (x >= 160 || y >= 128) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 160) w = 160 - x;
    if (y + h > 128) h = 128 - y;
    if (w <= 0 || h <= 0) return;
    uint32_t total = (uint32_t)w * (uint32_t)h;
    GPIOB->BSRR = BSRR_CS_CLR;
    setAddrWindowFast((uint8_t)x, (uint8_t)y, (uint8_t)(x + w - 1), (uint8_t)(y + h - 1));
    while (total--) spiWrite16(color);
    GPIOB->BSRR = BSRR_CS_SET;
  }

  void fillScreen(uint16_t color) override {
    fillRect(0, 0, 160, 128, color);
  }
};

FastST7735 tft;

// ============================================================================
// 4. CONSTANTES E BUFFERS (MODO ULTRASSOM ESTENDIDO 50 kHz -> 25 kHz NYQUIST)
// ============================================================================
#define DMA_BUF_LEN       32768
#define FFT_N             512
#define SD_CHUNK_SAMPLES  1024
#define TOTAL_REC_CHUNKS  243   // 5,0 segundos de captura (ou 485 para 10,0 segundos)
#define SAMPLE_RATE_RAW   51682.2f
#define FREQ_CAL_FACTOR   0.9620f
#define SAMPLE_RATE_HZ    (SAMPLE_RATE_RAW * FREQ_CAL_FACTOR)
#define DSP_INTERVAL_MS   17

#define GRAPH_X           0
#define GRAPH_Y           22
#define GRAPH_W           160
#define GRAPH_H           94
#define SCOPE_CENTER_Y    47
#define SCOPE_BOTTOM_Y    70
#define FFT_TOP_Y         73
#define FFT_BOTTOM_Y      115

#define WF_COLS           80
#define WF_ROWS           48

#define INFRA_DETECT_THRESHOLD    55
#define ULTRA_FREQ_THRESHOLD_HZ   11500.0f
#define ULTRA_DB_THRESHOLD        62
#define PERSISTENCE_FRAMES_REQ    8
#define COOLDOWN_AFTER_ALERT_MS   6000

volatile uint16_t dmaBuffer[DMA_BUF_LEN] __attribute__((aligned(4)));

float fftReal[FFT_N];
float fftImag[FFT_N];
float fftMag[FFT_N / 2];
float binNoiseFloor[FFT_N / 2];
bool noiseFloorReady = false;

int16_t rawSamples[FFT_N];
int16_t sdSectorBuf[SD_CHUNK_SAMPLES];

uint8_t logBinStart[WF_COLS];
uint8_t logBinEnd[WF_COLS];

uint8_t prevScopeY0[GRAPH_W];
uint8_t prevScopeY1[GRAPH_W];
bool scopeTraceValid = false;

uint8_t fftPeakHoldY[76];
uint8_t fftPeakDecay[76];

uint8_t wfBuffer[WF_ROWS][WF_COLS];
uint8_t wfLiveX[WF_COLS];
uint8_t wfHead = 0;
uint8_t wfWavePhase = 0;

uint8_t seismoHist[GRAPH_W];
uint8_t seismoIdx = 0;
int maxInfraHold = 0;

float dcBaseline = 0.0f;
float lpInfra = 0.0f;
bool baselineInitialized = false;
int lockedChOffset = -1;

volatile uint8_t viewMode = 0;
volatile bool modeChangedFlag = true;
volatile bool manualRecordRequest = false;
volatile uint16_t keyHoldMs = 0;
volatile bool keyDown = false;
volatile bool keyLongFired = false;

uint32_t bootTime = 0;
uint32_t lastDspTime = 0;

bool sdAvailable = false;
uint16_t nextEventId = 1;
volatile bool isRecordingSD = false;
uint8_t activeAlertType = 0;
uint32_t alertBannerUntil = 0;
uint32_t savedBannerUntil = 0;
uint32_t triggerCooldownUntil = 0;
uint8_t infraPersistCount = 0;
uint8_t ultraPersistCount = 0;

char lastSavedFilename[16] = "NENHUM";

// ============================================================================
// 5. RELOGIO EM TEMPO REAL (DATA E HORA EXATA DO MOMENTO DA GRAVACAO!)
// ============================================================================
#define BKP_MAGIC_VALUE   0x5A5A32F4UL

volatile uint16_t rtcYear   = 2026;
volatile uint8_t  rtcMonth  = 10;
volatile uint8_t  rtcDay    = 31;
volatile uint8_t  rtcHour   = 16;
volatile uint8_t  rtcMinute = 0;
volatile uint8_t  rtcSecond = 0;
volatile uint16_t rtcSubMs  = 0;

uint8_t daysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t dim[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (m < 1 || m > 12) return 30;
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
  return dim[m - 1];
}

// Empacota Data (Ano/Mes/Dia) e Hora (Hora/Min/Seg) em 32 bits para salvar no Backup RTC
uint32_t packDateTime(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  uint32_t yr = (y >= 2020) ? (y - 2020) : 0;
  return ((yr & 0x3F) << 26)
       | (((uint32_t)mo & 0x0F) << 22)
       | (((uint32_t)d  & 0x1F) << 17)
       | (((uint32_t)h  & 0x1F) << 12)
       | (((uint32_t)mi & 0x3F) << 6)
       | ((uint32_t)s   & 0x3F);
}

void unpackDateTime(uint32_t packed) {
  rtcYear   = 2020 + ((packed >> 26) & 0x3F);
  rtcMonth  = (packed >> 22) & 0x0F;
  rtcDay    = (packed >> 17) & 0x1F;
  rtcHour   = (packed >> 12) & 0x1F;
  rtcMinute = (packed >> 6)  & 0x3F;
  rtcSecond = packed & 0x3F;
}

// Avanca 1 segundo no calendario
void tickCalendarOneSecond() {
  rtcSecond++;
  if (rtcSecond >= 60) {
    rtcSecond = 0;
    rtcMinute++;
    if (rtcMinute >= 60) {
      rtcMinute = 0;
      rtcHour++;
      if (rtcHour >= 24) {
        rtcHour = 0;
        rtcDay++;
        if (rtcDay > daysInMonth(rtcYear, rtcMonth)) {
          rtcDay = 1;
          rtcMonth++;
          if (rtcMonth > 12) {
            rtcMonth = 1;
            rtcYear++;
          }
        }
      }
    }
  }
  // Salva o horario atualizado no registrador de Backup do STM32F411 (sobrevive a Reset!)
  RTC->BKP1R = packDateTime(rtcYear, rtcMonth, rtcDay, rtcHour, rtcMinute, rtcSecond);
}

// Extrai a data e hora de compilacao do PC (__DATE__ e __TIME__)
uint32_t getCompileTimePacked() {
  const char* d = __DATE__; // Ex: "Oct 31 2026"
  const char* t = __TIME__; // Ex: "16:05:10"

  uint8_t mo = 1;
  if      (strncmp(d, "Jan", 3) == 0) mo = 1;
  else if (strncmp(d, "Feb", 3) == 0) mo = 2;
  else if (strncmp(d, "Mar", 3) == 0) mo = 3;
  else if (strncmp(d, "Apr", 3) == 0) mo = 4;
  else if (strncmp(d, "May", 3) == 0) mo = 5;
  else if (strncmp(d, "Jun", 3) == 0) mo = 6;
  else if (strncmp(d, "Jul", 3) == 0) mo = 7;
  else if (strncmp(d, "Aug", 3) == 0) mo = 8;
  else if (strncmp(d, "Sep", 3) == 0) mo = 9;
  else if (strncmp(d, "Oct", 3) == 0) mo = 10;
  else if (strncmp(d, "Nov", 3) == 0) mo = 11;
  else if (strncmp(d, "Dec", 3) == 0) mo = 12;

  uint8_t dy = (uint8_t)atoi(d + 4);
  uint16_t yr = (uint16_t)atoi(d + 7);
  uint8_t hr = (uint8_t)atoi(t);
  uint8_t mi = (uint8_t)atoi(t + 3);
  uint8_t sc = (uint8_t)atoi(t + 6);

  return packDateTime(yr, mo, dy, hr, mi, sc);
}

void initRealtimeClock() {
  // Habilita acesso aos registradores de Backup (RTC->BKPxR) do STM32F411
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  PWR->CR |= PWR_CR_DBP;

  uint32_t compilePacked = getCompileTimePacked();

  // Se for um upload novo do Arduino IDE (ou primeira energizacao), sincroniza com o PC
  if (RTC->BKP0R != BKP_MAGIC_VALUE || RTC->BKP2R != compilePacked) {
    unpackDateTime(compilePacked);
    // Compensa ~9 segundos do tempo entre compilar e concluir a gravacao na placa
    for (int i = 0; i < 9; i++) tickCalendarOneSecond();
    RTC->BKP0R = BKP_MAGIC_VALUE;
    RTC->BKP2R = compilePacked;
  } else {
    // Se foi apenas um Reset da placa, recupera o relogio que ja estava correndo!
    unpackDateTime(RTC->BKP1R);
  }
}

// Callback Oficial do <SD.h> (SdFile): grava a Data e Hora exatas no arquivo .WAV!
void fatDateTimeCallback(uint16_t* date, uint16_t* time) {
  uint16_t y  = rtcYear;
  uint8_t  mo = rtcMonth;
  uint8_t  d  = rtcDay;
  uint8_t  h  = rtcHour;
  uint8_t  mi = rtcMinute;
  uint8_t  s  = rtcSecond;

  *date = (uint16_t)(((y - 1980) << 9) | ((uint16_t)mo << 5) | d);
  *time = (uint16_t)(((uint16_t)h << 11) | ((uint16_t)mi << 5) | (s >> 1));
}

struct BuzzerStep {
  uint16_t freqHz;
  uint16_t durationMs;
};
BuzzerStep buzzerQueue[8];
uint8_t buzzerQueueLen = 0;
uint8_t buzzerQueueIdx = 0;
uint32_t buzzerStepEndMs = 0;
volatile uint16_t activeBuzzerFreq = 0;
volatile uint8_t buzzerTickDiv = 0;

struct WavHeader {
  char     riff[4];
  uint32_t overallSize;
  char     wave[4];
  char     fmtChunkMarker[4];
  uint32_t lengthOfFmt;
  uint16_t formatType;
  uint16_t channels;
  uint32_t sampleRate;
  uint32_t byterate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char     dataChunkHeader[4];
  uint32_t dataSize;
};

// ============================================================================
// 6. TABELA LOGARITMICA DO EIXO X
// ============================================================================
void initLogFrequencyTable() {
  for (int i = 0; i < (FFT_N / 2); i++) {
    binNoiseFloor[i] = 25.0f;
  }
  for (int c = 0; c < WF_COLS; c++) {
    float t0 = (float)c / (float)WF_COLS;
    float t1 = (float)(c + 1) / (float)WF_COLS;
    int b0 = (int)(3.0f * powf(82.0f, t0) + 0.5f);
    int b1 = (int)(3.0f * powf(82.0f, t1) + 0.5f);
    if (b0 < 3) b0 = 3;
    if (b0 > 248) b0 = 248;
    if (b1 < b0) b1 = b0;
    if (b1 > 249) b1 = 249;
    logBinStart[c] = (uint8_t)b0;
    logBinEnd[c]   = (uint8_t)b1;
  }
}

// ============================================================================
// 7. INTERRUPCAO DE 1ms (WATCHDOG + RELOGIO 1s + BOTAO KEY + BUZZER)
// ============================================================================
extern "C" void HAL_IncTick(void) {
  uwTick += uwTickFreq;
  IWDG->KR = 0xAAAA;

  // Avanca o relogio de tempo real a cada 1000ms (mesmo durante gravacao!)
  rtcSubMs++;
  if (rtcSubMs >= 1000) {
    rtcSubMs = 0;
    tickCalendarOneSecond();
  }

  bool pressed = ((GPIOA->IDR & GPIO_IDR_ID0) == 0);
  if (pressed) {
    if (keyHoldMs < 5000) keyHoldMs++;
    if (keyHoldMs >= 25) keyDown = true;
    if (keyHoldMs == 800 && !keyLongFired) {
      keyLongFired = true;
      manualRecordRequest = true;
    }
  } else {
    if (keyDown) {
      if (!keyLongFired && keyHoldMs >= 25) {
        viewMode = (viewMode + 1) % 3;
        modeChangedFlag = true;
      }
      keyDown = false;
      keyLongFired = false;
    }
    keyHoldMs = 0;
  }

  if (activeBuzzerFreq > 0 && !isRecordingSD) {
    if (activeBuzzerFreq >= 1500) {
      GPIOB->BSRR = (uwTick & 1) ? (1UL << 0) : (1UL << 16);
    } else {
      buzzerTickDiv++;
      if (buzzerTickDiv >= 2) {
        buzzerTickDiv = 0;
        if (GPIOB->ODR & (1UL << 0)) GPIOB->BSRR = (1UL << 16);
        else                         GPIOB->BSRR = (1UL << 0);
      }
    }
  } else {
    GPIOB->BSRR = (1UL << 16);
  }
}

// ============================================================================
// 8. CONTROLE DO BUZZER ASSINCRONO
// ============================================================================
void startBuzzerPattern(uint8_t type) {
  buzzerQueueIdx = 0;
  if (type == 1) {
    buzzerQueue[0] = { 400, 200 };
    buzzerQueue[1] = {   0, 100 };
    buzzerQueue[2] = { 400, 240 };
    buzzerQueueLen = 3;
  } else if (type == 2) {
    buzzerQueue[0] = { 2500, 80 };
    buzzerQueue[1] = {    0, 55 };
    buzzerQueue[2] = { 2500, 80 };
    buzzerQueue[3] = {    0, 55 };
    buzzerQueue[4] = { 2500, 100 };
    buzzerQueueLen = 5;
  } else {
    buzzerQueue[0] = {  500, 90 };
    buzzerQueue[1] = {    0, 45 };
    buzzerQueue[2] = { 2500, 110 };
    buzzerQueueLen = 3;
  }
  activeBuzzerFreq = buzzerQueue[0].freqHz;
  buzzerStepEndMs = millis() + buzzerQueue[0].durationMs;
}

void muteBuzzerImmediately() {
  buzzerQueueLen = 0;
  buzzerQueueIdx = 0;
  activeBuzzerFreq = 0;
  GPIOB->BSRR = (1UL << 16);
}

void updateBuzzerAsync(uint32_t now) {
  if (buzzerQueueIdx >= buzzerQueueLen) {
    activeBuzzerFreq = 0;
    return;
  }
  if ((int32_t)(now - buzzerStepEndMs) >= 0) {
    buzzerQueueIdx++;
    if (buzzerQueueIdx < buzzerQueueLen) {
      activeBuzzerFreq = buzzerQueue[buzzerQueueIdx].freqHz;
      buzzerStepEndMs = now + buzzerQueue[buzzerQueueIdx].durationMs;
    } else {
      activeBuzzerFreq = 0;
      if ((int32_t)(triggerCooldownUntil - (now + 800)) < 0) {
        triggerCooldownUntil = now + 800;
      }
    }
  }
}

// ============================================================================
// 9. INICIALIZACAO DO MICROSD + REGISTRO DO CALLBACK DE DATA/HORA FAT32
// ============================================================================
uint8_t softSpiByte(uint8_t outB, uint8_t sckP, uint8_t mosiP, uint8_t misoP) {
  uint8_t inB = 0;
  for (int b = 7; b >= 0; b--) {
    digitalWrite(mosiP, (outB & (1 << b)) ? HIGH : LOW);
    delayMicroseconds(2);
    digitalWrite(sckP, HIGH);
    delayMicroseconds(2);
    inB = (uint8_t)((inB << 1) | (digitalRead(misoP) ? 1 : 0));
    digitalWrite(sckP, LOW);
    delayMicroseconds(2);
  }
  return inB;
}

uint8_t probeSDPins(uint8_t csP, uint8_t sckP, uint8_t mosiP, uint8_t misoP) {
  pinMode(csP, OUTPUT);
  pinMode(sckP, OUTPUT);
  pinMode(mosiP, OUTPUT);
  pinMode(misoP, INPUT_PULLUP);
  digitalWrite(csP, HIGH);
  digitalWrite(sckP, LOW);
  digitalWrite(mosiP, HIGH);

  uint8_t bestResp = 0xFF;
  for (int att = 0; att < 5; att++) {
    digitalWrite(csP, HIGH);
    for (int i = 0; i < 12; i++) softSpiByte(0xFF, sckP, mosiP, misoP);

    digitalWrite(csP, LOW);
    softSpiByte(0xFF, sckP, mosiP, misoP);
    softSpiByte(0x40, sckP, mosiP, misoP);
    softSpiByte(0x00, sckP, mosiP, misoP);
    softSpiByte(0x00, sckP, mosiP, misoP);
    softSpiByte(0x00, sckP, mosiP, misoP);
    softSpiByte(0x00, sckP, mosiP, misoP);
    softSpiByte(0x95, sckP, mosiP, misoP);

    for (int i = 0; i < 32; i++) {
      uint8_t r = softSpiByte(0xFF, sckP, mosiP, misoP);
      if (r != 0xFF) { bestResp = r; break; }
    }
    digitalWrite(csP, HIGH);
    softSpiByte(0xFF, sckP, mosiP, misoP);
    if (bestResp == 0x01) return 0x01;
    delay(4);
  }
  return bestResp;
}

void initMicroSDWithDiagnostics(bool showScreen) {
  sdAvailable = false;
  RCC->CFGR = (RCC->CFGR & ~(7UL << 13)) | (6UL << 13);

  // Registra o callback de Data e Hora para que todo arquivo criado receba
  // o carimbo exato do momento da gravacao no FAT32!
  SdFile::dateTimeCallback(fatDateTimeCallback);

  uint8_t rOk = probeSDPins(PA4, PA5, PA7, PA6);

  if (showScreen) {
    tft.fillScreen(C_DARK_BG);
    tft.fillRect(0, 0, 160, 18, C_HEADER_BG);
    tft.setTextSize(1);
    tft.setCursor(4, 5);
    tft.setTextColor(C_CYAN, C_HEADER_BG);
    tft.print("DIAGNOSTICO SD + RTC");

    tft.setCursor(4, 24);
    tft.setTextColor(C_WHITE, C_DARK_BG);
    char b[28];
    snprintf(b, sizeof(b), "1.SPI:0x%02X  %02u/%02u/%04u", rOk, rtcDay, rtcMonth, rtcYear);
    tft.print(b);
  }

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  pinMode(SD_MISO_PIN, INPUT_PULLUP);
  SPI.setMISO(SD_MISO_PIN);
  SPI.setMOSI(SD_MOSI_PIN);
  SPI.setSCLK(SD_SCK_PIN);

  bool beginOk = SD.begin(SD_CS_PIN);
  if (!beginOk && rOk == 0x01) {
    RCC->CFGR |= (7UL << 13);
    delay(40);
    beginOk = SD.begin(SD_CS_PIN);
  }

  if (beginOk) {
    if (SD.exists("BOOT_OK.TXT")) SD.remove("BOOT_OK.TXT");
    File f = SD.open("BOOT_OK.TXT", FILE_WRITE);
    if (f) {
      f.println("GRAVACAO SD + DATA/HORA OK!");
      f.flush();
      f.close();
      sdAvailable = true;
    }

    char f1[16], f2[16], f3[16];
    for (uint16_t i = 1; i < 999; i++) {
      snprintf(f1, sizeof(f1), "INF_%03u.WAV", i);
      snprintf(f2, sizeof(f2), "ULT_%03u.WAV", i);
      snprintf(f3, sizeof(f3), "MAN_%03u.WAV", i);
      if (!SD.exists(f1) && !SD.exists(f2) && !SD.exists(f3)) {
        nextEventId = i;
        break;
      }
    }
  }

  if (showScreen) {
    if (sdAvailable) {
      tft.setCursor(4, 44);
      tft.setTextColor(C_GREEN, C_DARK_BG);
      tft.print("2.CARTAO FAT32: OK!");
      tft.setCursor(4, 60);
      char tStr[28];
      snprintf(tStr, sizeof(tStr), "3.HORA: %02u:%02u:%02u ATIVA", rtcHour, rtcMinute, rtcSecond);
      tft.print(tStr);
      tft.setCursor(4, 80);
      tft.setTextColor(C_YELLOW, C_DARK_BG);
      tft.print("4.ULTRASSOM 25kHz OK!");
      delay(1200);
    } else {
      tft.setCursor(4, 44);
      tft.setTextColor(C_RED, C_DARK_BG);
      tft.print("2.SEM CARTAO / ERRO SD");
      delay(1500);
    }
  }
}

// ============================================================================
// 10. GRAVACAO WAV CONTINUA COM CARIMBO DE DATA E HORA DO MOMENTO DA GRAVACAO
// ============================================================================
void recordContinuousAudioToSD(uint8_t eventType) {
  muteBuzzerImmediately();

  if (!sdAvailable) {
    startBuzzerPattern(eventType);
    return;
  }

  // Garante que o callback de Data/Hora esteja ativo no momento exato de abrir o arquivo!
  SdFile::dateTimeCallback(fatDateTimeCallback);

  const char* prefix = (eventType == 1) ? "INF" : ((eventType == 2) ? "ULT" : "MAN");
  snprintf(lastSavedFilename, sizeof(lastSavedFilename), "%s_%03u.WAV", prefix, nextEventId);
  nextEventId++;

  if (SD.exists(lastSavedFilename)) {
    SD.remove(lastSavedFilename);
  }

  File wavFile = SD.open(lastSavedFilename, FILE_WRITE);
  if (!wavFile) {
    sdAvailable = false;
    startBuzzerPattern(eventType);
    return;
  }

  isRecordingSD = true;
  digitalWrite(REC_LED_PIN, LOW);

  uint32_t sRate = (uint32_t)(SAMPLE_RATE_HZ + 0.5f);
  uint32_t totalDataBytes = (uint32_t)TOTAL_REC_CHUNKS * SD_CHUNK_SAMPLES * sizeof(int16_t);

  WavHeader hdr;
  memcpy(hdr.riff, "RIFF", 4);
  hdr.overallSize = totalDataBytes + 36;
  memcpy(hdr.wave, "WAVE", 4);
  memcpy(hdr.fmtChunkMarker, "fmt ", 4);
  hdr.lengthOfFmt = 16;
  hdr.formatType = 1;
  hdr.channels = 1;
  hdr.sampleRate = sRate;
  hdr.bitsPerSample = 16;
  hdr.byterate = sRate * 2;
  hdr.blockAlign = 2;
  memcpy(hdr.dataChunkHeader, "data", 4);
  hdr.dataSize = totalDataBytes;

  wavFile.write((const uint8_t*)&hdr, sizeof(WavHeader));

  int chOffset = (lockedChOffset >= 0) ? lockedChOffset : 0;
  uint32_t dmaWriteNow = (DMA_BUF_LEN - DMA1_Stream0->NDTR) & ~(1UL);
  uint32_t readIdx = (dmaWriteNow + DMA_BUF_LEN - (4096 * 2)) & (DMA_BUF_LEN - 1);

  int32_t fixedDc = (int32_t)dcBaseline;

  for (uint16_t chunk = 0; chunk < TOTAL_REC_CHUNKS; chunk++) {
    while (true) {
      uint32_t writePos = (DMA_BUF_LEN - DMA1_Stream0->NDTR) & ~(1UL);
      uint32_t availWords = (writePos + DMA_BUF_LEN - readIdx) & (DMA_BUF_LEN - 1);
      if (availWords >= (SD_CHUNK_SAMPLES * 2)) {
        break;
      }
    }

    for (int i = 0; i < SD_CHUNK_SAMPLES; i++) {
      int16_t raw = (int16_t)dmaBuffer[(readIdx + chOffset) & (DMA_BUF_LEN - 1)];
      readIdx = (readIdx + 2) & (DMA_BUF_LEN - 1);

      int32_t centered = (int32_t)raw - fixedDc;
      if (centered > 32767)  centered = 32767;
      if (centered < -32768) centered = -32768;
      sdSectorBuf[i] = (int16_t)centered;
    }

    wavFile.write((const uint8_t*)sdSectorBuf, sizeof(sdSectorBuf));

    if ((chunk & 7) == 0 || chunk == TOTAL_REC_CHUNKS - 1) {
      int pct = (int)(((chunk + 1) * 100) / TOTAL_REC_CHUNKS);
      tft.setCursor(3, 118);
      tft.setTextColor(C_RED, C_FOOTER_BG);
      char recBuf[28];
      snprintf(recBuf, sizeof(recBuf), "GRAV %s %3d%% %02u:%02u", lastSavedFilename, pct, rtcHour, rtcMinute);
      tft.print(recBuf);
    }
  }

  wavFile.flush();
  wavFile.close();
  isRecordingSD = false;
  digitalWrite(REC_LED_PIN, HIGH);

  uint32_t now = millis();
  savedBannerUntil = now + 4500;
  triggerCooldownUntil = now + COOLDOWN_AFTER_ALERT_MS;

  startBuzzerPattern(eventType);
}

// ============================================================================
// 11. WATCHDOG E I2S3 + DMA1_STREAM0 (MODO ULTRASSOM ESTENDIDO ~50 kHz!)
// ============================================================================
void initWatchdog() {
  IWDG->KR = 0x5555;
  IWDG->PR = 4;
  IWDG->RLR = 3000;
  IWDG->KR = 0xAAAA;
  IWDG->KR = 0xCCCC;
}

void initI2S3_DMA() {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_DMA1EN;
  RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;

  GPIOA->MODER   &= ~(3UL << (15 * 2));
  GPIOA->MODER   |=  (2UL << (15 * 2));
  GPIOA->OSPEEDR |=  (3UL << (15 * 2));
  GPIOA->AFR[1]  &= ~(0xFUL << ((15 - 8) * 4));
  GPIOA->AFR[1]  |=  (6UL   << ((15 - 8) * 4));

  GPIOB->MODER   &= ~((3UL << (3 * 2)) | (3UL << (5 * 2)));
  GPIOB->MODER   |=  ((2UL << (3 * 2)) | (2UL << (5 * 2)));
  GPIOB->OSPEEDR |=  ((3UL << (3 * 2)) | (3UL << (5 * 2)));
  GPIOB->PUPDR   &= ~((3UL << (3 * 2)) | (3UL << (5 * 2)));
  GPIOB->PUPDR   |=  (2UL << (5 * 2));
  GPIOB->AFR[0]  &= ~((0xFUL << (3 * 4)) | (0xFUL << (5 * 4)));
  GPIOB->AFR[0]  |=  ((6UL   << (3 * 4)) | (6UL   << (5 * 4)));

  RCC->CR &= ~RCC_CR_PLLI2SON;
  uint32_t t0 = millis();
  while ((RCC->CR & RCC_CR_PLLI2SRDY) && (millis() - t0 < 100)) {}
  RCC->PLLI2SCFGR = (16UL << 0) | (192UL << 6) | (2UL << 28);
  RCC->CR |= RCC_CR_PLLI2SON;
  t0 = millis();
  while (!(RCC->CR & RCC_CR_PLLI2SRDY) && (millis() - t0 < 100)) {}

  DMA1_Stream0->CR &= ~DMA_SxCR_EN;
  while (DMA1_Stream0->CR & DMA_SxCR_EN) {}
  DMA1->LIFCR = 0x3D;

  DMA1_Stream0->PAR  = (uint32_t)&(SPI3->DR);
  DMA1_Stream0->M0AR = (uint32_t)dmaBuffer;
  DMA1_Stream0->NDTR = DMA_BUF_LEN;
  DMA1_Stream0->CR   = (0UL << 25)
                     | DMA_SxCR_PL_1
                     | DMA_SxCR_MSIZE_0
                     | DMA_SxCR_PSIZE_0
                     | DMA_SxCR_MINC
                     | DMA_SxCR_CIRC;
  DMA1_Stream0->CR  |= DMA_SxCR_EN;

  SPI3->I2SCFGR = 0;
  SPI3->CR2     = SPI_CR2_RXDMAEN;
  // Divisor 29 (ODD=1, I2SDIV=14) -> Taxa ~50 kHz (Alcance ate 25 kHz!)
  SPI3->I2SPR   = (1UL << 8) | 14UL;
  SPI3->I2SCFGR = SPI_I2SCFGR_I2SMOD
                | (3UL << 8)
                | (0UL << 4)
                | (0UL << 1)
                | SPI_I2SCFGR_CHLEN;
  SPI3->I2SCFGR |= SPI_I2SCFGR_I2SE;
}

// ============================================================================
// 12. FFT RADIX-2 DE 512 PONTOS
// ============================================================================
void computeFFT512() {
  int j = 0;
  for (int i = 0; i < FFT_N; i++) {
    if (i < j) {
      float tr = fftReal[i]; fftReal[i] = fftReal[j]; fftReal[j] = tr;
      float ti = fftImag[i]; fftImag[i] = fftImag[j]; fftImag[j] = ti;
    }
    int m = FFT_N >> 1;
    while (m >= 1 && j >= m) { j -= m; m >>= 1; }
    j += m;
  }

  for (int s = 1; s <= 9; s++) {
    int m = 1 << s;
    int half = m >> 1;
    float theta = -3.1415926535f / (float)half;
    float wpr = cosf(theta);
    float wpi = sinf(theta);

    for (int k = 0; k < FFT_N; k += m) {
      float wr = 1.0f, wi = 0.0f;
      for (int i = 0; i < half; i++) {
        int idx1 = k + i;
        int idx2 = idx1 + half;
        float tr = wr * fftReal[idx2] - wi * fftImag[idx2];
        float ti = wr * fftImag[idx2] + wi * fftReal[idx2];
        fftReal[idx2] = fftReal[idx1] - tr;
        fftImag[idx2] = fftImag[idx1] - ti;
        fftReal[idx1] += tr;
        fftImag[idx1] += ti;

        float wtemp = wr;
        wr = wr * wpr - wi * wpi;
        wi = wtemp * wpi + wi * wpr;
      }
    }
  }

  for (int i = 0; i < (FFT_N / 2); i++) {
    fftMag[i] = sqrtf(fftReal[i] * fftReal[i] + fftImag[i] * fftImag[i]) * (2.0f / FFT_N);
  }
  for (int z = 0; z < 3; z++) fftMag[z] = 0.0f;
}

uint16_t heatColor(uint8_t val) {
  if (val < 22) return C_DARK_BG;
  if (val < 76) {
    uint8_t b = (val - 22) >> 1;
    return (uint16_t)b;
  }
  if (val < 140) {
    uint8_t g = (val - 76);
    return ((uint16_t)g << 5) | 31;
  }
  if (val < 200) {
    uint8_t r = (val - 140) >> 1;
    uint8_t b = 31 - r;
    return ((uint16_t)r << 11) | (63 << 5) | b;
  }
  uint8_t g = 63 - ((val - 200) >> 1);
  return (31 << 11) | ((uint16_t)g << 5);
}

// ============================================================================
// 13. LAYOUT E ANALISE DSP
// ============================================================================
void drawStaticLayout() {
  tft.fillScreen(C_DARK_BG);
  tft.fillRect(0, 0, 160, 21, C_HEADER_BG);
  tft.drawFastHLine(0, 21, 160, C_CYAN);

  tft.fillRect(0, 117, 160, 11, C_FOOTER_BG);
  tft.drawFastHLine(0, 116, 160, C_GRID_AXIS);

  tft.setTextSize(1);
  tft.setCursor(3, 2);
  tft.setTextColor(C_CYAN, C_HEADER_BG);
  if (viewMode == 0)      tft.print("SCOPE/FFT 25kHz");
  else if (viewMode == 1) tft.print("WATERFALL 25kHz");
  else                    tft.print("SISMOGRAFO <20Hz");

  tft.setCursor(100, 2);
  if (sdAvailable) {
    tft.setTextColor(C_GREEN, C_HEADER_BG);
    tft.print("SD:OK ");
  } else {
    tft.setTextColor(C_YELLOW, C_HEADER_BG);
    tft.print("SD:NO ");
  }

  scopeTraceValid = false;
  for (int i = 0; i < 76; i++) {
    fftPeakHoldY[i] = FFT_BOTTOM_Y;
    fftPeakDecay[i] = 0;
  }
}

void captureAndAnalyze(float &outPeakFreq, int &outDb, int &outInfraLevel, bool &outMicOk) {
  static uint32_t prevAnalyzeMs = 0;
  uint32_t nowMs = millis();
  bool justReturnedFromSD = ((nowMs - prevAnalyzeMs) > 200);
  prevAnalyzeMs = nowMs;

  uint32_t ndtr = DMA1_Stream0->NDTR;
  uint32_t writeIdx = (DMA_BUF_LEN - ndtr) & (DMA_BUF_LEN - 1);
  uint32_t baseIdx = (writeIdx + DMA_BUF_LEN - (FFT_N * 2)) & ~(1UL);

  if (lockedChOffset < 0) {
    uint64_t energyEven = 0, energyOdd = 0;
    for (int i = 0; i < 64; i++) {
      int16_t sEven = (int16_t)dmaBuffer[(baseIdx + i * 2)     & (DMA_BUF_LEN - 1)];
      int16_t sOdd  = (int16_t)dmaBuffer[(baseIdx + i * 2 + 1) & (DMA_BUF_LEN - 1)];
      energyEven += (uint32_t)abs((int)sEven);
      energyOdd  += (uint32_t)abs((int)sOdd);
    }
    if (energyEven > 500 || energyOdd > 500) {
      lockedChOffset = (energyOdd > energyEven) ? 1 : 0;
    }
  }
  int chOffset = (lockedChOffset >= 0) ? lockedChOffset : 0;

  float blockSum = 0.0f;
  int16_t minSample = 32767, maxSample = -32768;

  for (int i = 0; i < FFT_N; i++) {
    uint32_t idx = (baseIdx + (i * 2) + chOffset) & (DMA_BUF_LEN - 1);
    int16_t s = (int16_t)dmaBuffer[idx];
    rawSamples[i] = s;
    blockSum += (float)s;
    if (s < minSample) minSample = s;
    if (s > maxSample) maxSample = s;
  }

  outMicOk = ((maxSample - minSample) > 2);
  float blockMean = blockSum / (float)FFT_N;

  uint32_t elapsedBoot = nowMs - bootTime;
  if (!baselineInitialized || elapsedBoot < 2500 || justReturnedFromSD) {
    dcBaseline = blockMean;
    lpInfra = blockMean;
    baselineInitialized = true;
    infraPersistCount = 0;
    ultraPersistCount = 0;
  } else {
    dcBaseline = 0.965f * dcBaseline + 0.035f * blockMean;
    lpInfra    = 0.800f * lpInfra    + 0.200f * blockMean;
  }

  // Subtrai pequena margem de deriva termica (45 LSB) para nao disparar ao tampar o mic
  float infraDelta = fabsf(lpInfra - dcBaseline) - 45.0f;
  if (infraDelta < 0.0f) infraDelta = 0.0f;
  int infLvl = (int)(infraDelta * 0.40f);
  if (infLvl > 99) infLvl = 99;
  if (elapsedBoot < 3500 || justReturnedFromSD) infLvl = 0;
  outInfraLevel = infLvl;
  if (infLvl > maxInfraHold) maxInfraHold = infLvl;

  float half1 = 0.0f, half2 = 0.0f;
  for (int i = 0; i < (FFT_N / 2); i++) {
    half1 += (float)rawSamples[i];
    half2 += (float)rawSamples[i + (FFT_N / 2)];
  }
  half1 /= (float)(FFT_N / 2);
  half2 /= (float)(FFT_N / 2);
  float slope = (half2 - half1) / (float)(FFT_N / 2);

  float sumSq = 0.0f;
  for (int i = 0; i < FFT_N; i++) {
    float detrended = ((float)rawSamples[i] - blockMean) - slope * ((float)i - 255.5f);
    sumSq += detrended * detrended;
    float w = 0.5f * (1.0f - cosf(2.0f * 3.1415926535f * (float)i / (float)(FFT_N - 1)));
    fftReal[i] = detrended * w;
    fftImag[i] = 0.0f;
  }

  float rms = sqrtf(sumSq / (float)FFT_N);
  int db = 30;
  if (rms > 1.0f) {
    db = (int)(28.0f + 20.0f * log10f(rms));
    if (db < 30) db = 30;
    if (db > 110) db = 110;
  }
  outDb = db;

  computeFFT512();

  // 1. Calcula a media de fundo do espectro atual e encontra o maior pico bruto
  int maxBin = 3;
  float maxMag = 0.0f;
  float sumMag = 0.0f;
  for (int i = 3; i < (FFT_N / 2) - 1; i++) {
    sumMag += fftMag[i];
    if (fftMag[i] > maxMag) {
      maxMag = fftMag[i];
      maxBin = i;
    }
  }
  // Media dos outros bins (excluindo o pico principal)
  float avgOtherBins = (sumMag - maxMag) / (float)((FFT_N / 2) - 5);
  if (avgOtherBins < 0.5f) avgOtherBins = 0.5f;

  // 2. Atualiza o piso de ruido APENAS nos bins de fundo (NUNCA sobe no pico de um tom ativo!)
  if (!noiseFloorReady || elapsedBoot < 2200) {
    for (int b = 3; b < (FFT_N / 2); b++) {
      binNoiseFloor[b] = 0.75f * binNoiseFloor[b] + 0.25f * fftMag[b];
    }
    noiseFloorReady = true;
  } else {
    for (int b = 3; b < (FFT_N / 2); b++) {
      // Se este bin estiver próximo do pico ativo ou muito acima da média, CONGELA o piso dele
      // para que um tom continuo de 1 kHz ou 19 kHz nunca seja "apagado" com o tempo!
      if (abs(b - maxBin) <= 3 && maxMag > (avgOtherBins * 3.2f)) {
        continue;
      }
      if (fftMag[b] < binNoiseFloor[b] * 1.6f) {
        binNoiseFloor[b] = 0.94f * binNoiseFloor[b] + 0.06f * fftMag[b];
      }
    }
  }

   // 3. Frequencia de Pico (com sensibilidade extra para Ultrassom de 11.5 kHz ate 25 kHz!)
  bool isUltraBin = (maxBin >= 118); // Bin 118 em diante = 11.5 kHz ate 25 kHz
  float minMagReq = isUltraBin ? 5.2f : 8.0f;
  float minSnrReq = isUltraBin ? (avgOtherBins * 2.5f) : (avgOtherBins * 3.2f);

  if (maxMag < minMagReq || maxMag < minSnrReq) {
    outPeakFreq = 0.0f;
  } else {
    float y1 = fftMag[maxBin - 1];
    float y2 = fftMag[maxBin];
    float y3 = fftMag[maxBin + 1];
    float delta = 0.0f;
    if (y3 >= y1) {
      float alpha = y3 / (y2 + 0.0001f);
      delta = (2.0f * alpha - 1.0f) / (alpha + 1.0f);
    } else {
      float alpha = y1 / (y2 + 0.0001f);
      delta = -(2.0f * alpha - 1.0f) / (alpha + 1.0f);
    }
    float exactBin = (float)maxBin + delta;
    outPeakFreq = exactBin * (SAMPLE_RATE_HZ / (float)FFT_N);
  }

  wfHead = (wfHead == 0) ? (WF_ROWS - 1) : (wfHead - 1);
  wfWavePhase++;

  uint8_t rawCols[WF_COLS];
  for (int c = 0; c < WF_COLS; c++) {
    int b0 = logBinStart[c];
    int b1 = logBinEnd[c];
    float m = 0.0f;
    for (int b = b0; b <= b1; b++) {
      float extraMargin = (c < 22) ? (14.0f + (float)(22 - c) * 1.2f) : 6.5f;
      float cleanVal = fftMag[b] - (binNoiseFloor[b] * 2.0f + extraMargin);
      if (cleanVal > m) m = cleanVal;
    }
    if (m <= 0.0f) {
      rawCols[c] = 0;
    } else {
      int v = (int)(log10f(1.0f + m * 0.70f) * 108.0f);
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      rawCols[c] = (uint8_t)v;
    }
  }

  uint8_t crestFactor = ((wfWavePhase & 3) == 0) ? 100 : (((wfWavePhase & 3) == 2) ? 72 : 86);

  for (int c = 0; c < WF_COLS; c++) {
    int acc = rawCols[c];
    for (int d = 1; d <= 4; d++) {
      int w = (5 - d);
      if (c - d >= 0) {
        int vL = (rawCols[c - d] * w) / 5;
        if (vL > acc) acc = vL;
      }
      if (c + d < WF_COLS) {
        int vR = (rawCols[c + d] * w) / 5;
        if (vR > acc) acc = vR;
      }
    }
    wfLiveX[c] = (uint8_t)acc;
    wfBuffer[wfHead][c] = (uint8_t)((acc * crestFactor) / 100);
  }

  seismoHist[seismoIdx] = (uint8_t)outInfraLevel;
  seismoIdx = (seismoIdx + 1) % GRAPH_W;
}

// ============================================================================
// 14. RENDERIZACAO DAS TELAS (MOSTRA HORA ATUAL NO RODAPE!)
// ============================================================================
void renderHeaderAndFooter(float peakFreq, int db, int infraLevel, bool micOk, uint32_t now) {
  tft.setTextSize(1);

  char topBuf[28];
  snprintf(topBuf, sizeof(topBuf), "F:%5dHz %3ddB INF:%02d", (int)(peakFreq + 0.5f), db, infraLevel);
  tft.setCursor(3, 12);
  tft.setTextColor(C_WHITE, C_HEADER_BG);
  tft.print(topBuf);

  tft.setCursor(3, 118);
  if ((int32_t)(now - savedBannerUntil) < 0) {
    tft.setTextColor(C_GREEN, C_FOOTER_BG);
    char savBuf[28];
    snprintf(savBuf, sizeof(savBuf), "SALVO %s %02u:%02u:%02u ", lastSavedFilename, rtcHour, rtcMinute, rtcSecond);
    tft.print(savBuf);
  } else if ((int32_t)(now - alertBannerUntil) < 0) {
    if (activeAlertType == 1) {
      tft.setTextColor(C_ORANGE, C_FOOTER_BG);
      tft.print("! ALERTA INFRASSOM <20Hz !");
    } else if (activeAlertType == 2) {
      tft.setTextColor(C_MAGENTA, C_FOOTER_BG);
      tft.print("! ALERTA ULTRASSOM >11kHz!");
    } else {
      tft.setTextColor(C_CYAN, C_FOOTER_BG);
      tft.print("! GRAVACAO MANUAL CONCL. !");
    }
  } else {
    tft.setTextColor(C_YELLOW, C_FOOTER_BG);
    char botBuf[28];
    snprintf(botBuf, sizeof(botBuf), "%02u/%02u %02u:%02u:%02u U:%-10s",
             rtcDay, rtcMonth, rtcHour, rtcMinute, rtcSecond, lastSavedFilename);
    tft.print(botBuf);
  }
}

void renderMode0_ScopeFFT() {
  int trigOffset = 0;
  for (int i = 4; i < 200; i++) {
    if (rawSamples[i - 1] < 0 && rawSamples[i] >= 0) {
      trigOffset = i;
      break;
    }
  }

  for (int x = 0; x < GRAPH_W; x++) {
    int s0 = rawSamples[trigOffset + x * 2];
    int s1 = rawSamples[trigOffset + x * 2 + 1];

    int y0 = SCOPE_CENTER_Y - (s0 >> 8);
    int y1 = SCOPE_CENTER_Y - (s1 >> 8);
    if (y0 < GRAPH_Y + 1) y0 = GRAPH_Y + 1;
    if (y0 > SCOPE_BOTTOM_Y) y0 = SCOPE_BOTTOM_Y;
    if (y1 < GRAPH_Y + 1) y1 = GRAPH_Y + 1;
    if (y1 > SCOPE_BOTTOM_Y) y1 = SCOPE_BOTTOM_Y;

    uint8_t ny0 = (uint8_t)((y0 < y1) ? y0 : y1);
    uint8_t ny1 = (uint8_t)((y0 > y1) ? y0 : y1);

    if (scopeTraceValid) {
      uint8_t py0 = prevScopeY0[x];
      uint8_t py1 = prevScopeY1[x];
      if (py0 != ny0 || py1 != ny1) {
        tft.drawFastVLine(x, py0, (py1 - py0) + 1, C_DARK_BG);
        if ((x & 15) == 0) tft.drawPixel(x, SCOPE_CENTER_Y, C_GRID_AXIS);
      }
    }

    tft.drawFastVLine(x, ny0, (ny1 - ny0) + 1, C_GREEN);
    prevScopeY0[x] = ny0;
    prevScopeY1[x] = ny1;
  }
  scopeTraceValid = true;
  tft.drawFastHLine(0, SCOPE_BOTTOM_Y + 1, GRAPH_W, C_GRID_AXIS);

  for (int b = 0; b < 76; b++) {
    int b0 = logBinStart[b];
    int b1 = logBinEnd[b];
    float m = 0.0f;
    for (int k = b0; k <= b1; k++) {
      float clean = fftMag[k] - binNoiseFloor[k];
      if (clean > m) m = clean;
    }

    int barH = (int)(log10f(1.0f + m * 0.45f) * 18.0f);
    if (barH < 0) barH = 0;
    if (barH > 40) barH = 40;

    int x = 4 + b * 2;
    int topY = FFT_BOTTOM_Y - barH;

    if (topY <= fftPeakHoldY[b]) {
      fftPeakHoldY[b] = (uint8_t)topY;
      fftPeakDecay[b] = 0;
    } else {
      fftPeakDecay[b]++;
      if (fftPeakDecay[b] > 2 && fftPeakHoldY[b] < FFT_BOTTOM_Y) {
        tft.drawPixel(x, fftPeakHoldY[b], C_DARK_BG);
        fftPeakHoldY[b]++;
      }
    }

    if (topY > FFT_TOP_Y) tft.drawFastVLine(x, FFT_TOP_Y, topY - FFT_TOP_Y, C_DARK_BG);
    if (barH > 0) {
      uint16_t barColor = (b > 55) ? C_MAGENTA : ((b < 6) ? C_ORANGE : C_CYAN);
      tft.drawFastVLine(x, topY, barH, barColor);
    }
    if (fftPeakHoldY[b] >= FFT_TOP_Y && fftPeakHoldY[b] < FFT_BOTTOM_Y) {
      tft.drawPixel(x, fftPeakHoldY[b], C_YELLOW);
    }
  }
}

void renderMode1_WaterfallBottomUp() {
  GPIOB->BSRR = BSRR_CS_CLR;
  tft.setAddrWindowFast(0, GRAPH_Y, 159, GRAPH_Y + 93);

  for (int r = 0; r < 47; r++) {
    uint8_t age = (uint8_t)(46 - r);
    uint8_t rowIdx = (wfHead + age) % WF_ROWS;
    uint16_t fadePct = (uint16_t)(100 - (age * 45) / 47);

    uint16_t rowColors[WF_COLS];
    for (int c = 0; c < WF_COLS; c++) {
      uint8_t liveBarRows = wfLiveX[c] / 26;
      if (age == 0) {
        rowColors[c] = (wfLiveX[c] > 20) ? C_WHITE : C_CYAN;
      } else if (age <= liveBarRows) {
        rowColors[c] = heatColor(wfLiveX[c]);
      } else {
        uint8_t v = (uint8_t)((wfBuffer[rowIdx][c] * fadePct) / 100);
        rowColors[c] = heatColor(v);
      }
    }

    for (int dupY = 0; dupY < 2; dupY++) {
      for (int c = 0; c < WF_COLS; c++) {
        uint16_t col = rowColors[c];
        tft.spiWrite16(col);
        tft.spiWrite16(col);
      }
    }
  }
  GPIOB->BSRR = BSRR_CS_SET;
}

void renderMode2_Seismograph(int infraLevel) {
  int barW = (infraLevel * 150) / 99;
  uint16_t barCol = (infraLevel >= INFRA_DETECT_THRESHOLD) ? C_RED : C_ORANGE;
  tft.fillRect(5, GRAPH_Y + 4, barW, 8, barCol);
  if (barW < 150) tft.fillRect(5 + barW, GRAPH_Y + 4, 150 - barW, 8, C_GRID_DIM);

  int baseY = GRAPH_Y + 90;
  for (int x = 0; x < GRAPH_W; x++) {
    int idx = (seismoIdx + x) % GRAPH_W;
    int val = seismoHist[idx];
    int h = (val * 68) / 99;
    int topY = baseY - h;

    tft.drawFastVLine(x, GRAPH_Y + 18, topY - (GRAPH_Y + 18), C_DARK_BG);
    if (h > 0) {
      uint16_t col = (val >= INFRA_DETECT_THRESHOLD) ? C_RED : C_YELLOW;
      tft.drawFastVLine(x, topY, h, col);
    } else {
      tft.drawPixel(x, baseY, C_GREEN);
    }
  }
}

// ============================================================================
// 15. SETUP E LOOP PRINCIPAL
// ============================================================================
void setup(void) {
  pinMode(KEY_BTN_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(REC_LED_PIN, OUTPUT);
  digitalWrite(REC_LED_PIN, HIGH);

  initRealtimeClock();
  initLogFrequencyTable();
  initWatchdog();
  tft.initR();
  tft.fillScreen(C_DARK_BG);

  initMicroSDWithDiagnostics(true);
  initI2S3_DMA();

  bootTime = millis();
  lastDspTime = bootTime;
  triggerCooldownUntil = bootTime + 3500;

  buzzerQueue[0] = { 2000, 60 };
  buzzerQueueLen = 1;
  buzzerQueueIdx = 0;
  activeBuzzerFreq = 2000;
  buzzerStepEndMs = bootTime + 60;

  drawStaticLayout();
}

void loop(void) {
  uint32_t now = millis();

  updateBuzzerAsync(now);

  if (modeChangedFlag) {
    modeChangedFlag = false;
    drawStaticLayout();
  }

  if (manualRecordRequest) {
    manualRecordRequest = false;
    activeAlertType = 3;
    alertBannerUntil = now + 4500;
    if (!sdAvailable) {
      initMicroSDWithDiagnostics(true);
      drawStaticLayout();
    }
    recordContinuousAudioToSD(3);
    lastDspTime = millis();
    return;
  }

  if ((uint32_t)(now - lastDspTime) < DSP_INTERVAL_MS) {
    return;
  }
  lastDspTime = now;

  float peakFreq = 0.0f;
  int db = 30;
  int infraLevel = 0;
  bool micOk = false;

  captureAndAnalyze(peakFreq, db, infraLevel, micOk);

  bool warmupDone = ((now - bootTime) > 3500);
  bool cooldownDone = ((int32_t)(now - triggerCooldownUntil) >= 0);

  if (warmupDone && !isRecordingSD && (activeBuzzerFreq == 0) && cooldownDone) {
    if (infraLevel >= INFRA_DETECT_THRESHOLD) {
      if (infraPersistCount < 25) infraPersistCount++;
    } else {
      infraPersistCount = 0;
    }

    if (peakFreq >= ULTRA_FREQ_THRESHOLD_HZ && db >= ULTRA_DB_THRESHOLD) {
      if (ultraPersistCount < 25) ultraPersistCount++;
    } else {
      ultraPersistCount = 0;
    }

    if (infraPersistCount >= PERSISTENCE_FRAMES_REQ) {
      infraPersistCount = 0;
      activeAlertType = 1;
      alertBannerUntil = now + 4500;
      recordContinuousAudioToSD(1);
      lastDspTime = millis();
      return;
    } else if (ultraPersistCount >= PERSISTENCE_FRAMES_REQ) {
      ultraPersistCount = 0;
      activeAlertType = 2;
      alertBannerUntil = now + 4500;
      recordContinuousAudioToSD(2);
      lastDspTime = millis();
      return;
    }
  }

  renderHeaderAndFooter(peakFreq, db, infraLevel, micOk, now);

  if (viewMode == 0) {
    renderMode0_ScopeFFT();
  } else if (viewMode == 1) {
    renderMode1_WaterfallBottomUp();
  } else {
    renderMode2_Seismograph(infraLevel);
  }
}