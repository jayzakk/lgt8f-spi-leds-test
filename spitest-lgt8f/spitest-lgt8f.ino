/*
  LGT8F driving WS2812 using SPI

  This mcu offers 4 bytes SPI FIFO buffer each for output and input
  With this buffer, we are able to create a constant clocked bitstream, which is a basic requirement for driving those stripes

  Connect the stripe at pin D11/MOSI
  Blocks pin D10/SS, sorry

  This is just a proof of concept: push bytes through SPI
  We do not take care of cRGB() or setPixel() things
  Output is not color-ordered (1:1 byte-to-bitstream output)
  Take care of your color ordering yourself

  All that bit checking and shifting will get perfectly optimized by the compiler, even if it looks complicated


  WS2812 chip timing:

  Official timings from WORLDSEMI datasheet I used (for models B-v3, D, S):
   0-Bit: HIGH=400ns, LOW=850ns
   1-Bit: HIGH=850ns, LOW=400ns

  They allow ±150ns tolerance, which means:
   400ns is anything from 250ns to 550ns
   850ns is anything from 700ns to 1000ns

  There is a "new" timing for models B-v5,C,Mini and WS2813+, which is:
   0-Bit: HIGH=220-380ns,  LOW=580-1600ns
   1-Bit: HIGH=580-1600ns, LOW=220- 420 ns



  There a a LOT of this 2812 LEDs around, and depending on the company, appendix, and sub-version, timing differ.
  
  - WS2812B V3    250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811151649_Worldsemi-WS2812B-V3_C114585.pdf
  - WS2812D       250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811021523_Worldsemi-WS2812D-F8_C139126.pdf
  - WS2812S       250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811011939_Worldsemi-WS2812S_C114584.pdf

  After that, they change:
  - WS2812E       220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1811151230_Worldsemi-WS2812E_C139127.pdf
  - WS2812B-B V5  220-380ns/580-1000ns + 580-1000ns/580-1000ns    https://datasheet.lcsc.com/szlcsc/2006151006_Worldsemi-WS2812B-B_C114586.pdf
                  This IS some documentation bug with the 1-low-time, yes ^^
  - WS2812C       220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1810231210_Worldsemi-WS2812C_C114587.pdf
  - WS2813-Mini   220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1810010024_Worldsemi-WS2813-Mini-WS2813-3535_C189639.pdf
  


  This code has 2 (and a half) timing "modes":

  1) WS_TIMING_375: Very accurate timing for "old chips", and still in spec with the "new chips":
    10 bits each bit @8M SPI: 1250ns cycle time, 0=375ns/875ns 1=875ns/375ns
    Only works with 32MHz and 16MHz clocked MCU, as we need 8MHz SPI speed.
    Uncomment the EXACT_LED_TIMING to use this. Results in larger code (and more CPU used, but we're waiting a lot anyways)

  2) WS_TIMING_500: Still-in-specs timing for the "old chips":
    5 bits each bit @4M SPI: 0=500ns/750ns 1=750ns/500ns
    Will also work with 8MHz clocked MCU, as we only use 4MHz SPI speed
    
  2b) WS_TIMING_250: Still-in-specs timing for the "new chips":
    Same as 2), but using: 0=250ns/1000ns 1=1000ns/250ns
    Though THAT would be in spec to the older chips, too, it's exactly on their limits.
   
I don't exacly know what kind of stripes I do have, but ALL of them work on ALL of the modes...

*/

// how many leds in the strip?
#define LEDS 30

// we can do basic gamma correction (using the mapping table floating around in the net):
//#define GAMMACORRECTION

// we can use one of the 3 different timing modes (only undef ONE of them!)
//#define WS_TIMING_375 // accurate 8MHz SPI for "old" and "new" chips
#define WS_TIMING_250 // good 4MHz SPI for "new" chips, and not-so-good for the "new" chips
//#define WS_TIMING_500 // good 4MHz SPI for "old" chips, and probably not for "new" chips

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

#if !defined(WS_TIMING_250) && !defined(WS_TIMING_500) && !defined(WS_TIMING_375)
  #pragma GCC error "Define ONE of of the WS_TIMING variants"
#endif

#ifdef WS_TIMING_375
  #if defined(WS_TIMING_250) || defined(WS_TIMING_500)
  #pragma GCC error "Define ONE of of the WS_TIMING variants"
  #endif
  // SPI=8M LEDs=800k
#if F_CPU == 32000000   // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 16000000 // :2
  SPSR = 1 << SPI2X;
#else
#pragma GCC error "LGT8SPILED only supports F_CPU 16+32MHz"
#endif

#else
  #if defined(WS_TIMING_250) && defined(WS_TIMING_500)
  #pragma GCC error "Define ONE of of the WS_TIMING variants"
  #endif

  // SPI=4M LEDs=800k
#if F_CPU == 32000000   // :8
  SPCR |= 1 << SPR0;
  SPSR = 1 << SPI2X;
#elif F_CPU == 16000000 // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 8000000  // :2
  SPSR = 1 << SPI2X;
#else
#pragma GCC error "LGT8SPILED only supports F_CPU 8+16+32MHz"
#endif


#endif

  SPFR = 0; // (WRFULL,WREMPT,WRPTR1,WRPTR2)
  SPCR &= ~(1 << SPE); // disable SPI

}


#ifdef GAMMACORRECTION
const uint8_t PROGMEM gamma8[] = {    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,   90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 109, 110, 112, 114,  115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,  144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,  177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,  215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255 };
#endif

#define SPIOUT(N) { uint8_t _m=((N));while ((SPFR & _BV(WRFULL))); SPDR=_m; }

void outSpiLeds(uint8_t*p, int leds) {
  int count = leds * 3;

  uint8_t sreg = SREG;
  SPCR |= 1 << SPE; // enable SPI

  while (count-- > 0) {
#ifdef GAMMACORRECTION
    uint8_t val = pgm_read_byte(&gamma8[*p++]);
#else
    uint8_t val = *p++;
#endif

#ifdef WS_TIMING_375
    // 375/875 timing:
    // 8 bits to 80 bits: AAAAAAA0 00BBBBBB B000CCCC CCC000DD DDDDD000  (upper nibble)
    //                    EEEEEEE0 00FFFFFF F000GGGG GGG000HH HHHHH000  (lower nibble)
    //                    111xxxx0 00111xxx x000111x xxx00011 1xxxx000

    // defines: only the msb 7 bits, as the 3 lsb are always 0
    #define fb0    0b01110000
    #define fb1    0b01111111

    // A=128 B=64 C=32 D=16
    cli();
    SPIOUT(val & 128 ? fb1 << 1 : fb0 << 1);                                      //A
    SPIOUT(val & 64 ? fb1 >> 1 : fb0 >> 1);                                       //B
    SPIOUT((val & 32 ? fb1 >> 3 : fb0 >> 3) | (val & 64 ? fb1 << 5 : fb0 << 5));  //B+C
    SPIOUT((val & 32 ? fb1 << 5 : fb0 << 5) | 3); // D 2 msbs are always 1        //C+D
    SPIOUT(val & 16 ? fb1 << 3 : fb0 << 3);                                       //D
    SREG = sreg;
    asm ( "nop;\n" );
    // E=8 F=4 G=2 H=1
    cli();
    SPIOUT(val & 8 ? fb1 << 1 : fb0 << 1);                                        //E
    SPIOUT(val & 4 ? fb1 >> 1 : fb0 >> 1);                                        //F
    SPIOUT((val & 2 ? fb1 >> 3 : fb0 >> 3) | (val & 4 ? fb1 << 5 : fb0 << 5));    //F+G
    SPIOUT((val & 2 ? fb1 << 5 : fb0 << 5) | 3); // G 2 msbs  are always 1        //G+H
    SPIOUT(val & 1 ? fb1 << 3 : fb0 << 3);                                        //H
    SREG = sreg;
#endif

#ifdef WS_TIMING_500
    // 500/750 timing:
    // 8 bits to 40 bits: AAA00BBB 00CCC00D DD00EEE0 0FFF00GG G00HHH00
    //                    11x0011x 0011x001 1x0011x0 011x0011 x0011x00

    // defines: only the msb 3 bits, as the 2 lsb are always 0
    #define fb0 0b00000110
    #define fb1 0b00000111

    // A=128 B=64 C=32 D=16 E=8 F=4 G=2 H=1
    cli();
    SPIOUT((val & 128 ? fb1 << 5 : fb0 << 5) | (val & 64 ? fb1 : fb0));           //A+B
    SPIOUT((val & 32 ? fb1 << 3 : fb0 << 3) | 1); // D msb is 1 always            //C+D
    SPIOUT((val & 16 ? fb1 << 6 : fb0 << 6) | (val & 8 ? fb1 << 1 : fb0 << 1));   //D+E
    SPIOUT((val & 4 ? fb1 << 4 : fb0 << 4) | 3); // G 2 msbs are always 1         //F+G
    SPIOUT((val & 2 ? fb1 << 7 : fb0 << 7) | (val & 1 ? fb1 << 2 : fb0 << 2));    //G+H

    /*  this looks MUCH nicer, but is 12 bytes longer!
        SPIOUT( 0b11000110 | ((val&128)>>2) | ((val&64)>>6) );
        SPIOUT( 0b00110001 | ((val&32)>>2) );
        SPIOUT( 0b10001100 | ((val&16)<<2) | ((val&8)>>2) );
        SPIOUT( 0b01100011 | ((val&4)<<2) );
        SPIOUT( 0b00011000 | ((val&2)<<6) | ((val&1)<<2) );
    */
#endif

#ifdef WS_TIMING_250
    // 250/1000 timing:
    // 8 bits to 40 bits: AAAA0BBB B0CCCC0D DDD0EEEE 0FFFF0GG GG0HHHH0
    //                    1xxx01xx x01xxx01 xxx01xxx 01xxx01x xx01xxx0

    // defines: only the msb 4 bits, as the 1 lsb is always 0
    #define fb0 0b00001000
    #define fb1 0b00001111
    cli();
    SPIOUT((val & 128 ? fb1 << 4 : fb0 << 4) | (val & 64 ? fb1 >> 1 : fb0 >> 1));                             //A+B
    SPIOUT((val & 64 ? fb1 << 7 : fb0 << 7 ) | (val & 32 ? fb1 << 2 : fb0 << 2) | 1); // D msb is 1 always    //B+C+D
    SPIOUT((val & 16 ? fb1 << 5 : fb0 << 5) | (val & 8 ? fb1 : fb0));                                         //D+E
    SPIOUT((val & 4 ? fb1 << 3 : fb0 << 3) | (val & 2 ? fb1 >> 2 : fb0 >> 2));                                //F+G
    SPIOUT((val & 2 ? fb1 << 6 : fb0 << 6) | (val & 1 ? fb1 << 1 : fb0 << 1));                                //G+H
#endif

    SREG = sreg;

  }

  // keep line low and switch SPI off - if not, output signal has 1 state
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPCR &= ~(1 << SPE); // disable SPI
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
