/*
  LGT8F driving WS2812 using SPI or USART-as-SPI

  This MCU offers 4 bytes SPI FIFO buffer each for output and input.
  With this buffer, we are able to create a constant clocked bitstream, which is a basic requirement for driving those stripes.
  This allows short ISRs while driving the strip, including the timer() ISR (not valid at 8MHz sysclock, it's just toooo slow).
  
  New: USART0 can be used as SPI, too; output pin is D6 then. We only have ONE byte buffer here, but it's sufficent for a clean bitstream.

  Connect the stripe at pin D11/MOSI (USART: D6/TXD-alt)
  Blocks pin D10/SS, sorry (USART: D4/XCK can not be used as output)

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

  2b) WS_TIMING_250: Still-in-specs timing for the "new chips", and still good for the "old" ones:
    Same as 2), but using: 0=250ns/1000ns 1=1000ns/250ns
    THAT would be in spec to the older chips, too, it's exactly on their limits.

  I don't exacly know what kind of stripes I do have, but ALL of them work on ALL of the modes.
  The DO (chain output) signals from the WS (measured by oscilloscope) suggest the old timing:
  - strip type A: 370/920 + 750/500
  - strip type B: 360/910 + 700/550
  They SHOULD be WS2812E as I ordered the ECO strips, but they output the old timing by themselves.

  Oh, and if anyone is asking "what is the ECO version"?
  - they are cheaper (25%)
  - they may use more power (16ma instead 12ma for each color)
  - they do not have a guaranteed "typical" brightness (read: they are darker and may even visually differ in brightness)
  - no 100nf filter capacitor in the chip
  - for processing/soldering of single parts: MSL level 6 only (reflow immediately)
  But they are perfect for your home projects

  Just lets assume that WS_TIMING_250 is a safe setting valid for all types

*/


// how many leds in the strip?
#define NUMBER_OF_LEDS 30

// we can do basic gamma correction (using the mapping table floating around in the net):
//#define GAMMACORRECTION
// switch R+G in output loop (RGB to GRB color order fix), if you have plain RGB buffers from somewhere else
//#define GRB_ON_THE_FLY

// allow ISRs? This will be ignored at 8MHz sysclock
#define ALLOW_ISRS_INBETWEEN

// use USART0 as SPI (if you do not need a lot of serial communication)
//#define USE_USARTSPI

// we can use one of the 3 different timing modes (only define ONE of them!)
//#define WS_TIMING_375 // accurate 8MHz SPI for "old" and "new" chips
#define WS_TIMING_250 // good 4MHz SPI for "new" chips, and not-so-perfect for the "old" chips
//#define WS_TIMING_500 // good 4MHz SPI for "old" chips, and probably not for "new" chips


/*

  ----------------------------------------------------------

  ARDUINO setup() and loop() with a demo rainbow code

  ----------------------------------------------------------

*/

void setup() {
  // setup of the hardware happens once in out display function, so nobody can forget
}


void loop() {
  // example code: do effect
  // from: https://codebender.cc/sketch:80438#Neopixel%20Rainbow.ino
  uint16_t i, j;

  for (j = 0; j < 256; j++) {
    for (i = 0; i < NUMBER_OF_LEDS; i++) {
      byte WheelPos = (2 * (i * 1 + j)) & 255;
      if (WheelPos < 85) {
        setPixel(i, 255 - WheelPos * 3, WheelPos * 3, 0);
      }
      else if (WheelPos < 170) {
        WheelPos -= 85;
        setPixel(i, 0, 255 - WheelPos * 3, WheelPos * 3);
      }
      else {
        WheelPos -= 170;
        setPixel(i, WheelPos * 3, 0, 255 - WheelPos * 3);
      }
    }


    // set first 5 pixels
    setPixel(0, 255, 0, 0); // R
    setPixel(1, 0, 255, 0); // G
    setPixel(2, 0, 0, 255); // B
    setPixel(3, 255, 255, 255); // WHITE
    setPixel(4, 0, 0, 0); // BLACK

    displayWithLimit();    
    delay(20);
  }
}


/*

  ----------------------------------------------------------
  
  Some example functions a usual library will have:

  - a RGB structure
  - setPixel function
  - display function
  - calculate current / limit current (power consumtion)
  
  ----------------------------------------------------------
  
*/



// rgb example struct for the led data buffer
struct cRGB {
#ifdef GRB_ON_THE_FLY
  uint8_t r;
  uint8_t g;
#else
  uint8_t g;
  uint8_t r;
#endif
  uint8_t b;
  set(uint8_t red, uint8_t green, uint8_t blue) {
    r = red;
    g = green;
    b = blue;
  }
} __attribute__((packed));

// buffer for led data
cRGB leds[NUMBER_OF_LEDS];

// example functions: set a led color
void setPixel(int led, uint8_t red, uint8_t green, uint8_t blue) {
  leds[led].set(red, green, blue);
}

// example function: push to stripe, initialize, if not done before
// limit to 500mA to secure the power supply (usb)
boolean wsIsInitialized = false;
void displayWithLimit() {
  if (!wsIsInitialized) {
    setupSpiLeds();
    wsIsInitialized = true;
  }

  outSpiLeds(leds, NUMBER_OF_LEDS,calculateBrightnessForPower(leds,NUMBER_OF_LEDS,500));
}

// Power consumtion defines for the next function.
// !!! MEASURED ON AN WS2812E (ECO) STRIP - VALUES MAY/WILL DIFFER ON OTHER LED CHIPS !!!
// Measured currents: r=11.8ma, g=10ma, b=5.7ma (full-white at 30 LEDs / 1 meter: ~760mA => power drop)
// the defines here are multiplied by 2 to get a slightly better resolution, and rounded down to compensate for voltage drop in the stripe
#define COLOR_R_MA 23 // = 11.5ma
#define COLOR_G_MA 20 // = 10.0ma
#define COLOR_B_MA 11 // =  5.5ma - yes, the blue is not really bright on this stripes

// example function: calculate power usage of the strip, returns mA
// the power usage is very different by color. plus, the voltage drop is not calculated for longer strips with more than 30 LEDs.
// I assume you are a good guy and add power as often as possible.
uint32_t calculateLedPower(void*data, int numleds) {  
  uint32_t total0=0,total1=0,total2=0;
  uint8_t*p = (uint8_t*)data;
  uint8_t*pEnd = p + (numleds * 3);
  uint8_t ccount=0;
  uint8_t val;
  while (p<pEnd) {
#ifdef GAMMACORRECTION
    val=pgm_read_byte(&gamma8[*p++]);
#else
    val=*p++;
#endif

    switch (ccount++) {
      case 0: total0+=val; break;
      case 1: total1+=val; break;
      case 2: total2+=val; ccount=0; break;
    } 
  }

#ifdef GRB_ON_THE_FLY
  total0 = total0*COLOR_R_MA;
  total0+= total1*COLOR_G_MA;
#else
  total0 = total0*COLOR_G_MA;
  total0+= total1*COLOR_R_MA;
#endif
  total0+= total2*COLOR_B_MA;
  return total0>>9;
}

// example function: calculate max brightness for a given max power consumption
uint8_t calculateBrightnessForPower(void*data, int numleds,uint32_t mAmax) {
  uint32_t current=calculateLedPower(data,numleds);
  mAmax=(mAmax<<8)/current;
  return mAmax>255?255:mAmax;
}

/*  

 ----------------------------------------------------------
 
 the "real" proof of concept code - do the bit magic ^^ 
 
 ----------------------------------------------------------
 
 
*/ 

// a gamma correction table, if needed
// will be stored to progmem to keep the ram free
#ifdef GAMMACORRECTION
const uint8_t PROGMEM gamma8[] = {    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,   90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 109, 110, 112, 114,  115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,  144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,  177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,  215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255 };
#endif

#if ((!defined(WS_TIMING_250) && !defined(WS_TIMING_500) && !defined(WS_TIMING_375)) || (defined(WS_TIMING_250) && defined(WS_TIMING_500)) || (defined(WS_TIMING_250) && defined(WS_TIMING_375)) || (defined(WS_TIMING_500) && defined(WS_TIMING_375)))
#pragma GCC error "Define ONE of of the WS_TIMING variants"
#endif

#if F_CPU <= 8000000
#undef ALLOW_ISRS_INBETWEEN
#endif

// macro to read the next color data from buffer; special version to change the byte order
#ifdef GRB_ON_THE_FLY
#define WS_READNEXT *(p+(ccount==0?1:ccount==1?0:2))
#else
#define WS_READNEXT *p++;
#endif

// macro to read the next color data, extended by gamma
#ifdef GAMMACORRECTION
#define WS_READNEXT_WITHGAMMA pgm_read_byte(&gamma8[WS_READNEXT])
#else
#define WS_READNEXT_WITHGAMMA WS_READNEXT
#endif

// macro to:
// - calculate the argument FIRST (will be stored in a register, no local stack memory)
//   to put it to SPI buffer as soon as there is room
// - if the WRFULL (write buffer full) bit is set, wait until not set anymore
// - then put the data into SPI buffer
#ifndef USE_USARTSPI // use the hardware SPI
#define SPIOUT(N) { uint8_t _m=((N));while ((SPFR & _BV(WRFULL))); SPDR=_m; }
#else // use the USART SPI
#define SPIOUT(N) { uint8_t _m=((N));while (!(UCSR0A & _BV(UDRE0))); UDR0=_m; }
#endif

// macro to enable and disable SPI in the loop
#ifndef USE_USARTSPI
#define ENABLE_SPI  SPCR |= 1 << SPE
#define DISABLE_SPI SPCR &= ~(1 << SPE)
#else // for USART, we just "borrow" the pin from and "return" it back to the serial usb interface
#define ENABLE_SPI  PMX0|=1<<WCE;PMX0|=(1<<TXD6)
#define DISABLE_SPI PMX0|=1<<WCE;PMX0&=~(1<<TXD6)
#endif

// macro to short allow ISR, or just disable them
#ifdef ALLOW_ISRS_INBETWEEN
#define TAKE_CARE_OF_ISR SREG = sreg;asm ( "nop;\n" );cli()
#else
#define TAKE_CARE_OF_ISR cli()
#endif


#ifndef USE_USARTSPI

//
// main function: setup SPI controller
//

void setupSpiLeds() {
  // D10 (SS) must be set output, HIGH; SPI may stop working, if not
  // D11 (MOSI) must be set output, as this is will be overriden by SPI hardware
  // It also must be set LOW, because this is the level we need when SPI is done
  fastioWrite(D10, HIGH);
  fastioMode(D10, OUTPUT);
  fastioMode(D11, OUTPUT);
  fastioWrite(D11, LOW);

  // SPI control register: Enable SPI, most significant bit comes first
  SPCR = 0 << SPIE | 1 << SPE | 1 << MSTR;

  // configure the SPI clock for our needs

#ifdef WS_TIMING_375
  // SPI=8M LEDs=800k
#if F_CPU == 32000000   // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 16000000 // :2
  SPSR = 1 << SPI2X;
#else // f_cpu
#pragma GCC error "LGT8SPILED only supports F_CPU 16+32MHz"
#endif // f_cpu
#else // ws_timing
  // SPI=4M LEDs=800k
#if F_CPU == 32000000   // :8
  SPCR |= 1 << SPR0;
  SPSR = 1 << SPI2X;
#elif F_CPU == 16000000 // :4
  SPSR = 0 << SPI2X;
#elif F_CPU == 8000000  // :2
  SPSR = 1 << SPI2X;
#else // f_cpu
#pragma GCC error "LGT8SPILED only supports F_CPU 8+16+32MHz"
#endif // f_cpu
#endif // ws_timing
  // clear the SPFR register
  SPFR = 0; // (WRFULL,WREMPT,WRPTR1,WRPTR2)
  DISABLE_SPI;
}


#else // USE_USARTSPI

//
// main function: setup USART as SPI controller
//

void setupSpiLeds() {
  // we use USART0 as SPI
  // D6 will be used as remapped TXD, to "save" some connected USBSerial from chaos (RXD stays at D0)
  // D4 can NOT be used as OUTPUT (as XCK (SPI clock) will override the port), but still as input
  fastioMode(D6, OUTPUT);
  fastioWrite(D6, LOW);

  // and now setup the USART-SPI mode
  // bitrate divider must be 0 to switch to SPI mode:
  UBRR0 = 0;
  // then we can enable SPI MASTER mode and both receiver and transmitter
  UCSR0C = (1<<UMSEL01)|(1<<UMSEL00);
  UCSR0B = (1<<RXEN0)|(1<<TXEN0);

#ifdef WS_TIMING_375
#if F_CPU>=16000000
  UBRR0=((F_CPU/8000000/2)-1); // SPI=8M LEDs=800k
#else // f_cpu
#pragma GCC error "LGT8SPILED only supports F_CPU 16+32MHz"
#endif // f_cpu
#else // ws_timing
#if F_CPU>=8000000
  UBRR0=((F_CPU/4000000/2)-1); // SPI=4M LEDs=800k
#else // f_cpu  
#pragma GCC error "LGT8SPILED only supports F_CPU 8+16+32MHz"
#endif // f_cpu
#endif // ws_timing
}
#endif //usartspi

// 
// main function: write the buffer to the stripe (with full brightness)
//
void outSpiLeds(void*data, int numleds) {
  outSpiLeds(data,numleds,255);
}

// 
// main function: write the buffer to the stripe (with brightness value)
//
void outSpiLeds(void*data, int numleds, uint8_t brightness) {
  uint8_t*p = (uint8_t*)data;

#ifdef GRB_ON_THE_FLY
  uint8_t ccount = 0;
#endif
  uint8_t *pEnd = p + (numleds * 3);
  uint8_t sreg = SREG;  // fetch the current status register, including the ISR allow flag
  
  TAKE_CARE_OF_ISR;
  // enable SPI, will start taking over the D11 (USART=D6) port
  ENABLE_SPI;
  
  while (p < pEnd) {
    uint8_t val = WS_READNEXT_WITHGAMMA;
    if (brightness!=255) {
      val=(val*brightness)>>8;
    }
    TAKE_CARE_OF_ISR;

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
    TAKE_CARE_OF_ISR;
    // E=8 F=4 G=2 H=1
    SPIOUT(val & 8 ? fb1 << 1 : fb0 << 1);                                        //E
    SPIOUT(val & 4 ? fb1 >> 1 : fb0 >> 1);                                        //F
    SPIOUT((val & 2 ? fb1 >> 3 : fb0 >> 3) | (val & 4 ? fb1 << 5 : fb0 << 5));    //F+G
    SPIOUT((val & 2 ? fb1 << 5 : fb0 << 5) | 3); // G 2 msbs  are always 1        //G+H
    SPIOUT(val & 1 ? fb1 << 3 : fb0 << 3);                                        //H
#endif // ws_timing

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
#endif // ws_timing

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
#endif // ws_timing

#ifdef GRB_ON_THE_FLY
    if (++ccount == 3) {
      p += ccount;
      ccount = 0;
    }
#endif
  }

  // keep D11 (USART=D6) line low - if not, output signal becomes high state after buffer emptied
  TAKE_CARE_OF_ISR;
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  SPIOUT(0);
  // disable SPI, even while SPI transferring bytes in the buffer (hardware SPI only)
  // thats ok, as we just need a constant LOW level on D11 now
  // we MAY see a very short HIGH spike, but it is shorter than the WS2812 cares of
  // after disabling, D11 (USART=D6) is back to regular GPIO control
  DISABLE_SPI;

  // allow ISRs again
  SREG = sreg;
}
