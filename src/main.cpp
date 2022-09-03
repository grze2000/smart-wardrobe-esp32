#include <Arduino.h>
#include <ezButton.h>
#include <SPI.h>
#include <MFRC522.h>
#include <TFT_eSPI.h>
#include <Tone32.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <SparkFun_APDS9960.h>
// #include <EEPROM.h>
#include "wifiCredentials.h"

// LED setup
#define LED_PIN 25
#define LED_COUNT 470
#define WARDROBE_LED_END 143
#define DESK_LED_END 199
#define BOOKSHELF1_LED_END 235
#define WALL_CABINET_LED_END 246
#define WARDROBE_LED_END 281
#define DRESSER_LED_END 305
#define BOOKSHELF2_BACK_LED_END 340
#define BOOKSHELF2_LED_END 434
#define BOOKSHELF3_LED_END 470
#define DESK_LED_ANIMATION_DELAY 50
int LEDAnimationState = -1;
unsigned long LEDAnimationTimer = 0;
unsigned long FurnitureAnimationTimer = 0;
long firstPixelHue = 0;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Wifi setup
unsigned long lastReconnect = millis();
unsigned long reconnectInterval = 20000;

// Button setup
#define DOOR_SENSOR_1 33
#define DOOR_SENSOR_2 32
#define DEBOUNCE_TIME 1000

ezButton doorSensorLeft(DOOR_SENSOR_1);
ezButton doorSensorRight(DOOR_SENSOR_2);

// RFID Setup
#define SS_PIN 15
#define RST_PIN 3

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Buzzer Setup
#define BUZZER_PIN  26
#define BUZZER_CHANNEL 0

// TFT Display Setup
#define BACKLIGHT_PIN 27

TFT_eSPI tft = TFT_eSPI();

// Gesture sensor setup
int deskLedsBrightness = 5;
bool warm = false;
SparkFun_APDS9960 apds = SparkFun_APDS9960();

/**
 * Helper routine to dump a byte array as hex values to Serial. 
 */
void printHex(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}

void useRFID() {
  if ( ! rfid.PICC_IsNewCardPresent())
    return;

  if ( ! rfid.PICC_ReadCardSerial())
    return;

  Serial.println(F("The NUID tag is:"));
  Serial.print(F("In hex: "));
  printHex(rfid.uid.uidByte, rfid.uid.size);
  Serial.println();

  tone(BUZZER_PIN, NOTE_A4, 500, BUZZER_CHANNEL);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2);

  for (byte i = 0; i < rfid.uid.size; i++) {
    tft.print(rfid.uid.uidByte[i], HEX);
    if(i+1 < rfid.uid.size) {
      tft.print(":");
    }
  }

  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}

void printWeatherInfo() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE); 
  tft.setTextSize(2);
  tft.setCursor((tft.getViewportWidth()-tft.textWidth("rain", 2))/2, 0, 2);
  tft.println("rain");
  tft.setTextSize(1);
  tft.setCursor(0, 50);
  tft.print("  2");
  tft.print((char)96);
  tft.println("C  63%  1035hPa");
}

void useLEDAnimation() {
  if(LEDAnimationState == -1 || millis() - LEDAnimationTimer < 20) {
    return;
  }

  if(LEDAnimationState < 57) {
    strip.setPixelColor(LEDAnimationState, strip.Color(255, 50, 0));
    strip.setPixelColor(WARDROBE_LED_END-LEDAnimationState, strip.Color(255, 50, 0));
  } else if(LEDAnimationState == 57) {
    strip.fill(strip.Color(255, 50, 0), 57, 87);
  } else {
    LEDAnimationState = -1;
    return;
  }
  strip.show();
  LEDAnimationState++;
  LEDAnimationTimer = millis();
}

void useDeskLEDAnimation() {
  if(millis() - FurnitureAnimationTimer > DESK_LED_ANIMATION_DELAY) {
    // bookshelf1
    for(int i=DESK_LED_END; i<BOOKSHELF1_LED_END; i++) {
      int pixelHue = firstPixelHue + (i-DESK_LED_END+1 * 65536L / BOOKSHELF1_LED_END-DESK_LED_END);
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }

    // wall cabinet
    for(int i=BOOKSHELF1_LED_END; i<LED_COUNT; i++) {
      int pixelHue = firstPixelHue + (i-BOOKSHELF1_LED_END+1 * 65536L / LED_COUNT-BOOKSHELF1_LED_END);
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    }

    // bookshelf2
    // for(int i=WALL_CABINET_LED_END; i<DRESSER_LED_END; i++) {
    //   int pixelHue = firstPixelHue + (i-BOOKSHELF1_LED_END+1 * 65536L / DRESSER_LED_END-WALL_CABINET_LED_END);
    //   strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue)));
    // }

    
    strip.show();
    FurnitureAnimationTimer = millis();
    firstPixelHue += 256;
    if(firstPixelHue > 65536) {
      firstPixelHue = 0;
    }
  }
}

void handleGesture() {
  if ( apds.isGestureAvailable() ) {
    switch ( apds.readGesture() ) {
      case DIR_UP:
        if(deskLedsBrightness > 1 && strip.getPixelColor(WARDROBE_LED_END+1) != strip.Color(0, 0, 0)) {
          deskLedsBrightness--;
          // EEPROM.write(0, deskLedsBrightness);
          // EEPROM.commit();
          strip.fill(strip.Color(51*deskLedsBrightness, 51*deskLedsBrightness, 51*deskLedsBrightness), WARDROBE_LED_END+1, DESK_LED_END-WARDROBE_LED_END);
        }
        break;
      case DIR_DOWN:
        if(deskLedsBrightness < 5 && strip.getPixelColor(WARDROBE_LED_END+1) != strip.Color(0, 0, 0)) {
          deskLedsBrightness++;
          // EEPROM.write(0, deskLedsBrightness);
          // EEPROM.commit();
          strip.fill(strip.Color(51*deskLedsBrightness, 51*deskLedsBrightness, 51*deskLedsBrightness), WARDROBE_LED_END+1, DESK_LED_END-WARDROBE_LED_END);
        }
        break;
      case DIR_LEFT:
        strip.fill(strip.Color(0, 0, 0), WARDROBE_LED_END+1, DESK_LED_END-WARDROBE_LED_END);
        break;
      case DIR_RIGHT:
        strip.fill(strip.Color(51*deskLedsBrightness, 51*deskLedsBrightness, 51*deskLedsBrightness), WARDROBE_LED_END+1, DESK_LED_END-WARDROBE_LED_END);
        break;
      case DIR_NEAR:
        warm = !warm;
        strip.fill(warm ? strip.Color(240, 186, 86) : strip.Color(51*deskLedsBrightness, 51*deskLedsBrightness, 51*deskLedsBrightness), WARDROBE_LED_END+1, DESK_LED_END-WARDROBE_LED_END);
        break;
      case DIR_FAR:
        Serial.println("FAR");
        break;
      default:
        Serial.println("NONE");
    }
    strip.show();
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Rebooting...");

  pinMode(DOOR_SENSOR_1, INPUT_PULLUP);
  pinMode(DOOR_SENSOR_2, INPUT_PULLUP);
  doorSensorLeft.setDebounceTime(DEBOUNCE_TIME);
  doorSensorRight.setDebounceTime(DEBOUNCE_TIME);
  
  SPI.begin();
  rfid.PCD_Init();
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  pinMode(BACKLIGHT_PIN, OUTPUT);
  tft.init();
  tft.setRotation(3);
  printWeatherInfo();
  if(!doorSensorLeft.getState() && !doorSensorRight.getState()) {
    digitalWrite(BACKLIGHT_PIN, LOW);
  }

  strip.begin();
  strip.setBrightness(100);
  strip.show();
  if(doorSensorLeft.getState() || doorSensorRight.getState()) {
    LEDAnimationState = 0;
    LEDAnimationTimer = millis();
  }
  strip.show();

  // Initialize APDS-9960 (configure I2C and initial values)
  if ( apds.init() ) {
    Serial.println(F("APDS-9960 initialization complete"));
  } else {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }
  
  // Start running the APDS-9960 gesture sensor engine
  if ( apds.enableGestureSensor(true) ) {
    Serial.println(F("Gesture sensor is now running"));
  } else {
    Serial.println(F("Something went wrong during gesture sensor init!"));
  }

  // EEPROM.begin(1);
  // deskLedsBrightness = EEPROM.read(0);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  lastReconnect = millis();

  ArduinoOTA.begin();
}

void loop() {
  if ((WiFi.status() != WL_CONNECTED) && (millis() - lastReconnect >= reconnectInterval)) {
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    lastReconnect = millis();
  }

  ArduinoOTA.handle();
  
  doorSensorLeft.loop();
  doorSensorRight.loop();

  if((doorSensorRight.isPressed() && !doorSensorLeft.getState()) || (doorSensorLeft.isPressed() && !doorSensorRight.getState()) || (doorSensorLeft.isPressed() && doorSensorRight.isPressed())) {
    Serial.println("Turn off LEDs");
    tft.fillScreen(TFT_BLACK);
    digitalWrite(BACKLIGHT_PIN, LOW);
    strip.fill(strip.Color(0, 0, 0), 0, 144);
    strip.show();
  }
  if((doorSensorRight.isReleased() && !doorSensorLeft.getState()) || (doorSensorLeft.isReleased() && !doorSensorRight.getState())) {
    Serial.println("Turn on LEDs");
    printWeatherInfo();
    digitalWrite(BACKLIGHT_PIN, HIGH);
    LEDAnimationState = 0;
    LEDAnimationTimer = millis();
  }

  useLEDAnimation();

  useRFID();
  
  handleGesture();

  useDeskLEDAnimation();
}