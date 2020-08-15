/*
  LGT8F driving WS2812 using SPI

  This mcu offers 4 bytes SPI FIFO buffer each for output and input
  With this buffer, we are able to create a constant clocked bitstream, which is a basic requirement for driving those stripes.
  This allows short ISRs while driving the strip, including the timer() ISR (not valid at 8MHz sysclock, it's just toooo slow).

  Connect the stripe at pin D11/MOSI
  Blocks pin D10/SS, sorry

  This is just a proof of concept: push bytes through SPI
  Output is not color-ordered (1:1 byte-to-bitstream output)
  - take care of your color ordering yourself (GRB is default on WS2812)
  - you may define GRB_ON_THE_FLY to flip R+G in the output loop

  All that bit checking and shifting will get perfectly optimized by the compiler, even if it looks complicated


  How does this work?
  
  The WS2811/12/13/..-series of LEDs do need 24 bits of color information (8 bits for each basic color red/green/blue, in some chip specific color order).
  These bits are transmitted using a special, pulse-width based serial one-wire protocol:
  A "1" bit is sent as a long HIGH amd a short LOW, as a "0" bit is a short HIGH and a long LOW level.
  
  We now abuse a hardware byte serializer (SPI device) to fake the needed protocol by sending a couple of 1 bits for the HIGH level, and a couple 
  of 0 bits for the LOW level. The number of bits defines the length of the HIGH or LOW level sent.
  As an example, if we send a "10000000" through SPI with 8MHz, we would see a HIGH level of 125ns (nanoseconds), and a LOW level of 875ns, for 
  a total cycle of 1000ns (1/8th of 8MHz). Unfortunately, the WS LEDs need a total cycle time (HIGH+LOW phase) of 1250ns, which equates to 800kHz. To 
  accomplish that, we need to not use 8 bits for a cycle, but 10 (at 8MHz) or 5 (at 4MhZ). The code needs to convert the 8 bit color data into 80 (or 40) 
  bits, which is 10 (or 5) bytes. Back to a correct byte boundary, we can write a very simple code.

  Example again for a 40-bit transfer:
  The code needs to send the first color byte 200 (decimal, which is C8 in hex or 11001000 in binary). These 8 bits (7-0, highest first) have to be packed 
  into a bitstream. From timing definitions (see below), we know we need to send "10000" binary for a "0" and "11110" binary for a 1, at 4MHz SPI speed.
  So, we split the 8 bits of the input data ("200") into 8 blocks each 5 bits:

  1 => 11110
  1 => 11110
  0 => 10000
  0 => 10000
  1 => 11110
  0 => 10000
  0 => 10000
  0 => 10000

  and combine them to a total of 40 bits in a row:

  11110111 10100001 00001111 01000010 00010000  

  These 5 bytes are sent timing-accurate using the SPI serializer hardware, and the WS LEDs will understand a color value of "200".
  Three of these values make up a full color (red, green, blue). Now, each of the LEDs in a strip want their own color, so have to to send 
  this for each LED. The first (receiving) LED transfers this data to the next one, as soon as it receives new data for itself. This chain 
  works "by itself". Having 30 LEDs, makes up 90 color values (30 times red, green, blue), and 450 bytes to be transferred.
  When no new data is sent in a specified time, all the LEDs will "latch": switch in parallel to the last color they received for themselves.

  Easy, isn't it? ;)
   

  

  WS2812 LED chip timing:

  Official timings from WORLDSEMI datasheet I used (for models B-v3, D, S):
   0-Bit: HIGH=400ns, LOW=850ns  (±150ns tolerance)
   1-Bit: HIGH=850ns, LOW=400ns  (±150ns tolerance)
  Which means:
   0-Bit: HIGH=250-550ns,  LOW=700-1000ns
   1-Bit: HIGH=700-1000ns, LOW= 250-550ns
  
  There is a "new" timing for models B-v5,C,Mini and WS2813+, which is:
   0-Bit: HIGH=220-380ns,  LOW=580-1600ns
   1-Bit: HIGH=580-1600ns, LOW=220- 420 ns



  There a a LOT of this 2812 LEDs around, and depending on the company, model suffix, and sub-version, timing differ.
  
  - WS2812B V3    250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811151649_Worldsemi-WS2812B-V3_C114585.pdf
  - WS2812D       250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811021523_Worldsemi-WS2812D-F8_C139126.pdf
  - WS2812S       250-550ns/700-1000ns + 700-1000ns/250-550ns     https://datasheet.lcsc.com/szlcsc/1811011939_Worldsemi-WS2812S_C114584.pdf

  After that, they changed in the datasheet:
  - WS2812E (ECO) 220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1811151230_Worldsemi-WS2812E_C139127.pdf
  - WS2812B-B V5  220-380ns/580-1000ns + 580-1000ns/580-1000ns    https://datasheet.lcsc.com/szlcsc/2006151006_Worldsemi-WS2812B-B_C114586.pdf
                  This IS some documentation bug with the T1L, yes ^^
  - WS2812C       220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1810231210_Worldsemi-WS2812C_C114587.pdf
  - WS2813-Mini   220-380ns/580-1600ns + 580-1600ns/220-420ns     https://datasheet.lcsc.com/szlcsc/1810010024_Worldsemi-WS2813-Mini-WS2813-3535_C189639.pdf
  


  This code has 2 (and a half) timing "modes":

  1) WS_TIMING_375: Very accurate timing for "old chips", and still in spec with the "new chips":
    10 bits each bit @8M SPI: 1250ns cycle time, 0=375ns/875ns 1=875ns/375ns
    Only works with 32MHz and 16MHz clocked MCU, as we need 8MHz SPI speed.

  2) WS_TIMING_500: Still-in-specs timing for the "old chips":
    5 bits each bit @4M SPI: 0=500ns/750ns 1=750ns/500ns
    Will also work with 8MHz clocked MCU, as we only use 4MHz SPI speed
    
  2b) WS_TIMING_250: Still-in-specs timing for the "new chips":
    Same as 2), but using: 0=250ns/1000ns 1=1000ns/250ns
    Though THAT would be in spec to the older chips, too, it's exactly on their limits.
   
  I don't exacly know what kind of stripes I do have, but ALL of them work on ALL of the modes.
  The DO (chain output) signals from the WS (measured by oscilloscope) suggest the old timing:
  - strip type A: 370/920 + 750/500
  - strip type B: 360/910 + 700/550
  They SHOULD be WS2812E as I ordered the ECO strips, but they output the old timing by themselves.
  
  Oh, and if anyone is asking "what is the ECO version"?
  - they are cheaper (25%)
  - they may use more power (16ma instead 12ma for each color)
  - they do not have a guaranteed "typical" brightness (read: the are darker)
  - no 100nf filter capacitor in the chip
  - for processing/soldering of single parts: MSL level 6 only (reflow immediately)
  But they are perfect for some home projects
  
  Just lets assume that WS_TIMING_250 is a safe setting valid for all types

*/

// how many leds in the strip?
#define LEDS 60

// we can do basic gamma correction (using the mapping table floating around in the net):
//#define GAMMACORRECTION
// switch R+G in output loop (RGB to GRB color order fix)
//#define GRB_ON_THE_FLY

// allow ISRs? This will be ignored at 8MHz sysclock
#define ALLOW_ISRS_INBETWEEN


// we can use one of the 3 different timing modes (only undef ONE of them!)
//#define WS_TIMING_375 // accurate 8MHz SPI for "old" and "new" chips
#define WS_TIMING_250 // good 4MHz SPI for "new" chips, and not-so-perfect for the "old" chips
//#define WS_TIMING_500 // good 4MHz SPI for "old" chips, and probably not for "new" chips


// The LED buffer is BYTE based, you need to take care yourself of the byte order for each color:
uint8_t buffer[LEDS * 3];

void setup() {
  setupSpiLeds();
}

void loop() {
  rainbowBRG();
}



void setupSpiLeds() {
  fastioWrite(D10, HIGH);
  fastioMode(D10, OUTPUT);
  fastioMode(D11, OUTPUT);
  fastioWrite(D11, LOW);

  SPCR = 0 << SPIE | 1 << SPE | 1 << MSTR;

#if ((!defined(WS_TIMING_250) && !defined(WS_TIMING_500) && !defined(WS_TIMING_375)) || (defined(WS_TIMING_250) && defined(WS_TIMING_500)) || (defined(WS_TIMING_250) && defined(WS_TIMING_375)) || (defined(WS_TIMING_500) && defined(WS_TIMING_375)))
  #pragma GCC error "Define ONE of of the WS_TIMING variants"
#endif

#ifdef WS_TIMING_375
  // SPI=8M LEDs=800k
#if F_CPU == 32000000   // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 16000000 // :2
  SPSR = 1 << SPI2X;
#else
#pragma GCC error "LGT8SPILED only supports F_CPU 16+32MHz"
#endif

#else
  // SPI=4M LEDs=800k
#if F_CPU == 32000000   // :8
  SPCR |= 1 << SPR0;
  SPSR = 1 << SPI2X;
#elif F_CPU == 16000000 // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 8000000  // :2
#undef ALLOW_ISRS_INBETWEEN
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

#ifdef GRB_ON_THE_FLY
#define WS_READNEXT *(p+(ccount^1))
#else
#define WS_READNEXT *p++;
#endif

#define SPIOUT(N) { uint8_t _m=((N));while ((SPFR & _BV(WRFULL))); SPDR=_m; }

void outSpiLeds(uint8_t*p, int leds) {
#ifdef GRB_ON_THE_FLY
  uint8_t ccount=0;
#endif
  uint8_t *pEnd = p+(leds*3);

  uint8_t sreg = SREG;

#ifndef ALLOW_ISRS_INBETWEEN
    // disable ISRs for all
    cli();
#endif    

  SPCR |= 1 << SPE; // enable SPI

  while (p<pEnd) {
#ifdef GAMMACORRECTION
    
    uint8_t val = pgm_read_byte(&gamma8[WS_READNEXT]);
#else
    uint8_t val = WS_READNEXT;
#endif

#ifdef ALLOW_ISRS_INBETWEEN
    // disable ISRs for now
    cli();
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
    SPIOUT(val & 128 ? fb1 << 1 : fb0 << 1);                                      //A
    SPIOUT(val & 64 ? fb1 >> 1 : fb0 >> 1);                                       //B
    SPIOUT((val & 32 ? fb1 >> 3 : fb0 >> 3) | (val & 64 ? fb1 << 5 : fb0 << 5));  //B+C
    SPIOUT((val & 32 ? fb1 << 5 : fb0 << 5) | 3); // D 2 msbs are always 1        //C+D
    SPIOUT(val & 16 ? fb1 << 3 : fb0 << 3);                                       //D

#ifdef ALLOW_ISRS_INBETWEEN
    // allow ISRs for a short moment, which may have queued up:
    SREG = sreg;
    asm ( "nop;\n" );
    cli();
#endif

    // E=8 F=4 G=2 H=1
    SPIOUT(val & 8 ? fb1 << 1 : fb0 << 1);                                        //E
    SPIOUT(val & 4 ? fb1 >> 1 : fb0 >> 1);                                        //F
    SPIOUT((val & 2 ? fb1 >> 3 : fb0 >> 3) | (val & 4 ? fb1 << 5 : fb0 << 5));    //F+G
    SPIOUT((val & 2 ? fb1 << 5 : fb0 << 5) | 3); // G 2 msbs  are always 1        //G+H
    SPIOUT(val & 1 ? fb1 << 3 : fb0 << 3);                                        //H
#endif

#ifdef WS_TIMING_500
    // 500/750 timing:
    // 8 bits to 40 bits: AAA00BBB 00CCC00D DD00EEE0 0FFF00GG G00HHH00
    //                    11x0011x 0011x001 1x0011x0 011x0011 x0011x00

    // defines: only the msb 3 bits, as the 2 lsb are always 0
    #define fb0 0b00000110
    #define fb1 0b00000111

    // A=128 B=64 C=32 D=16 E=8 F=4 G=2 H=1
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
    SPIOUT((val & 128 ? fb1 << 4 : fb0 << 4) | (val & 64 ? fb1 >> 1 : fb0 >> 1));                             //A+B
    SPIOUT((val & 64 ? fb1 << 7 : fb0 << 7 ) | (val & 32 ? fb1 << 2 : fb0 << 2) | 1); // D msb is 1 always    //B+C+D
    SPIOUT((val & 16 ? fb1 << 5 : fb0 << 5) | (val & 8 ? fb1 : fb0));                                         //D+E
    SPIOUT((val & 4 ? fb1 << 3 : fb0 << 3) | (val & 2 ? fb1 >> 2 : fb0 >> 2));                                //F+G
    SPIOUT((val & 2 ? fb1 << 6 : fb0 << 6) | (val & 1 ? fb1 << 1 : fb0 << 1));                                //G+H
#endif

#ifdef ALLOW_ISRS_INBETWEEN
    // allow ISRs again
    SREG = sreg;

#ifdef GRB_ON_THE_FLY
    ccount++;
    if (ccount==3) { ccount=0; p+=3; }
#endif
    
#endif

  }


  // keep line low and switch SPI off while low - if not, output signal becomes high state after buffer emptied
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPCR &= ~(1 << SPE); // disable SPI

  #ifndef ALLOW_ISRS_INBETWEEN
    // allow ISRs again
    SREG = sreg;
  #endif

}


// from: https://codebender.cc/sketch:80438#Neopixel%20Rainbow.ino
void rainbowBRG() {
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
