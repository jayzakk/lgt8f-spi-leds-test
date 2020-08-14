// LGT8F WS2812B (B only!) using SPI (and 4 bytes SPI buffer)
// Connect the stripe at pin D11/MOSI
// Blocks pin D10/SS, sorry
// should help out with "longer" ISRs (softwareserial)


#define LEDS 30
//#define GAMMACORRECTION
uint8_t buffer[LEDS * 3];

void setup() {
  setupSpiLeds();
}

void loop() {
  rainbow();
}



void setupSpiLeds() {
  fastioWrite(D10, HIGH);
  fastioMode(D10, OUTPUT);
  fastioMode(D11, OUTPUT);
  fastioWrite(D11, LOW);

  SPCR = 0 << SPIE | 1 << SPE | 1 << MSTR;
#if F_CPU == 32000000 // SPI=8M LEDs=800k
  SPSR = 0; // SPI2X
#elif F_CPU == 16000000 // SPI=8M LEDs=800k
  SPSR = 1; // SPI2X
#else
#pragma warn "LGT8SPILED only supports F_CPU 16+32MHz"
#endif
  SPFR = 0; // (WRFULL,WREMPT,WRPTR1,WRPTR2)
  SPCR = 0; // disable for now
  
}


#ifdef GAMMACORRECTION
const uint8_t PROGMEM gamma8[] = {    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };
#endif

#define b0    0b11100000
#define b1    0b11111100
#define b0R2  0b00111000
#define b1R2  0b00111111
#define b0R4  0b00001110
#define b1R4  0b00001111
#define b_R6  0b00000011
#define b0L2  0b10000000
#define b1L2  0b11110000
#define b0L4  0b00000000
#define b1L4  0b11000000

#define SPIOUT(N) { uint8_t _m=((N));while ((SPFR & _BV(WRFULL))); SPDR=_m; }

void outSpiLeds(uint8_t*p, int leds) {
  int count = leds * 3;

  uint8_t sreg = SREG;
  SPCR = 0 << SPIE | 1 << SPE | 1 << MSTR; // enable SPI

  while (count-- > 0) {
    #ifdef GAMMACORRECTION
    uint8_t NIB = pgm_read_byte(&gamma8[*p++]);
    #else
    uint8_t NIB = *p++;
    #endif
    cli();
    SPIOUT(NIB & 128 ? b1 : b0);
    SPIOUT(NIB & 64 ? b1R2 : b0R2);
    SPIOUT(NIB & 32 ? b1R4 : b0R4);
    SPIOUT((NIB & 32 ? b1L4 : b0L4) | b_R6);
    SPIOUT(NIB & 16 ? b1L2 : b0L2);
    SREG = sreg;
    asm ( "nop;\n" );
    cli();
    SPIOUT(NIB & 8 ? b1 : b0);
    SPIOUT(NIB & 4 ? b1R2 : b0R2);
    SPIOUT(NIB & 2 ? b1R4 : b0R4);
    SPIOUT((NIB & 2 ? b1L4 : b0L4) | b_R6);
    SPIOUT(NIB & 1 ? b1L2 : b0L2);
    SREG = sreg;
  }

  // keep low and switch off - if not, has high state
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPCR=0;
}


// from: https://codebender.cc/sketch:80438#Neopixel%20Rainbow.ino
void rainbow() {
  uint16_t i, j;

  for (j = 0; j < 256; j++) {
    uint8_t*p = buffer;
    for (i = 0; i < LEDS; i++) {
      byte WheelPos = (2 * (i * 1 + j)) & 255;
      if (WheelPos < 85) {
        *p++ = WheelPos * 3;
        *p++ = 255 - WheelPos * 3;
        *p++ = 0;
      }
      else if (WheelPos < 170) {
        WheelPos -= 85;
        *p++ = 255 - WheelPos * 3;
        *p++ = 0;
        *p++ = WheelPos * 3;
      }
      else {
        WheelPos -= 170;
        *p++ = 0;
        *p++ = WheelPos * 3;
        *p++ = 255 - WheelPos * 3;
      }
    }
    outSpiLeds(buffer, LEDS);
    delay(20);
  }
}
