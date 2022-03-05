#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <ezButton.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Tone32.h>
#include <TFT_eSPI.h>
#include "wifiCredentials.h"

// Wifi setup
unsigned long lastReconnect = 0;
unsigned long reconnectInterval = 60000;

// LED setup
#define LED_PIN 25
#define LED_COUNT 144
int LEDAnimationState = -1;
unsigned long LEDAnimationTimer = 0;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Button setup
#define DOOR_SENSOR_1 33
#define DOOR_SENSOR_2 32
#define DEBOUNCE_TIME 1000

ezButton doorSensorLeft(DOOR_SENSOR_1);
ezButton doorSensorRight(DOOR_SENSOR_2);

// RFID setup
#define SS_PIN 15
#define RST_PIN 21

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// Buzzer setup
#define BUZZER_PIN 26

// TFT Display Setup
#define BACKLIGHT_PIN 27

TFT_eSPI tft = TFT_eSPI();

void useLEDAnimation() {
  if(LEDAnimationState == -1 || millis() - LEDAnimationTimer < 20) {
    return;
  }

  if(LEDAnimationState < 57) {
    strip.setPixelColor(LEDAnimationState, strip.Color(255, 50, 0));
    strip.setPixelColor(LED_COUNT - LEDAnimationState, strip.Color(255, 50, 0));
  } else if(LEDAnimationState == 57) {
    strip.fill(strip.Color(255, 50, 0), 57, LED_COUNT-57);
  } else {
    LEDAnimationState = -1;
    return;
  }

  strip.show();
  LEDAnimationState++;
  LEDAnimationTimer = millis();
}

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

  tone(BUZZER_PIN, NOTE_A4, 500, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2);

  for(byte i=0; i<rfid.uid.size; i++) {
    tft.print(rfid.uid.uidByte[i], HEX);
    if(i+1 < rfid.uid.size) {
      tft.print(':');
    }
  }

  rfid.PICC_HaltA();

  rfid.PCD_StopCrypto1();
}

void printWeatherInfo() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor((tft.getViewportWidth()-tft.textWidth("rain", 2))/2, 0, 2);
  tft.println("rain");
  tft.setCursor(0, 50);
  tft.setTextSize(1);
  tft.print("  2");
  tft.print((char)96);
  tft.println("C  63%  1035hPa");
}

void setup() {
  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  ArduinoOTA.begin();

  pinMode(DOOR_SENSOR_1, INPUT_PULLUP);
  pinMode(DOOR_SENSOR_2, INPUT_PULLUP);
  doorSensorLeft.setDebounceTime(DEBOUNCE_TIME);
  doorSensorRight.setDebounceTime(DEBOUNCE_TIME);

  SPI.begin();
  rfid.PCD_Init();
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
  
  strip.begin();
  strip.setBrightness(100);
  
  pinMode(BACKLIGHT_PIN, OUTPUT);
  tft.init();
  tft.setRotation(3);
  if(doorSensorLeft.getState() || doorSensorRight.getState()) {
    digitalWrite(BACKLIGHT_PIN, HIGH);
    printWeatherInfo();
    LEDAnimationState = 0;
    LEDAnimationTimer = millis();
  } else {
    digitalWrite(BACKLIGHT_PIN, LOW);
  }
}

void loop() {
  if((WiFi.status() != WL_CONNECTED) && (millis() - lastReconnect >= reconnectInterval)) {
    WiFi.disconnect();
    WiFi.reconnect();
    lastReconnect = millis();
  }

  ArduinoOTA.handle();

  doorSensorLeft.loop();
  doorSensorRight.loop();

  if((doorSensorLeft.isPressed() && !doorSensorRight.getState()) || (doorSensorRight.isPressed() && !doorSensorLeft.getState())) {
    Serial.println("Turning off LEDs");
    strip.fill(strip.Color(0, 0, 0), 0, LED_COUNT);
    strip.show();
    tft.fillScreen(TFT_BLACK);
    digitalWrite(BACKLIGHT_PIN, LOW);
  }

  if((doorSensorLeft.isReleased() && !doorSensorRight.getState()) || (doorSensorRight.isReleased() && !doorSensorLeft.getState())) {
    Serial.println("Turning on LEDs");
    LEDAnimationState = 0;
    LEDAnimationTimer = millis();
    printWeatherInfo();
    digitalWrite(BACKLIGHT_PIN, HIGH);
  }

  useLEDAnimation();

  useRFID();
}