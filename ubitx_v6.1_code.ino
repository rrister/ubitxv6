/**
  This source file is under General Public License version 3.

  This verision uses a built-in Si5351 library
  Most source code are meant to be understood by the compilers and the computers.
  Code that has to be hackable needs to be well understood and properly documented.
  Donald Knuth coined the term Literate Programming to indicate code that is written be
  easily read and understood.

  The Raduino is a small board that includes the Arduin Nano, a TFT display and
  an Si5351a frequency synthesizer. This board is manufactured by HF Signals Electronics Pvt Ltd

  To learn more about Arduino you may visit www.arduino.cc.

  The Arduino works by starts executing the code in a function called setup() and then it
  repeatedly keeps calling loop() forever. All the initialization code is kept in setup()
  and code to continuously sense the tuning knob, the function button, transmit/receive,
  etc is all in the loop() function. If you wish to study the code top down, then scroll
  to the bottom of this file and read your way up.

  Below are the libraries to be included for building the Raduino
  The EEPROM library is used to store settings like the frequency memory, caliberation data, etc.

   The main chip which generates upto three oscillators of various frequencies in the
   Raduino is the Si5351a. To learn more about Si5351a you can download the datasheet
   from www.silabs.com although, strictly speaking it is not a requirment to understand this code.
   Instead, you can look up the Si5351 library written by xxx, yyy. You can download and
   install it from www.url.com to complile this file.
   The Wire.h library is used to talk to the Si5351 and we also declare an instance of
   Si5351 object to control the clocks.
*/
/*  N8LOV mods
    20210106 - Remove duplicate #Defines. Add callsign ver. Reduce setFrequency by 80 bytes. Remove sideband default from doTuning.
    20210107 - Inhibit transmit when freq is out of bounds for hardware.
    20210713 - Add oneKhzOn and mod doTuning.   Remove call to saveVFOs to reduce EEPROM writes.
    20210716 - Add code to handle Cw mode for each VFO.
    20220114 - Add bandSelectOn, modified doTuning to handle band selection tuning. CW receive freq offset by +/- sidetone based on isUSB.
    20220115 - Removed the RIT function.  
*/
#include <Wire.h>
#include <EEPROM.h>
#include "ubitx.h"
#include "nano_gui.h"

// N8LOV - define displayed software version here
#define CALLSIGN_VER  "v6.1.N8LOV.1"

/**
    The main chip which generates upto three oscillators of various frequencies in the
    Raduino is the Si5351a. To learn more about Si5351a you can download the datasheet
    from www.silabs.com although, strictly speaking it is not a requirment to understand this code.

    We no longer use the standard SI5351 library because of its huge overhead due to many unused
    features consuming a lot of program space. Instead of depending on an external library we now use
    Jerry Gaffke's, KE7ER, lightweight standalone mimimalist "si5351bx" routines (see further down the
    code). Here are some defines and declarations used by Jerry's routines:
*/


/**
   We need to carefully pick assignment of pin for various purposes.
   There are two sets of completely programmable pins on the Raduino.
   First, on the top of the board, in line with the LCD connector is an 8-pin connector
   that is largely meant for analog inputs and front-panel control. It has a regulated 5v output,
   ground and six pins. Each of these six pins can be individually programmed
   either as an analog input, a digital input or a digital output.
   The pins are assigned as follows (left to right, display facing you):
        Pin 1 (Violet), A7, SPARE
        Pin 2 (Blue),   A6, KEYER (DATA)
        Pin 3 (Green), +5v
        Pin 4 (Yellow), Gnd
        Pin 5 (Orange), A3, PTT
        Pin 6 (Red),    A2, F BUTTON
        Pin 7 (Brown),  A1, ENC B
        Pin 8 (Black),  A0, ENC A
  Note: A5, A4 are wired to the Si5351 as I2C interface
 *       *
   Though, this can be assigned anyway, for this application of the Arduino, we will make the following
   assignment
   A2 will connect to the PTT line, which is the usually a part of the mic connector
   A3 is connected to a push button that can momentarily ground this line. This will be used for RIT/Bandswitching, etc.
   A6 is to implement a keyer, it is reserved and not yet implemented
   A7 is connected to a center pin of good quality 100K or 10K linear potentiometer with the two other ends connected to
   ground and +5v lines available on the connector. This implments the tuning mechanism


  #define ENC_A (A0)
  #define ENC_B (A1)
  #define FBUTTON (A2)
  #define PTT   (A3)
  #define ANALOG_KEYER (A6)
  #define ANALOG_SPARE (A7)
*/

/** pin assignments
  14  T_IRQ           2 std   changed
  13  T_DOUT              (parallel to SOD/MOSI, pin 9 of display)
  12  T_DIN               (parallel to SDI/MISO, pin 6 of display)
  11  T_CS            9   (we need to specify this)
  10  T_CLK               (parallel to SCK, pin 7 of display)
  9   SDO(MSIO) 12    12  (spi)
  8   LED       A0    8   (not needed, permanently on +3.3v) (resistor from 5v,
  7   SCK       13    13  (spi)
  6   SDI       11    11  (spi)
  5   D/C       A3    7   (changable)
  4   RESET     A4    9 (not needed, permanently +5v)
  3   CS        A5    10  (changable)
  2   GND       GND
  1   VCC       VCC

  The model is called tjctm24028-spi
  it uses an ILI9341 display controller and an  XPT2046 touch controller.


  #define TFT_DC  9
  #define TFT_CS 10

  //#define TIRQ_PIN  2
  #define CS_PIN  8
*/

// MOSI=11, MISO=12, SCK=13

//XPT2046_Touchscreen ts(CS_PIN);

//Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

/**
   The Arduino, unlike C/C++ on a regular computer with gigabytes of RAM, has very little memory.
   We have to be very careful with variables that are declared inside the functions as they are
   created in a memory region called the stack. The stack has just a few bytes of space on the Arduino
   if you declare large strings inside functions, they can easily exceed the capacity of the stack
   and mess up your programs.
   We circumvent this by declaring a few global buffers as  kitchen counters where we can
   slice and dice our strings. These strings are mostly used to control the display or handle
   the input and output from the USB port. We must keep a count of the bytes used while reading
   the serial port as we can easily run out of buffer space. This is done in the serial_in_count variable.
*/
char c[30], b[30];
//char printBuff[2][20];  //mirrors what is showing on the two lines of the display
//int count = 0;          //to generally count ticks, loops, etc

/**
    The second set of 16 pins on the Raduino's bottom connector are have the three clock outputs and the digital lines to control the rig.
    This assignment is as follows :
      Pin   1   2    3    4    5    6    7    8    9    10   11   12   13   14   15   16
           GND +5V CLK0  GND  GND  CLK1 GND  GND  CLK2  GND  D2   D3   D4   D5   D6   D7
    These too are flexible with what you may do with them, for the Raduino, we use them to :
    - TX_RX line : Switches between Transmit and Receive after sensing the PTT or the morse keyer
    - CW_KEY line : turns on the carrier for CW


  #define TX_RX (7)
  #define CW_TONE (6)
  #define TX_LPF_A (5)
  #define TX_LPF_B (4)
  #define TX_LPF_C (3)
  #define CW_KEY (2)
*/

/**
   These are the indices where these user changable settinngs are stored  in the EEPROM

  #define MASTER_CAL 0
  #define LSB_CAL 4
  #define USB_CAL 8
  #define SIDE_TONE 12
*/
/*these are ids of the vfos as well as their offset into the eeprom storage, don't change these 'magic' values
  #define VFO_A 16
  #define VFO_B 20
  #define CW_SIDETONE 24
  #define CW_SPEED 28
*/
/* the screen calibration parameters : int slope_x=104, slope_y=137, offset_x=28, offset_y=29;
  #define SLOPE_X 32
  #define SLOPE_Y 36
  #define OFFSET_X 40
  #define OFFSET_Y 44
  #define CW_DELAYTIME 48
*/

//These are defines for the new features back-ported from KD8CEC's software
//these start from beyond 256 as Ian, KD8CEC has kept the first 256 bytes free for the base version
//#define VFO_A_MODE  256 // 2: LSB, 3: USB
//#define VFO_B_MODE  257

//values that are stored for the VFO modes
//#define VFO_MODE_LSB 2
//#define VFO_MODE_USB 3

// handkey, iambic a, iambic b : 0,1,2f
//#define CW_KEY_TYPE 358

/**
   The uBITX is an upconnversion transceiver. The first IF is at 45 MHz.
   The first IF frequency is not exactly at 45 Mhz but about 5 khz lower,
   this shift is due to the loading on the 45 Mhz crystal filter by the matching
   L-network used on it's either sides.
   The first oscillator works between 48 Mhz and 75 MHz. The signal is subtracted
   from the first oscillator to arriive at 45 Mhz IF. Thus, it is inverted : LSB becomes USB
   and USB becomes LSB.
   The second IF of 12 Mhz has a ladder crystal filter. If a second oscillator is used at
   57 Mhz, the signal is subtracted FROM the oscillator, inverting a second time, and arrives
   at the 12 Mhz ladder filter thus doouble inversion, keeps the sidebands as they originally were.
   If the second oscillator is at 33 Mhz, the oscilaltor is subtracated from the signal,
   thus keeping the signal's sidebands inverted. The USB will become LSB.
   We use this technique to switch sidebands. This is to avoid placing the lsbCarrier close to
   12 MHz where its fifth harmonic beats with the arduino's 16 Mhz oscillator's fourth harmonic
*/


//#define INIT_USB_FREQ   (11059200l)
// limits the tuning and working range of the ubitx between 3 MHz and 30 MHz
//#define LOWEST_FREQ   (3000000l)
//#define HIGHEST_FREQ (30000000l)

//we directly generate the CW by programmin the Si5351 to the cw tx frequency, hence, both are different modes
//these are the parameter passed to startTx
//#define TX_SSB 0
//#define TX_CW 1

bool bandSelectOn = 0;  // N8LOV - band selection mode is off
bool inhibitTx = 0; // N8LOV - default to no inhibit
/* -RIT
bool ritOn = 0;
-RIT */
char vfoActive = VFO_A;
//int8_t meter_reading = 0; // a -1 on meter makes it invisible
unsigned long vfoA = 7150000L, vfoB = 14200000L, sideTone = 800, usbCarrier;
bool isUsbVfoA = false, isUsbVfoB = true;
unsigned long frequency  /*-RIT , ritRxFrequency, ritTxFrequency -RIT */;  //frequency is the current frequency on the dial
unsigned long firstIF =   45005000L;

// if cwMode is flipped on, the rx frequency is tuned down by sidetone hz instead of being zerobeat
bool cwMode = false; // the current cw status (of the active VFO)
bool vfoAcwMode = false; // N8LOV - cw mode status for VFO A
bool vfoBcwMode = false; // N8LOV - cw mode status for VFO B

//these are variables that control the keyer behaviour
int cwSpeed = 100; //this is actuall the dot period in milliseconds
extern int32_t calibration;
int cwDelayTime = 60;
bool Iambic_Key = true;
//#define IAMBICB 0x10 // 0 for Iambic A, 1 for Iambic B
unsigned char keyerControl = IAMBICB;
//during CAT commands, we will freeeze the display until CAT is disengaged
bool doingCAT = 0;


/**
   Raduino needs to keep track of current state of the transceiver. These are a few variables that do it
*/
bool txCAT = false;        //turned on if the transmitting due to a CAT command
bool inTx = false;                //it is set to 1 if in transmit mode (whatever the reason : cw, ptt or cat)
bool splitOn = false;             //working split, uses VFO B as the transmit frequency
bool oneKhzOn = false;            //1 Khz freq adjustment by knob
bool keyDown = false;             //in cw mode, denotes the carrier is being transmitted
bool isUSB = false;               //upper sideband was selected, this is reset to the default for the
//frequency when it crosses the frequency border of 10 MHz
byte menuOn = 0;              //set to 1 when the menu is being displayed, if a menu item sets it to zero, the menu is exited
unsigned long cwTimeout = 0;  //milliseconds to go before the cw transmit line is released and the radio goes back to rx mode
unsigned long dbgCount = 0;   //not used now
unsigned char txFilter = 0;   //which of the four transmit filters are in use
boolean modeCalibrate = false;//this mode of menus shows extended menus to calibrate the oscillators and choose the proper
//beat frequency

/**
   Below are the basic functions that control the uBitx. Understanding the functions before
   you start hacking around
*/

/**
   Our own delay. During any delay, the raduino should still be processing a few times.
*/

void active_delay(int delay_by) {
  unsigned long timeStart = millis();
  while (millis() - timeStart <= (unsigned long)delay_by) {
    delay(10);
    //Background Work
    checkCAT();
  }
}

/*
void saveVFOs() {

  if (vfoActive == VFO_A)
    EEPROM.put(VFO_A, frequency);
  else
    EEPROM.put(VFO_A, vfoA);

  if (isUsbVfoA)
    EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);  

  if (vfoActive == VFO_B)
    EEPROM.put(VFO_B, frequency);
  else
    EEPROM.put(VFO_B, vfoB);

  if (isUsbVfoB)
    EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
  else
    EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);

  EEPROM.put(CW_MODE, cwMode); // N8LOV - save Cw Mode
}
*/


// N8LOV - for manual save of active VFO
// Only writes to EEPROM if there is a data change.
void saveVFO() {
  byte x;
  bool b;
  if (vfoActive == VFO_A){
    EEPROM.get(VFO_A, vfoA);
    if (frequency != vfoA) EEPROM.put(VFO_A, frequency);

    EEPROM.get(VFO_A_MODE, x);
    if (isUSB)
      if (x != VFO_MODE_USB) EEPROM.put(VFO_A_MODE, VFO_MODE_USB);
    else
      if (x != VFO_MODE_LSB) EEPROM.put(VFO_A_MODE, VFO_MODE_LSB);  

    EEPROM.get(VFO_A_CW_MODE, b);
    if (b != cwMode) EEPROM.put(VFO_A_CW_MODE, cwMode);  
  }

  if (vfoActive == VFO_B){
    EEPROM.get(VFO_B, vfoB);
    if (frequency != vfoB) EEPROM.put(VFO_B, frequency);

    EEPROM.get(VFO_B_MODE, x);
    if (isUSB)
      if (x != VFO_MODE_USB) EEPROM.put(VFO_B_MODE, VFO_MODE_USB);
    else
      if (x != VFO_MODE_LSB) EEPROM.put(VFO_B_MODE, VFO_MODE_LSB);

    EEPROM.get(VFO_B_CW_MODE, b);
    if (b != cwMode) EEPROM.put(VFO_B_CW_MODE, cwMode);
  }
}


// N8LOV - for manual recall of active VFO settings
void recallVFO() {
  byte x;
  if (vfoActive == VFO_A){
    EEPROM.get(VFO_A, vfoA);
    frequency = vfoA;
    EEPROM.get(VFO_A_MODE, x);
    isUsbVfoA = (x == VFO_MODE_USB);
    isUSB = isUsbVfoA;
    EEPROM.get(VFO_A_CW_MODE, vfoAcwMode);
    cwMode = vfoAcwMode;
  }

  if (vfoActive == VFO_B){
    EEPROM.get(VFO_B, vfoB);
    frequency = vfoB;
    EEPROM.get(VFO_B_MODE, x);
    isUsbVfoB = (x == VFO_MODE_USB); 
    isUSB = isUsbVfoB;
    EEPROM.get(VFO_B_CW_MODE, vfoBcwMode);
    cwMode = vfoBcwMode;
  }
  
  //EEPROM.get(CW_MODE, cwMode);
}

/**
   Select the properly tx harmonic filters
   The four harmonic filters use only three relays
   the four LPFs cover 30-21 Mhz, 18 - 14 Mhz, 7-10 MHz and 3.5 to 5 Mhz
   Briefly, it works like this,
   - When KT1 is OFF, the 'off' position routes the PA output through the 30 MHz LPF
   - When KT1 is ON, it routes the PA output to KT2. Which is why you will see that
     the KT1 is on for the three other cases.
   - When the KT1 is ON and KT2 is off, the off position of KT2 routes the PA output
     to 18 MHz LPF (That also works for 14 Mhz)
   - When KT1 is On, KT2 is On, it routes the PA output to KT3
   - KT3, when switched on selects the 7-10 Mhz filter
   - KT3 when switched off selects the 3.5-5 Mhz filter
   See the circuit to understand this
*/

void setTXFilters(unsigned long freq) {

  if (freq > 21000000L) { // the default filter is with 35 MHz cut-off
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq >= 14000000L) { //thrown the KT1 relay on, the 30 MHz LPF is bypassed and the 14-18 MHz LPF is allowd to go through
    digitalWrite(TX_LPF_A, 1);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq > 7000000L) {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 1);
    digitalWrite(TX_LPF_C, 0);
  }
  else {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 1);
  }
}


void setTXFilters_v5(unsigned long freq) {

  if (freq > 21000000L) { // the default filter is with 35 MHz cut-off
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq >= 14000000L) { //thrown the KT1 relay on, the 30 MHz LPF is bypassed and the 14-18 MHz LPF is allowd to go through
    digitalWrite(TX_LPF_A, 1);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 0);
  }
  else if (freq > 7000000L) {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 1);
    digitalWrite(TX_LPF_C, 0);
  }
  else {
    digitalWrite(TX_LPF_A, 0);
    digitalWrite(TX_LPF_B, 0);
    digitalWrite(TX_LPF_C, 1);
  }
}

// N8LOV
// sets inhibitTx based on transmit frequency compared to transmit frequency bounds
void checkTxFreq(unsigned long f) {
  inhibitTx = ((f > HIGHEST_TX_FREQ || f < LOWEST_TX_FREQ) ? 1 : 0);
}

/**
   This is the most frequently called function that configures the
   radio to a particular frequeny, sideband and sets up the transmit filters

   The transmit filter relays are powered up only during the tx so they dont
   draw any current during rx.

   The carrier oscillator of the detector/modulator is permanently fixed at
   uppper sideband. The sideband selection is done by placing the second oscillator
   either 12 Mhz below or above the 45 Mhz signal thereby inverting the sidebands
   through mixing of the second local oscillator.
*/

void setFrequency(unsigned long f) {
  uint64_t osc_f, firstOscillator, secondOscillator;

  setTXFilters(f);

  /*
    if (isUSB){
      si5351bx_setfreq(2, firstIF  + f);
      si5351bx_setfreq(1, firstIF + usbCarrier);
    }
    else{
      si5351bx_setfreq(2, firstIF + f);
      si5351bx_setfreq(1, firstIF - usbCarrier);
    }
  */
  //alternative to reduce the intermod spur

  /*
    if (isUSB){
    if (cwMode)
      si5351bx_setfreq(2, firstIF  + f + sideTone);
    else
      si5351bx_setfreq(2, firstIF  + f);
    si5351bx_setfreq(1, firstIF + usbCarrier);
    }
    else{
    if (cwMode)
      si5351bx_setfreq(2, firstIF  + f + sideTone);
    else
      si5351bx_setfreq(2, firstIF + f);
    si5351bx_setfreq(1, firstIF - usbCarrier);
    }
  */

  //N8LOV - simplify code, save 80 bytes program storage
  // set 2 to firstIF + f, add/subtract sideTone if cwMode
  si5351bx_setfreq(2, firstIF  + f + (cwMode ? (isUSB ? -sideTone : sideTone) : 0));
  // set 1 to firstIF, subtract usbCarrier if USB, else add usbCarrier
  si5351bx_setfreq(1, firstIF + (isUSB ? -usbCarrier : usbCarrier));

  frequency = f;
}

/**
   startTx is called by the PTT, cw keyer and CAT protocol to
   put the uBitx in tx mode. It takes care of rit settings, sideband settings
   Note: In cw mode, doesnt key the radio, only puts it in tx mode
   CW offest is calculated as lower than the operating frequency when in LSB mode, and vice versa in USB mode
*/

void startTx(byte txMode) {
  //unsigned long tx_freq = 0;

  // N8LOV - check Tx frequency bounds first, return if Tx freq is out of bounds
/* -RIT
  if (ritOn) {
    checkTxFreq(ritTxFrequency);
  } else 
-RIT */
  
  if (splitOn) {
    if (vfoActive == VFO_B) {
      checkTxFreq(vfoA);
    } else if (vfoActive == VFO_A) {
      checkTxFreq(vfoB);
    }
  } else {
    checkTxFreq(frequency);
  }
  if (inhibitTx) return;

  digitalWrite(TX_RX, 1);
  inTx = 1;

/* -RIT
  if (ritOn) {
    //save the current as the rx frequency
    ritRxFrequency = frequency;
    setFrequency(ritTxFrequency);
  }
  else
  {
-RIT */  
    if (splitOn == 1) {
      if (vfoActive == VFO_B) {
        vfoActive = VFO_A;
        isUSB = isUsbVfoA;
        frequency = vfoA;
      }
      else if (vfoActive == VFO_A) {
        vfoActive = VFO_B;
        frequency = vfoB;
        isUSB = isUsbVfoB;
      }
    }
    setFrequency(frequency);
/* -RIT    
  }
-RIT */  

  if (txMode == TX_CW) {
    digitalWrite(TX_RX, 0);

    //turn off the second local oscillator and the bfo
    si5351bx_setfreq(0, 0);
    si5351bx_setfreq(1, 0);

    // N8LOV - set clk2 frequency to the exact carrier frequency for CW. Saved 44 bytes of program storage.
    si5351bx_setfreq(2, frequency);

    delay(20);
    digitalWrite(TX_RX, 1);
  }
  drawTx();

}

void stopTx() {
  inTx = false;

  digitalWrite(TX_RX, 0);           //turn off the tx
  si5351bx_setfreq(0, usbCarrier);  //set back the carrier oscillator anyway, cw tx switches it off

/* -RIT
  if (ritOn)
    setFrequency(ritRxFrequency);
  else {
-RIT  */
    if (splitOn) {
      //vfo Change
      if (vfoActive == VFO_B) {
        vfoActive = VFO_A;
        frequency = vfoA;
        isUSB = isUsbVfoA;
      }
      else if (vfoActive == VFO_A) {
        vfoActive = VFO_B;
        frequency = vfoB;
        isUSB = isUsbVfoB;
      }
    }
    setFrequency(frequency);
/* -RIT    
  }
-RIT  */
  //updateDisplay();
  drawTx();
}

/**
   ritEnable is called with a frequency parameter that determines
   what the tx frequency will be
*/
/* -RIT
void ritEnable(unsigned long f) {
  ritOn = 1;
  //save the non-rit frequency back into the VFO memory
  //as RIT is a temporary shift, this is not saved to EEPROM
  ritTxFrequency = f;
}

// this is called by the RIT menu routine
void ritDisable() {
  if (ritOn) {
    ritOn = 0;
    setFrequency(ritTxFrequency);
    updateDisplay();
  }
}
-RIT */

/**
   Basic User Interface Routines. These check the front panel for any activity
*/

/**
   The PTT is checked only if we are not already in a cw transmit session
   If the PTT is pressed, we shift to the ritbase if the rit was on
   flip the T/R line to T and update the display to denote transmission
*/

void checkPTT() {
  //we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;

  if (digitalRead(PTT) == 0 && !inTx) {
    startTx(TX_SSB);
    active_delay(50); //debounce the PTT
  }

  if (digitalRead(PTT) == 1 && inTx)
    stopTx();
}

//check if the encoder button was pressed
void checkButton() {
  int i, t1, t2, knob, new_knob;

  //only if the button is pressed
  if (!btnDown())
    return;
  active_delay(50);
  if (!btnDown()) //debounce
    return;
  
  //disengage any CAT work
  doingCAT = 0;

  int downTime = 0;
  while (btnDown()) {
    active_delay(10);
    downTime++;
    if (downTime > 300) {
      doSetup2();
      return;
    }
  }
  active_delay(100);


  doCommands();
  //wait for the button to go up again
  while (btnDown())
    active_delay(10);
  active_delay(50);//debounce
}

void switchVFO(int vfoSelect) {
  if (vfoSelect == VFO_A) {
    if (vfoActive == VFO_B) {
      vfoB = frequency;
      isUsbVfoB = isUSB;
      vfoBcwMode = cwMode;

    }
    vfoActive = VFO_A;
    //      printLine2("Selected VFO A  ");
    frequency = vfoA;
    isUSB = isUsbVfoA;
    cwMode = vfoAcwMode;
  }
  else { //(vfoSelect == VFO_B)
    if (vfoActive == VFO_A) {
      vfoA = frequency;
      isUsbVfoA = isUSB;
      vfoAcwMode = cwMode;
    }
    vfoActive = VFO_B;
    //      printLine2("Selected VFO B  ");
    frequency = vfoB;
    isUSB = isUsbVfoB;
    cwMode = vfoBcwMode;
  }

  setFrequency(frequency);
  redrawVFOs();
  //saveVFOs();  // N8LOV - reduce EEPROM writes
}

/**
   The tuning jumps by 50 Hz on each step when you tune slowly
   As you spin the encoder faster, the jump size also increases
   This way, you can quickly move to another band by just spinning the
   tuning knob
*/

void doTuning() {
  int s;
  static unsigned long prev_freq;
  static unsigned long nextFrequencyUpdate = 0;

  unsigned long now = millis();

  if (now >= nextFrequencyUpdate && prev_freq != frequency) {
    updateDisplay();
    nextFrequencyUpdate = now + 500;
    prev_freq = frequency;
  }

  s = enc_read();
  if (!s)
    return;

  doingCAT = 0; // go back to manual mode if you were doing CAT
  prev_freq = frequency;

  // N8LOV - Reduce program memory by 24 bytes
  if (bandSelectOn) 
    setBandFreq(frequency, s);
  else if (oneKhzOn) // N8LOV - add 1Khz adjustment mode
    frequency += 1000l * (s<0 ? -1: (s>0 ? 1: 0));
  else if (s > 10 || s < -10)
    frequency += 200l * s;
  else if (s > 5 || s < -5)
    frequency += 100l * s;
  else if (s > 0 || s < 0)
    frequency += 50l * s;
  //else if (s < -10)
  //  frequency += 200l * s;
  //else if (s < -5)
  //  frequency += 100l * s;
  //else if (s  < 0)
  //  frequency += 50l * s;

  // N8LOV - observe frequency bounds
  if (frequency > HIGHEST_FREQ) frequency = HIGHEST_FREQ;
  if (frequency < LOWEST_FREQ) frequency = LOWEST_FREQ;

  setFrequency(frequency);
}


/**
   RIT only steps back and forth by 100 hz at a time
*/
/* -RIT
void doRIT() {
  unsigned long newFreq;

  int knob = enc_read();
  unsigned long old_freq = frequency;

  if (knob < 0)
    frequency -= 100l;
  else if (knob > 0)
    frequency += 100l;

  if (old_freq != frequency) {
    setFrequency(frequency);
    updateDisplay();
  }
}
-RIT */

/**
   The settings are read from EEPROM. The first time around, the values may not be
   present or out of range, in this case, some intelligent defaults are copied into the
   variables.
*/
void initSettings() {
  byte x;
  //read the settings from the eeprom and restore them
  //if the readings are off, then set defaults
  EEPROM.get(MASTER_CAL, calibration);
  EEPROM.get(USB_CAL, usbCarrier);
  EEPROM.get(VFO_A, vfoA);
  EEPROM.get(VFO_B, vfoB);
  EEPROM.get(CW_SIDETONE, sideTone);
  EEPROM.get(CW_SPEED, cwSpeed);
  EEPROM.get(CW_DELAYTIME, cwDelayTime);

  // the screen calibration parameters : int slope_x=104, slope_y=137, offset_x=28, offset_y=29;

  if (usbCarrier > 11060000l || usbCarrier < 11048000l)
    usbCarrier = 11052000l;
  if (vfoA > HIGHEST_FREQ || LOWEST_FREQ > vfoA) // N8LOV - keep lower frequencies, use defines for range
    vfoA = 7285000l; // N8LOV - SSB calling
  if (vfoB > HIGHEST_FREQ || LOWEST_FREQ > vfoB)// N8LOV - keep lower frequencies, use defines for range
    vfoB = 14285000l; // N8LOV - SSB calling
  if (sideTone < 100 || 2000 < sideTone)
    sideTone = 800;
  if (cwSpeed < 10 || 1000 < cwSpeed)
    cwSpeed = 100;
  if (cwDelayTime < 10 || cwDelayTime > 100)
    cwDelayTime = 50;

  /*
     The VFO modes are read in as either 2 (USB) or 3(LSB), 0, the default
     is taken as 'uninitialized
  */

  EEPROM.get(VFO_A_MODE, x);
  isUsbVfoA = (x==VFO_MODE_USB? true: (x==VFO_MODE_LSB? false: (vfoA>10000000l? true: false))); // N8LOV - save memory


  EEPROM.get(VFO_B_MODE, x);
  isUsbVfoB = (x==VFO_MODE_USB? true: (x==VFO_MODE_LSB? false: (vfoB>10000000l? true: false))); // N8LOV - save memory


  //set the current mode
  isUSB = isUsbVfoA;

  EEPROM.get(VFO_A_CW_MODE, vfoAcwMode); // N8LOV - restore Cw Mode
  cwMode = vfoAcwMode;
  EEPROM.get(VFO_B_CW_MODE, vfoBcwMode);
  

  /*
     The keyer type splits into two variables
  */
  EEPROM.get(CW_KEY_TYPE, x);

  if (x == 0)
    Iambic_Key = false;
  else if (x == 1) {
    Iambic_Key = true;
    keyerControl &= ~IAMBICB;
  }
  else if (x == 2) {
    Iambic_Key = true;
    keyerControl |= IAMBICB;
  }

}

void initPorts() {

  analogReference(DEFAULT);

  //??
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(FBUTTON, INPUT_PULLUP);

  //configure the function button to use the external pull-up
  //  pinMode(FBUTTON, INPUT);
  //  digitalWrite(FBUTTON, HIGH);

  pinMode(PTT, INPUT_PULLUP);
  //  pinMode(ANALOG_KEYER, INPUT_PULLUP);

  pinMode(CW_TONE, OUTPUT);
  digitalWrite(CW_TONE, 0);

  pinMode(TX_RX, OUTPUT);
  digitalWrite(TX_RX, 0);

  pinMode(TX_LPF_A, OUTPUT);
  pinMode(TX_LPF_B, OUTPUT);
  pinMode(TX_LPF_C, OUTPUT);
  digitalWrite(TX_LPF_A, 0);
  digitalWrite(TX_LPF_B, 0);
  digitalWrite(TX_LPF_C, 0);

  pinMode(CW_KEY, OUTPUT);
  digitalWrite(CW_KEY, 0);
}

void setup()
{
  Serial.begin(38400);
  Serial.flush();

  displayInit();
  initSettings();
  initPorts();
  initOscillators();
  frequency = vfoA;
  setFrequency(vfoA);
  enc_setup();

  if (btnDown()) {
    setupTouch();
    isUSB = true;
    setFrequency(10000000l);
    setupFreq();
    isUSB = false;
    setFrequency(7100000l);
    setupBFO();
  }
  guiUpdate();
  //displayRawText("v6.1", 160, 210, DISPLAY_LIGHTGREY, DISPLAY_NAVY);
  // N8LOV - use #define for software version. display in commandbar area at startup
  displayText(CALLSIGN_VER, CMDBAR_X, ROW2_Y, FULL_W, BTN_H, DISPLAY_LIGHTGREY, DISPLAY_NAVY, DISPLAY_NAVY);
}



/**
   The loop checks for keydown, ptt, function button and tuning.
*/

void loop() {

  if (cwMode)
    cwKeyer();
  else if (!txCAT)
    checkPTT();

  checkButton();
  //tune only when not tranmsitting
  if (!inTx) {
/* -RIT    
    if (ritOn)
      doRIT();
    else
-RIT */
      doTuning();
    checkTouch();
  } else if (bandSelectOn) toggleBandSelect(); // N8LOV - cancel band select in transmit

  checkCAT();
}
