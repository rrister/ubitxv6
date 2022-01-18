#include <Arduino.h>
#include <EEPROM.h>
#include "morse.h"
#include "ubitx.h"
#include "nano_gui.h"
/* N8LOV Mods
   20210106 - Mod band selection.  Fix formatFreq to handle frequencies below 1000Khz.  Use defines for freq limits in enterFreq.  Make RIT not selectable during SPL.
   20210107 - Implemented tuning bounds in fastTune.
   20210108 - Use defines in UI.  Added clearCommandbar(). Updates to drawCommandbar, displayRIT, fastTune, setCwTone.
   20210109 - Added band/freq list and new band selection capability (selectBand).
   20210711 - Modified band/freq list.
   20210713 - Added 1Kz button and code. Added A>I button and code (copy active VFO freq to Inactive VFO & set split mode). Remove calls to saveVFOs to reduce EEPROM writes.
   20220114 - Modify selectBand and associated functions.
   20220116 - Fixed doCommands.
*/

/**
   The user interface of the ubitx consists of the encoder, the push-button on top of it
   and the 16x2 LCD display.
   The upper line of the display is constantly used to display frequency and status
   of the radio. Occasionally, it is used to provide a two-line information that is
   quickly cleared up.
*/

#define BUTTON_SELECTED 1

struct Button {
  int x, y, w, h;
  char *text;
  //char *morse;
};

#define MAX_BUTTONS 14
const struct Button btn_set[MAX_BUTTONS] PROGMEM = {
  //const struct Button  btn_set [] = {
  {VFOA_X, ROW1_Y, VFO_W, VFO_H, "VFOA"},
  {VFOB_X, ROW1_Y, VFO_W, VFO_H, "VFOB"},

  {COL1_X, ROW3_Y, BTN_W, BTN_H, "USB"},
  {COL2_X, ROW3_Y, BTN_W, BTN_H, "LSB"},
  {COL3_X, ROW3_Y, BTN_W, BTN_H, "CW"},
  {COL4_X, ROW3_Y, BTN_W, BTN_H, "A>I"},// 'A>I' - copy active VFO freq to inactive VFO & set split mode for handling pileup/ if in RIT, RX freq to active, TX Freq to Inactive then RIT off
  {COL5_X, ROW3_Y, BTN_W, BTN_H, "SPL"},

/* -RIT  {COL1_X, ROW4_Y, BTN_W, BTN_H, "RIT"},
-RIT */
  //{COL1_X, ROW4_Y, BTN_W, BTN_H, ""},
  //{COL2_X, ROW4_Y, BTN_W, BTN_H, ""},
  {COL3_X, ROW4_Y, BTN_W, BTN_H, "FRQ"},
  {COL4_X, ROW4_Y, BTN_W, BTN_H, "BND"},
  {COL5_X, ROW4_Y, BTN_W, BTN_H, "1Kz"}, // '1Kz' - toggle to tune freq 1khz only in active VFO - to support split mode pileup 

  {COL1_X, ROW5_Y, BTN_W, BTN_H, "SAV"}, // save the active vfo to EEPROM
  {COL2_X, ROW5_Y, BTN_W, BTN_H, "RCL"}, // recall the active vfo from EEPROM
  //{COL3_X, ROW5_Y, BTN_W, BTN_H, ""},
  {COL4_X, ROW5_Y, BTN_W, BTN_H, "WPM"},
  {COL5_X, ROW5_Y, BTN_W, BTN_H, "TON"},
};

#define MAX_KEYS 15
const struct Button keypad[MAX_KEYS] PROGMEM = {
  {COL1_X, ROW3_Y, BTN_W, BTN_H,  "1"}, //, "1"},
  {COL2_X, ROW3_Y, BTN_W, BTN_H, "2"}, //, "2"},
  {COL3_X, ROW3_Y, BTN_W, BTN_H, "3"}, //, "3"},
  {COL4_X, ROW3_Y, BTN_W, BTN_H,  ""}, //"000"}, // N8LOV
  {COL5_X, ROW3_Y, BTN_W, BTN_H,  "OK"}, //, "K"},

  {COL1_X, ROW4_Y, BTN_W, BTN_H,  "4"}, //, "4"},
  {COL2_X, ROW4_Y, BTN_W, BTN_H,  "5"}, //, "5"},
  {COL3_X, ROW4_Y, BTN_W, BTN_H,  "6"}, //, "6"},
  {COL4_X, ROW4_Y, BTN_W, BTN_H,  "0"}, //, "0"},
  {COL5_X, ROW4_Y, BTN_W, BTN_H,  "<-"}, //, "B"},

  {COL1_X, ROW5_Y, BTN_W, BTN_H,  "7"}, //, "7"},
  {COL2_X, ROW5_Y, BTN_W, BTN_H, "8"}, //, "8"},
  {COL3_X, ROW5_Y, BTN_W, BTN_H, "9"}, //, "9"},
  {COL4_X, ROW5_Y, BTN_W, BTN_H,  ""}, //, ""},
  {COL5_X, ROW5_Y, BTN_W, BTN_H,  "Can"}, //, "C"},
};


// N8LOV - Radio Band Frequency Data

// bit value definitions for bitValue in struct
#define bLSB (0x01) // set USB for frequency
#define bUSB (0x02) // set LSB for frequency
#define bCW (0x04)  // set CW on for frequency


struct Freq {
  char *text; // frequency display value, ?? chars max
  unsigned long Hz; // Frequency in hz
  unsigned char bitValues; // bit oriented selections using defines above
};

// The list of bands/frequencies to scroll through during band selection.
// The list should be kept concise and useful to minimize memory use.
// The initial list contains mainly calling frequencies.
// Must include at least one frequency in each band that is desired to be selectable.
// Order of the frequencies in the list dictates the order in which they are traversed.
// Must be in order of lowest freq to highest.  Failure to order them this way could lead
// to some misbehavior during selection.
// Desired settings for USB/LSB and CW are bit or'ed together.
#define MAX_FREQS 22
const struct Freq freq_set[MAX_FREQS] PROGMEM = {
  // 2200M
  //{"2200 Min", 135700, bLSB},
  //{"Max", 137800, bLSB},

  // 630M
  //{"630 Min", 472000, bLSB},
  //{"630 CW", 474200, bLSB | bCW},
  //{"Max", 479000, bLSB},

  //160M
  //{"Bottom", 1800000, bLSB},
  //{"160 CW1", 1810000, bLSB | bCW},
  //{"160 SSB1", 1825000, bLSB},
  //{"160 CW2", 1843000, bLSB | bCW},
  //{"160 SSB2", 1910000, bLSB},
  //{"Max", 2000000, bLSB},

  // 80M
  //{"Min", 3500000, bLSB},
  {"80 CW", 3560000, bLSB | bCW},
  {"80 SSB1", 3690000, bLSB},
  {"80 SSB2", 3985000, bLSB},
  //{"Max", 4000000, bLSB},

  // 60M
  //{"60 1U", 5330500, bUSB},
  //{"60 1C", 5332000, bUSB|bCW},
  //{"60 2U", 5346500, bUSB}, // calling
  //{"60 2C", 5348000, bUSB|bCW},
  //{"60 3U", 5357000, bUSB},
  //{"60 3C", 5358500, bUSB|bCW},
  //{"60 4U", 5371500, bUSB},
  //{"60 4C", 5373000, bUSB|bCW},
  {"60 5U", 5403500, bUSB}, // International calling
  //{"60 5C", 5405000, bUSB|bCW},

  // 40M
  //{"Min", 7000000, bLSB},
  {"40 CW", 7040000, bLSB | bCW},
  {"40 SSB", 7285000, bLSB},
  //{"Max", 7300000, bLSB},

  // 30M
  //{"Min", 10100000, bUSB|bCW},
  {"30 CW1", 10106000, bUSB | bCW},
  {"30 CW2", 10116000, bUSB | bCW},
  //{"Max", 10150000, bUSB|bCW},

  // 20M
  //{"Min", 14000000, bUSB},
  {"20 CW", 14060000, bUSB | bCW},
  {"20 SSB", 14285000, bUSB},
  //{"Max", 14350000, bUSB},

  // 17M
  //{"Min", 18068000, bUSB},
  {"17 CW1", 18080000, bUSB | bCW},
  {"17 CW2", 18096000, bUSB | bCW},
  {"17 SSB", 18130000, bUSB},
  //{"Max", 18168000, bUSB},

  // 15M
  //{"Min", 21000000, bUSB},
  {"15 CW", 21060000, bUSB | bCW},
  {"15 SSB1", 21285000, bUSB},
  {"15 SSB2", 21385000, bUSB},
  //{"Max", 21450000, bUSB},

  // 12M
  //{"Min", 24890000, bUSB},
  {"12 CW1", 24906000, bUSB | bCW},
  {"12 CW2", 24910000, bUSB | bCW},
  {"12 SSB", 24950000, bUSB},
  //{"Min", 24990000, bUSB},

  // CB 11M
  //{"Ch 1", 26965000, bUSB},
  //{"Ch 40", 27405000, bUSB},

  // 10M
  //{"Min", 28000000, bUSB},
  {"10 CW", 28060000, bUSB | bCW},
  {"10 SSB1", 28365000, bUSB},
  {"10 SSB2", 28385000, bUSB},
  //{"Max", 29700000, bUSB},
};

boolean getButton(char *text, struct Button *b) {
  for (int i = 0; i < MAX_BUTTONS; i++) {
    memcpy_P(b, btn_set + i, sizeof(struct Button));
    if (!strcmp(text, b->text)) {
      return true;
    }
  }
  return false;
}

/*
   This formats the frequency given in f
*/
void formatFreq(long f, char *buff) {
  // tks Jack Purdum W8TEE
  // replaced fsprint commmands by str commands for code size reduction

  memset(buff, 0, 10);
  memset(b, 0, sizeof(b));

  ultoa(f, b, DEC);

  // N8LOV - Handle frequencies <1000Khz.  Saves 24 bytes of program storage.
  size_t n = strlen(b);
  memset(buff, ' ', 8 - n);   // Start with one space for every digit fewer than 8 digits
  strncat(buff, b, n - 3);    // Add the Khz digits
  strcat(buff, ".");          // Add the decimal
  strncat(buff, &b[n - 3], 2); // Add the hundreds and tens digits
}

// N8LOV - add command bar clear function
void clearCommandbar() {
  displayFillrect(CMDBAR_X, ROW2_Y, FULL_W, BTN_H, DISPLAY_NAVY);
}

void drawCommandbar(char *text) {
  clearCommandbar(); // N8LOV
  //displayFillrect(CMDBAR_X,ROW2_Y,FULL_W, BTN_H, DISPLAY_NAVY);
  displayText(text, CMDBAR_X, ROW2_Y, FULL_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY); // N8LOV
}

/** A generic control to read variable values
*/
int getValueByKnob(int minimum, int maximum, int step_size,  int initial, char* prefix, char *postfix)
{
  int knob = 0;
  int knob_value;

  while (btnDown())
    active_delay(100);

  active_delay(200);
  knob_value = initial;

  strcpy(b, prefix);
  itoa(knob_value, c, 10);
  strcat(b, c);
  strcat(b, postfix);
  drawCommandbar(b);

  while ((!btnDown() && !readTouch()) && digitalRead(PTT) == HIGH) { // N8LOV

    knob = enc_read();
    if (knob != 0) {
      if (knob_value > minimum && knob < 0)
        knob_value -= step_size;
      if (knob_value < maximum && knob > 0)
        knob_value += step_size;

      strcpy(b, prefix);
      itoa(knob_value, c, 10);
      strcat(b, c);
      strcat(b, postfix);
      drawCommandbar(b);
    }
    checkCAT();
  }
  clearCommandbar(); // N8LOV
  //displayFillrect(30,41,280, 32, DISPLAY_NAVY);
  return knob_value;
}

void printCarrierFreq(unsigned long freq) {

  memset(c, 0, sizeof(c));
  memset(b, 0, sizeof(b));

  ultoa(freq, b, DEC);

  strncat(c, b, 2);
  strcat(c, ".");
  strncat(c, &b[2], 3);
  strcat(c, ".");
  strncat(c, &b[5], 1);
  displayText(c, 110, 100, 100, 30, DISPLAY_CYAN, DISPLAY_NAVY, DISPLAY_NAVY);
}

void displayDialog(char *title, char *instructions) {
  displayClear(DISPLAY_BLACK);
  displayRect(10, 10, 300, 220, DISPLAY_WHITE);
  displayHline(20, 45, 280, DISPLAY_WHITE);
  displayRect(12, 12, 296, 216, DISPLAY_WHITE);
  displayRawText(title, 20, 20, DISPLAY_CYAN, DISPLAY_NAVY);
  displayRawText(instructions, 20, 200, DISPLAY_CYAN, DISPLAY_NAVY);
}




char vfoDisplay[12];
void displayVFO(int vfo) {
  int x, y;
  int displayColor, displayBorder;
  Button b;

  if (vfo == VFO_A) {
    getButton("VFOA", &b);
    if (splitOn) {
      if (vfoActive == VFO_A)
        strcpy(c, "R:");
      else
        strcpy(c, "T:");
    }
    else
      strcpy(c, "A:");
    if (vfoActive == VFO_A) {
      formatFreq(frequency, c + 2);
      displayColor = DISPLAY_WHITE;
      displayBorder = DISPLAY_BLACK;
    } else {
      formatFreq(vfoA, c + 2);
      displayColor = DISPLAY_GREEN;
      displayBorder = DISPLAY_BLACK;
    }
  }

  if (vfo == VFO_B) {
    getButton("VFOB", &b);

    if (splitOn) {
      if (vfoActive == VFO_B)
        strcpy(c, "R:");
      else
        strcpy(c, "T:");
    }
    else
      strcpy(c, "B:");
    if (vfoActive == VFO_B) {
      formatFreq(frequency, c + 2);
      displayColor = DISPLAY_WHITE;
      displayBorder = DISPLAY_WHITE;
    } else {
      displayColor = DISPLAY_GREEN;
      displayBorder = DISPLAY_BLACK;
      formatFreq(vfoB, c + 2);
    }
  }

  if (vfoDisplay[0] == 0) {
    displayFillrect(b.x, b.y, b.w, b.h, DISPLAY_BLACK);
    if (vfoActive == vfo)
      displayRect(b.x, b.y, b.w , b.h, DISPLAY_WHITE);
    else
      displayRect(b.x, b.y, b.w , b.h, DISPLAY_NAVY);
  }
  x = b.x + 6;
  y = b.y + 3;

  char *text = c;

  for (int i = 0; i <= strlen(c); i++) {
    char digit = c[i];
    if (digit != vfoDisplay[i]) {

      displayFillrect(x, y, 15, b.h - 6, DISPLAY_BLACK);
      //checkCAT();

      displayChar(x, y + TEXT_LINE_HEIGHT + 3, digit, displayColor, DISPLAY_BLACK);
      checkCAT();
    }
    if (digit == ':' || digit == '.')
      x += 7;
    else
      x += 16;
    text++;
  }//end of the while loop of the characters to be printed

  strcpy(vfoDisplay, c);
}


void btnDraw(struct Button *b) {
  if (!strcmp(b->text, "VFOA")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_A);
  }
  else if (!strcmp(b->text, "VFOB")) {
    memset(vfoDisplay, 0, sizeof(vfoDisplay));
    displayVFO(VFO_B);
  }
  else if (
/* -RIT    (!strcmp(b->text, "RIT") && ritOn) ||
-RIT */
          
           (!strcmp(b->text, "USB") && isUSB) ||
           (!strcmp(b->text, "LSB") && !isUSB) ||
           (!strcmp(b->text, "1Kz") && oneKhzOn) ||
           (!strcmp(b->text, "BND") && bandSelectOn) ||
           (!strcmp(b->text, "SPL") && splitOn))
    displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_DARKGREY);
  else if (!strcmp(b->text, "CW") && cwMode)
    displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_DARKGREY);
  else
    displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_GREEN, DISPLAY_BLACK, DISPLAY_DARKGREY);
}

/* -RIT
void displayRIT() {
  clearCommandbar(); // N8LOV
  //displayFillrect(0,41,320,30, DISPLAY_NAVY);
  if (ritOn) {
    strcpy(c, "TX:");
    formatFreq(ritTxFrequency, c + 3);
    if (vfoActive == VFO_A)
      displayText(c, VFOA_X, ROW2_Y, VFO_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY);
    else
      displayText(c, VFOB_X, ROW2_Y, VFO_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY);
  }
  else {
    if (vfoActive == VFO_A)
      displayText("", VFOA_X, ROW2_Y, VFO_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY);
    else
      displayText("", VFOB_X, ROW2_Y, VFO_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY);
  }
}
-RIT */

void fastTune() {
  int encoder;

  //if the btn is down, wait until it is up
  while (btnDown())
    active_delay(50);
  active_delay(300);
  
  if (bandSelectOn) toggleBandSelect(); // N8LOV - turn off band select in fasttune mode
  clearCommandbar();
  
  displayText("Fast tune", 145, ROW2_Y, 30, BTN_H, DISPLAY_CYAN, DISPLAY_NAVY, DISPLAY_NAVY); // N8LOV
  while (1) {
    checkCAT();

    //exit after debouncing the btnDown
    if (btnDown()) {
      clearCommandbar(); // N8LOV
      //displayFillrect(100, 55, 120, 30, DISPLAY_NAVY);

      //wait until the button is realsed and then return
      while (btnDown())
        active_delay(50);
      active_delay(300);
      return;
    }

    encoder = enc_read();
    if (encoder != 0) {

      frequency += (encoder > 0 ? 50000l : -50000l);

      // N8LOV - observe defined frequency bounds
      if (frequency > HIGHEST_FREQ) frequency = HIGHEST_FREQ;
      if (frequency < LOWEST_FREQ) frequency = LOWEST_FREQ;

      setFrequency(frequency);
      displayVFO(vfoActive);
    }
  }// end of the event loop
}

void enterFreq() {
  //force the display to refresh everything
  //display all the buttons
  int f;

  for (int i = 0; i < MAX_KEYS; i++) {
    struct Button b;
    memcpy_P(&b, keypad + i, sizeof(struct Button));
    btnDraw(&b);
  }

  int cursor_pos = 0;
  memset(c, 0, sizeof(c));
  f = frequency / 1000l;

  while (1) {

    checkCAT();
    if (!readTouch())
      continue;

    scaleTouch(&ts_point);

    int total = sizeof(btn_set) / sizeof(struct Button);
    for (int i = 0; i < MAX_KEYS; i++) {
      struct Button b;
      memcpy_P(&b, keypad + i, sizeof(struct Button));

      int x2 = b.x + b.w;
      int y2 = b.y + b.h;

      if (b.x < ts_point.x && ts_point.x < x2 &&
          b.y < ts_point.y && ts_point.y < y2) {
        if (!strcmp(b.text, "OK")) {
          long f = atol(c);
          // N8LOV - use defines for limits
          if (HIGHEST_FREQ / 1000l >= f && f > LOWEST_FREQ / 1000l) {
            frequency = f * 1000l;
            setFrequency(frequency);
            if (vfoActive == VFO_A)
              vfoA = frequency;
            else
              vfoB = frequency;
            //saveVFOs();  // N8LOV - reduce EEPROM writes
          }
          guiUpdate();
          return;
        }
        else if (!strcmp(b.text, "<-")) {
          c[cursor_pos] = 0;
          if (cursor_pos > 0)
            cursor_pos--;
          c[cursor_pos] = 0;
        }
        else if (!strcmp(b.text, "Can")) {
          guiUpdate();
          ts_point.x=-1; // N8LOV - untouch button location
          return;
        }
        else if ('0' <= b.text[0] && b.text[0] <= '9') {
          c[cursor_pos++] = b.text[0];
          c[cursor_pos] = 0;
        }
      }
    } // end of the button scanning loop
    strcpy(b, c);
    strcat(b, " KHz");
    displayText(b, COL1_X, ROW2_Y, FULL_W, BTN_H, DISPLAY_WHITE, DISPLAY_NAVY, DISPLAY_NAVY);
    delay(300);
    while (readTouch())
      checkCAT();
  } // end of event loop : while(1)

}

void drawCWStatus() {
  displayFillrect(COL1_X, ROW6_Y, FULL_W, BTN_H, DISPLAY_NAVY);
  strcpy(b, " cw:");
  int wpm = 1200 / cwSpeed;
  itoa(wpm, c, 10);
  strcat(b, c);
  strcat(b, "wpm, ");
  itoa(sideTone, c, 10);
  strcat(b, c);
  strcat(b, "hz");
  displayText(b, COL1_X, ROW6_Y, 210, BTN_H, DISPLAY_CYAN, DISPLAY_NAVY, DISPLAY_NAVY);
}


void drawTx() {
  // N8LOV - Change Tx display to change border color around Tx VFO

  // Note: When in transmit, vfoActive is already set to the correct vfo even in split mode.
  //       When not in transmit and split is on, vfo's need to be swapped to get the transmit vfo.
  char TxVfo = (!inTx && splitOn ? ((vfoActive == VFO_A) ? VFO_B : VFO_A) : vfoActive);

  Button b;
  unsigned int c = (inTx ? DISPLAY_RED : ((TxVfo == vfoActive) ? DISPLAY_WHITE : DISPLAY_NAVY));

  getButton( ((TxVfo == VFO_A) ? "VFOA" : "VFOB"), &b);

  displayRect(b.x, b.y, b.w , b.h, c);

}


void drawStatusbar() {
  drawCWStatus();
}


void guiUpdate() {

  // use the current frequency as the VFO frequency for the active VFO
  displayClear(DISPLAY_NAVY);

  memset(vfoDisplay, 0, 12);
  displayVFO(VFO_A);
  checkCAT();
  memset(vfoDisplay, 0, 12);
  displayVFO(VFO_B);

  checkCAT();
/* -RIT
  displayRIT();
  checkCAT();
-RIT */

  //force the display to refresh everything
  //display all the buttons
  for (int i = 0; i < MAX_BUTTONS; i++) {
    struct Button b;
    memcpy_P(&b, btn_set + i, sizeof(struct Button));
    btnDraw(&b);
    checkCAT();
  }
  drawStatusbar();
  checkCAT();
}



// this builds up the top line of the display with frequency and mode
void updateDisplay() {
  displayVFO(vfoActive);
}



/* -RIT
void ritToggle(struct Button *b) {
  // N8LOV - make RIT not selectable during SPL, since SPL selection disables RIT
  if (!ritOn && !splitOn)
    ritEnable(frequency);
  else
    ritDisable();
  btnDraw(b);
  displayRIT();
}
-RIT */

// N8LOV 
void oneKhzToggle(struct Button *b) {
  oneKhzOn = !oneKhzOn;
  btnDraw(b);
}


void splitToggle(struct Button *b) {
  if (bandSelectOn) toggleBandSelect();
  splitOn = !splitOn; // N8LOV

  btnDraw(b);

/* -RIT
  //disable rit as well
  ritDisable();

  struct Button b2;
  getButton("RIT", &b2);
  btnDraw(&b2);


  displayRIT();
-RIT */
  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_A);
  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_B);
}

// N8LOV - copy active VFO freq/modes to inactive VFO and set split mode operation
void Act2Inact(struct Button *b) {
      displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_DARKGREY);
      if (bandSelectOn) toggleBandSelect();
      struct Button b2;
      if (!splitOn) {
         getButton("SPL", &b2);
         splitToggle(&b2);
/* -RIT
      } else {
         //disable rit as well
         ritDisable();
         getButton("RIT", &b2);
         btnDraw(&b2);
         displayRIT();
-RIT */
      }
 
      if (vfoActive == VFO_A) {
        vfoA = frequency;
        vfoB = vfoA;
        isUsbVfoA = isUSB;
        isUsbVfoB = isUsbVfoA;
        vfoAcwMode = cwMode;
        vfoBcwMode = vfoAcwMode;
      } else if (vfoActive == VFO_B) {
        vfoB = frequency;
        vfoA = vfoB;
        isUsbVfoB = isUSB;
        isUsbVfoA = isUsbVfoB;
        vfoBcwMode = cwMode;
        vfoAcwMode = vfoBcwMode;
      }
      memset(vfoDisplay, 0, sizeof(vfoDisplay));
      displayVFO(VFO_A);
      memset(vfoDisplay, 0, sizeof(vfoDisplay));
      displayVFO(VFO_B);
      displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_GREEN, DISPLAY_BLACK, DISPLAY_DARKGREY);
}   

// N8LOV - save the active vfo data to EEPROM
void  saveActiveVFO(struct Button *b){
  displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_DARKGREY);
  saveVFO();
  displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_GREEN, DISPLAY_BLACK, DISPLAY_DARKGREY);
}

// N8LOV - recall the active vfo data from EEPROM
void  recallActiveVFO(struct Button *b){
  displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_BLACK, DISPLAY_ORANGE, DISPLAY_DARKGREY);
  recallVFO();
  if (vfoActive == VFO_A) 
    displayVFO(VFO_A);
  else 
    displayVFO(VFO_B);
  displayText(b->text, b->x, b->y, b->w, b->h, DISPLAY_GREEN, DISPLAY_BLACK, DISPLAY_DARKGREY);

  struct Button b2;
  getButton("USB", &b2);
  btnDraw(&b2);
  getButton("LSB", &b2);
  btnDraw(&b2);
  getButton("CW", &b2);
  btnDraw(&b2);

  setFrequency(frequency);
}


void cwToggle(struct Button *b) {
  cwMode = !cwMode; // N8LOV
  setFrequency(frequency);
  btnDraw(b);
}

void sidebandToggle(struct Button *b) {
  if (!strcmp(b->text, "LSB"))
    isUSB = false;
  else
    isUSB = true;

  struct Button e;
  getButton("USB", &e);
  btnDraw(&e);
  getButton("LSB", &e);
  btnDraw(&e);

    // N8LOV 
  if (vfoActive == VFO_A) 
     isUsbVfoA = isUSB;
  else 
     isUsbVfoB = isUSB;
  setFrequency(frequency);
  
}


void redrawVFOs() {

  struct Button b;
/* -RIT
  ritDisable();
  getButton("RIT", &b);
  btnDraw(&b);
  displayRIT();
-RIT */
  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_A);
  memset(vfoDisplay, 0, sizeof(vfoDisplay));
  displayVFO(VFO_B);

  //draw the lsb/usb buttons, the sidebands might have changed
  getButton("LSB", &b);
  btnDraw(&b);
  getButton("USB", &b);
  btnDraw(&b);
  getButton("CW", &b); // N8LOV - Supports CW setting for each VFO
  btnDraw(&b);
}

static int enccnt = 0;

// N8LOV - Sets the frequency/USB/LSB/CW info into the active vfo when selecting band
// dir is the encoder value.
// dir < 0 (counter clock-wise), go lower in freq, else go higher in freq.
void setBandFreq(unsigned long f, int dir) {
  // Make encoder knob less sensitive so greater travel is needed to switch bands
  enccnt += dir;
  if (abs(enccnt) < 4) return; else enccnt = 0;

  // Find nearest band frequency in direction
  struct Freq fr;
  char i = 0;
  char li = -1;

  //Find the next freq in the band data in the proper direction
  for (i=0; i<MAX_FREQS; i++) {
    // get the next band data
    memcpy_P(&fr, freq_set + i, sizeof(struct Freq));

    // if going lower in frequency
    if (dir < 0 && (fr.Hz < f)) li = i;
    
    if (fr.Hz > f) {
      // if going higher in frequency
      if (dir > 0) li = i;

      // once the band frequency is higher that the given frequency the loop is done
      break;
    }  
  }
  if (li < 0) {
    li = (dir<0 ? 0 : MAX_FREQS -1);
  }
  // get the band data to apply
  memcpy_P(&fr, freq_set + li, sizeof(struct Freq));
  
  struct Button e;
  // get the cwMode to set
  if (fr.bitValues & bCW)
    cwMode = true;
  else
    cwMode = false;

  // get the frequency to set
  frequency = fr.Hz;

  // get the sideband data to set
  if (fr.bitValues & bUSB || (!fr.bitValues & bUSB && !fr.bitValues & bLSB))
    isUSB = true;
  else
    isUSB = false;

  // update the buttons on the screen
  getButton("CW", &e);
  btnDraw(&e);
  getButton("USB", &e);
  btnDraw(&e);
  getButton("LSB", &e);
  btnDraw(&e);

  // display the band text on screen
  drawCommandbar(fr.text);

  // set and display the frequency
  setFrequency(frequency);
  displayVFO(vfoActive);
}

void toggleBandSelect(){
  enccnt = 50; // force band select after toggle

  // Disable Split
  if (!bandSelectOn && splitOn) {
    struct Button b2;
    getButton("SPL", &b2);
    splitToggle(&b2);
  }

  bandSelectOn = !bandSelectOn;

  struct Freq fr;
  struct Button bttn;
  getButton("BND", &bttn);

  btnDraw(&bttn);
  
  if (bandSelectOn) {

    setBandFreq(frequency, 1); // set radio freq/SB/CW and update vfo
    
  } else {
    clearCommandbar();

  }

  return;
}


// N8LOV - Selects a band/frequency from the list
void selectBand(struct Button *bttn) {
  /* -RIT
  // Don't overwrite RIT display, just inhibit this function
  if (!bandSelectOn && ritOn) return;
  -RIT */
  while (btnDown() || readTouch())
    active_delay(100);
    
  toggleBandSelect();
  return; 
}



int setCwSpeed() {
  int knob = 0;
  int wpm;

  wpm = 1200 / cwSpeed;

  wpm = getValueByKnob(1, 100, 1,  wpm, "CW: ", " WPM");

  cwSpeed = 1200 / wpm;

  EEPROM.put(CW_SPEED, cwSpeed);
  active_delay(500);
  drawStatusbar();
  //    printLine2("");
  //    updateDisplay();
}


void setCwTone() {
  int knob = 0;
  //int prev_sideTone;

  // N8LOV init display
  char displayInit = 0;

  while (btnDown()); // N8LOV - wait for button up (for button menu function)

  //disable all clock 1 and clock 2
  while (digitalRead(PTT) == HIGH && (!btnDown() && !readTouch())) // N8LOV
  {
    knob = enc_read();

    if (!displayInit) // N8LOV
      displayInit = 1;
    else if (knob > 0 && sideTone < 2000)
      sideTone += 10;
    else if (knob < 0 && sideTone > 100 )
      sideTone -= 10;
    else
      continue; //don't update the frequency or the display

    tone(CW_TONE, sideTone);
    itoa(sideTone, c, 10);
    strcpy(b, "CW Tone: ");
    strcat(b, c);
    strcat(b, " Hz");
    drawCommandbar(b);
    //printLine2(b);

    checkCAT();
    active_delay(20);
  }
  noTone(CW_TONE);
  //save the setting
  EEPROM.put(CW_SIDETONE, sideTone);

  clearCommandbar(); // N8LOV
  //displayFillrect(30,41,280, 32, DISPLAY_NAVY);
  drawStatusbar();
  //  printLine2("");
  //  updateDisplay();
}

void doCommand(struct Button *b) {

/* -RIT
  if (!strcmp(b->text, "RIT"))
    ritToggle(b);
  else
-RIT */ 
  if (!strcmp(b->text, "LSB"))
    sidebandToggle(b);
  else if (!strcmp(b->text, "USB"))
    sidebandToggle(b);
  else if (!strcmp(b->text, "CW"))
    cwToggle(b);
  else if (!strcmp(b->text, "SPL"))
    splitToggle(b);
  else if (!strcmp(b->text, "VFOA")) {
    if (vfoActive == VFO_A)
      fastTune();
    else
      switchVFO(VFO_A);
  }
  else if (!strcmp(b->text, "VFOB")) {
    if (vfoActive == VFO_B)
      fastTune();
    else
      switchVFO(VFO_B);
  }
  else if (!strcmp(b->text, "SAV"))
    saveActiveVFO(b);
  else if (!strcmp(b->text, "RCL"))
    recallActiveVFO(b);
  else if (!strcmp(b->text, "1Kz"))
    oneKhzToggle(b);
  else if (!strcmp(b->text, "A>I"))
    Act2Inact(b);
  else if (!strcmp(b->text, "BND"))
    selectBand(b);
  else if (!strcmp(b->text, "FRQ"))
    enterFreq();
  else if (!strcmp(b->text, "WPM"))
    setCwSpeed();
  else if (!strcmp(b->text, "TON"))
    setCwTone();
}

void  checkTouch() {

  if (!readTouch())
    return;

  while (readTouch())
    checkCAT();
  scaleTouch(&ts_point);

  /* //debug code
    Serial.print(ts_point.x); Serial.print(' ');Serial.println(ts_point.y);
  */
  int total = sizeof(btn_set) / sizeof(struct Button);
  for (int i = 0; i < MAX_BUTTONS; i++) {
    struct Button b;
    memcpy_P(&b, btn_set + i, sizeof(struct Button));

    int x2 = b.x + b.w;
    int y2 = b.y + b.h;

    if (b.x < ts_point.x && ts_point.x < x2 &&
        b.y < ts_point.y && ts_point.y < y2)
      doCommand(&b);
  }
}

//returns true if the button is pressed
int btnDown() {
  if (digitalRead(FBUTTON) == HIGH)
    return 0;
  else
    return 1;
}


void drawFocus(int ibtn, int color) {
  struct Button b;

  memcpy_P(&b, btn_set + ibtn, sizeof(struct Button));
  displayRect(b.x, b.y, b.w, b.h, color);
}


void doCommands() {
  int select = 0, i = 0, prevButton = 0, btnState = 0;
  select = (vfoActive == VFO_A ? 0 : 1);
  prevButton = select;
  
  //wait for the button to be raised up
  while (btnDown())
    active_delay(50);
  active_delay(50);  //debounce

  menuOn = 2;

  while (menuOn) {

    //check if the knob's button was pressed
    btnState = btnDown();
    if (btnState) {
      struct Button b;
      memcpy_P(&b, btn_set + select, sizeof(struct Button));
      
      //wait for the button to be up and debounce
      while (btnDown())
        active_delay(100);
      doCommand(&b);

      //unfocus the buttons
      drawFocus(select, DISPLAY_NAVY);
      if (vfoActive == VFO_A)
        drawFocus(0, DISPLAY_WHITE);
      else
        drawFocus(1, DISPLAY_WHITE);


      active_delay(500);
      menuOn=0;
      return;
    }

    i = enc_read();

    if (i == 0) {
      active_delay(50);
      continue;
    }

    if (i > 0) {
      if (select + 1 < MAX_BUTTONS)
        select += 1;
    }
    if (i < 0 && select - 1 >= 0)
      select += -1;      

    if (prevButton == select)
      continue;

    //we are on a new button
    if (prevButton < 2) // for VFOs
      drawFocus(prevButton, DISPLAY_NAVY);
    else
      drawFocus(prevButton, DISPLAY_DARKGREY);
    drawFocus(select, DISPLAY_WHITE);
    prevButton = select;

    active_delay(100);
  }


  //debounce the button
  while (btnDown())
    active_delay(50);
  active_delay(50);

  checkCAT();
  
}
