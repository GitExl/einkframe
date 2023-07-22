// MIT License

// Copyright (c) 2023 Dennis Meuwissen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "epd7in5b.h"
#include <SD.h>
#include <string.h>
#include <Adafruit_SleepyDog.h>


#define PIN_BUTTON      A0   // Button
#define PIN_ORIENTATION A1   // Orientation sensor
#define PIN_SD_LED      8    // SD card LED


#define EPD_CONNECTED     1     // Run with or without an ePaper display connected, for debugging.
#define SERIAL_WAIT       0     // Wait for serial connection before starting.
#define UPDATE_TIME       86400 // How often to wait before refreshing.
#define UPDATE_SHORT_TIME 21600 // How long to wait before refreshing after a user interaction.


// The way in which we woke up from sleep.
#define WAKE_NONE        0
#define WAKE_BUTTON      1

char wakeState = WAKE_NONE;
int sleepTime = 0;
int nextSleepTime = UPDATE_SHORT_TIME;


// Current device state.
#define STATE_RUNNING     0
#define STATE_LOW_BATTERY 1
#define STATE_NO_SD       2
#define STATE_EPD_FAIL    3
#define STATE_BLANK       4

char state = STATE_RUNNING;
int previousIndex = -1;


// Orientation as read from tilt switch sendor.
#define ORIENTATION_LANDSCAPE 1
#define ORIENTATION_PORTRAIT 0

const char* ORIENTATION_PATH[] = {
  "/LANDSCAP/",
  "/PORTRAIT/"
};

char currentOrientation = ORIENTATION_LANDSCAPE;


// References to compressed builtin status images.
extern const unsigned char IMAGE_DATA_CARD_PORTRAIT[];
extern const unsigned char IMAGE_DATA_CARD_LANDSCAPE[];

extern const unsigned char IMAGE_DATA_BATTERY_PORTRAIT[];
extern const unsigned char IMAGE_DATA_BATTERY_LANDSCAPE[];


Epd epd;


/**
 * Display an image from a bin file.
 */
void displayImageFile(const char* filename, int orientation) {
  char filepath[255];
  snprintf(filepath, 255, "%s%s", ORIENTATION_PATH[orientation], filename);

  if (!SD.exists(filepath)) {
    Serial.print("Could not find file "); Serial.println(filepath);
    failSD();
    return;
  }

  SDFile img = SD.open(filepath, FILE_READ);
  if (!img) {
    Serial.println("Could not open file.");
    failSD();
    return;
  }

  Serial.print("Displaying "); Serial.println(filepath);
  #if EPD_CONNECTED
    if (epd.Init() != 0) {
      Serial.println("EPD init failed.");
      failEPD();
      return;
    }

    epd.DisplayImageFromFile(img);
    epd.Sleep();
  #endif

  img.close();
}

/**
 * Get a list of filenames inside a directory.
 */
char** getFileList(SDFile dir, int* count) {
  const int MAX_FILES = 64;

  Serial.print("Read directory "); Serial.println(dir.name());

  char** entries = (char**) malloc(MAX_FILES * sizeof(char*));

  int currentIndex = 0;
  while (true) {
    SDFile entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    if (!entry.isDirectory()) {
      entries[currentIndex] = strdup(entry.name());
      currentIndex++;
    }
    entry.close();

    if (currentIndex == MAX_FILES) {
      break;
    }
  }

  *count = currentIndex;
  return entries;
}

/**
 * Free a list of filename strings and the list itself.
 */
void freeFileList(char** list, int count) {
  for (int i = 0; i < count; i++) {
    free(list[i]);
  }
  free(list);
}

/**
 * Sleep for next cycle.
 */
void loopSleep() {
  sleepTime = 0;
  wakeState = WAKE_NONE;
  while (sleepTime < nextSleepTime && wakeState == WAKE_NONE) {

    // Wake up early if button is pressed.
    if (!digitalRead(PIN_BUTTON)) {
      wakeState = WAKE_BUTTON;
      nextSleepTime = UPDATE_SHORT_TIME;
      return;
    }

    sleepTime += Watchdog.sleep(4000) / 1000;
  }

  nextSleepTime = UPDATE_TIME;
}

/**
 * Show SD card error image.
 */
void displayFailImage(const unsigned char data_portrait[], const unsigned char data_landscape[]) {
  #if EPD_CONNECTED
    updateOrientation();

    if (epd.Init() != 0) {
      Serial.println("EPD init failed.");
      failEPD();
      return;
    }

    if (currentOrientation == ORIENTATION_PORTRAIT) {
      epd.DisplayCompressedImage(data_portrait);
    } else {
      epd.DisplayCompressedImage(data_landscape);
    }
    epd.Sleep();

    // Blank next cycle.
    loopSleep();
    epd.DisplayBlank();
    epd.Sleep();
  #endif
}

/**
 * E-paper display failure detected.
 */
void failEPD() {
  Serial.println("EPD failure.");
  state = STATE_EPD_FAIL;

  for (int i = 0; i < 60; i++) {
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(900);
    digitalWrite(PIN_LED, HIGH);
  }
}

/**
 * SD card failure detected.
 */
void failSD() {
  Serial.println("SD card failure.");
  state = STATE_NO_SD;
}

/**
 * Low battery detected.
 */
void failBattery() {
  Serial.println("Low battery.");
  state = STATE_LOW_BATTERY;
}

/**
 * Update current orientation from tilt sensor.
 *
 * Sensor must be mounted with its pins facing down.
 */
char updateOrientation() {

  // Read tilt sensor state from A1.
  pinMode(PIN_ORIENTATION, INPUT);
  digitalWrite(PIN_ORIENTATION, HIGH);
  delay(20);
  const int val = digitalRead(PIN_ORIENTATION);
  Serial.print("Orientation "); Serial.println(val);

  char newOrientation = ORIENTATION_PORTRAIT;
  if (val) {
    newOrientation = ORIENTATION_LANDSCAPE;
  }

  char changed = (newOrientation != currentOrientation);
  currentOrientation = newOrientation;
  return changed;
}

/**
 * Get a random BIN image in the current orientation and display it.
 */
void displayRandomImage() {

  // Open orientation directory.
  updateOrientation();
  SDFile dir = SD.open(ORIENTATION_PATH[currentOrientation], FILE_READ);
  if (!dir) {
    Serial.print("Cannot open orientation directory "); Serial.println(ORIENTATION_PATH[currentOrientation]);
    failSD();
    return;
  }

  // List all files inside.
  int count;
  char** list = getFileList(dir, &count);
  dir.close();

  // Pick a random file and display it.
  int index;
  for (int i = 0; i < 10; i++) {
    index = random(0, count);
    if (index != previousIndex) {
      break;
    }
  }
  displayImageFile(list[index], currentOrientation);
  previousIndex = index;

  freeFileList(list, count);
}

/**
 * Measures the current battery voltage.
 */
float measureBattery() {
  const float measuredV = analogRead(A7);
  return ((measuredV * 2.0) * 3.3) / 1024.0;
}

/**
 * Setup.
 */
void setup() {

  // LED off.
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // SD card LED off?
  pinMode(PIN_SD_LED, OUTPUT);
  digitalWrite(PIN_SD_LED, LOW);

  #if SERIAL_WAIT
    Serial.begin(9600);
    while (!Serial);
  #endif

  if (!SD.begin(4)) {
    Serial.println("SD begin failed.");
    failSD();
    return;
  }

  // Random seed from analog noise.
  randomSeed(analogRead(0));

  // Enable button pullup.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

/**
 * Loop.
 */
void loop() {
  const float v = measureBattery();

  // Very low battery.
  if (v < 3.6) {
    state = STATE_BLANK;

  // Low battery.
  } else if (v < 3.7) {
    failBattery();

  // Resume from SD failure.
  } else if (wakeState == WAKE_BUTTON && state == STATE_NO_SD) {
    state = STATE_RUNNING;

  // Resume from blanked mode.
  } else if (wakeState == WAKE_BUTTON && state == STATE_BLANK) {
    state = STATE_RUNNING;

  // Regular Button input.
  } else if (wakeState == WAKE_BUTTON) {

    // If the orientation changed, just show a new image.
    // Otherwise we should blank.
    if (!updateOrientation()) {
      state = STATE_BLANK;
    } else {
      state = STATE_RUNNING;
    }

  }

  if (state == STATE_LOW_BATTERY) {
    displayFailImage(IMAGE_DATA_BATTERY_PORTRAIT, IMAGE_DATA_BATTERY_LANDSCAPE);
    state = STATE_BLANK;

  } else if (state == STATE_NO_SD) {
    displayFailImage(IMAGE_DATA_CARD_PORTRAIT, IMAGE_DATA_CARD_LANDSCAPE);
    state = STATE_BLANK;

  } else if (state == STATE_EPD_FAIL) {
    state = STATE_BLANK;

  } else if (state == STATE_RUNNING) {
    displayRandomImage();

  } else {
    Serial.println("Blanking.");
    #if EPD_CONNECTED
      if (epd.Init() != 0) {
        Serial.println("EPD init failed.");
        failEPD();
        return;
      }
      epd.DisplayBlank();
      epd.Sleep();
    #endif

  }

  loopSleep();
}

