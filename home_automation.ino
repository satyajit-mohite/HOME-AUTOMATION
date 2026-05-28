# HOME-AUTOMATION
// IoT based Home Automation by using ESP8266 & Firebase
// Created By  : SATYAJIT MOHITE
// Version     : 2.4 FINAL - Single Firebase read for all devices
//
// KEY IMPROVEMENT: Reads ALL 4 tags in ONE Firebase call
// This eliminates timeout delays caused by sequential reads
//
// EXACT Firebase tags:
//   "Door"  -> "0" or "1"
//   "Lamp"  -> ""off"" or ""on""
//   "fan"   -> ""OFF"" or ""ON""
//   "light" -> ""OFF"" or ""ON""
//
// RELAY TYPE : Active LOW
//   RELAY_ON  = LOW  = Device ON
//   RELAY_OFF = HIGH = Device OFF

#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include <EEPROM.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// -----------------------------------------------------------------------
//  CONFIG
// -----------------------------------------------------------------------
#define WIFI_SSID     "satu"
#define WIFI_PASSWORD "123456789000"
#define FIREBASE_HOST "home-automation-b59b7-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "77Fu2PkvBsnCh5zZV8RMtty4VommcWfCWNX5WICw"

// -----------------------------------------------------------------------
//  PIN DEFINITIONS
// -----------------------------------------------------------------------
#define MainLamp   16   // D0 -> "light"
#define NightLamp   5   // D1 -> "Lamp"
#define FanPin      4   // D2 -> "fan"
#define DoorLock    0   // D3 -> "Door"

// Relay logic
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// -----------------------------------------------------------------------
//  EEPROM
// -----------------------------------------------------------------------
#define ADDR_LIGHT  0
#define ADDR_LAMP   1
#define ADDR_FAN    2
#define ADDR_DOOR   3
#define EEPROM_SIZE 4

// -----------------------------------------------------------------------
//  FIREBASE OBJECTS
// -----------------------------------------------------------------------
FirebaseData   fbdo;
FirebaseData   fbdo2;   // second object for parallel reads
FirebaseAuth   auth;
FirebaseConfig config;

// -----------------------------------------------------------------------
//  GLOBAL STATE
// -----------------------------------------------------------------------
String prev_light = "";
String prev_lamp  = "";
String prev_fan   = "";
String prev_door  = "";

unsigned long lastReadTime   = 0;
unsigned long lastHealthTime = 0;

#define READ_INTERVAL    800    // normal poll interval ms
#define FIREBASE_TIMEOUT 3000  // timeout per read ms

// -----------------------------------------------------------------------
//  EEPROM FUNCTIONS
// -----------------------------------------------------------------------
void saveToEEPROM(int address, int value) {
  EEPROM.write(address, value);
  EEPROM.commit();
}

int readFromEEPROM(int address) {
  int val = EEPROM.read(address);
  if (val != 0 && val != 1) return 0;
  return val;
}

void applyEEPROMState(int address, int pin, String name) {
  int state = readFromEEPROM(address);
  digitalWrite(pin, state ? RELAY_ON : RELAY_OFF);
  Serial.println("  [EEPROM] " + name + " -> " + (state ? "ON" : "OFF"));
}

// -----------------------------------------------------------------------
//  STRING CLEANER
// -----------------------------------------------------------------------
String cleanValue(String raw) {
  raw.replace("\"", "");
  raw.replace("'",  "");
  raw.replace("[",  "");
  raw.replace("]",  "");
  raw.replace("\\", "");
  raw.trim();
  raw.toLowerCase();
  return raw;
}

// -----------------------------------------------------------------------
//  APPLY STATE TO PIN
//  Returns true if state changed
// -----------------------------------------------------------------------
bool applyState(String newVal, String &prevVal, int pin,
                int eepromAddr, String deviceName) {

  if (newVal == "" || newVal == prevVal) return false;

  if (newVal == "on" || newVal == "1" || newVal == "true") {
    digitalWrite(pin, RELAY_ON);
    saveToEEPROM(eepromAddr, 1);
    Serial.println(">>> " + deviceName + " ON  [EEPROM saved]");
    prevVal = newVal;
    return true;
  }
  else if (newVal == "off" || newVal == "0" || newVal == "false") {
    digitalWrite(pin, RELAY_OFF);
    saveToEEPROM(eepromAddr, 0);
    Serial.println(">>> " + deviceName + " OFF [EEPROM saved]");
    prevVal = newVal;
    return true;
  }
  else {
    Serial.println("  [UNKNOWN] " + deviceName + " = [" + newVal + "]");
    return false;
  }
}

// -----------------------------------------------------------------------
//  WIFI CONNECT
// -----------------------------------------------------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("  Connecting WiFi");
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) {
    delay(500);
    Serial.print(".");
    t++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  WiFi OK -> " + WiFi.localIP().toString());
  } else {
    Serial.println("\n  WiFi FAILED - restarting...");
    delay(2000);
    ESP.restart();
  }
}

// -----------------------------------------------------------------------
//  READ ALL DEVICES IN ONE CALL using JSON
//  Reads entire database root at once - much faster than 4 separate reads
// -----------------------------------------------------------------------
void readAllDevices() {

  // Read entire database root "/" in one single call
  if (!Firebase.RTDB.getJSON(&fbdo, "/")) {
    Serial.println("  JSON read FAIL: " + fbdo.errorReason());
    // Fallback to individual reads if JSON fails
    readIndividual();
    return;
  }

  // Parse the JSON result
  FirebaseJson &json = fbdo.jsonObject();
  FirebaseJsonData result;

  // --- light ---
  json.get(result, "light");
  if (result.success) {
    String val = cleanValue(result.stringValue);
    applyState(val, prev_light, MainLamp, ADDR_LIGHT, "Main Lamp ");
  }

  // --- Lamp ---
  json.get(result, "Lamp");
  if (result.success) {
    String val = cleanValue(result.stringValue);
    applyState(val, prev_lamp, NightLamp, ADDR_LAMP, "Night Lamp");
  }

  // --- fan ---
  json.get(result, "fan");
  if (result.success) {
    String val = cleanValue(result.stringValue);
    applyState(val, prev_fan, FanPin, ADDR_FAN, "Fan       ");
  }

  // --- Door ---
  json.get(result, "Door");
  if (result.success) {
    String val = cleanValue(result.stringValue);
    applyState(val, prev_door, DoorLock, ADDR_DOOR, "Door Lock ");
  }
}

// -----------------------------------------------------------------------
//  FALLBACK - Individual reads if JSON call fails
// -----------------------------------------------------------------------
void readIndividual() {
  Serial.println("  Using fallback individual reads...");

  FirebaseData tempFbdo;
  tempFbdo.setResponseSize(512);

  // light
  if (Firebase.RTDB.getString(&tempFbdo, "/light")) {
    applyState(cleanValue(tempFbdo.stringData()),
               prev_light, MainLamp, ADDR_LIGHT, "Main Lamp ");
  }
  // Lamp
  if (Firebase.RTDB.getString(&tempFbdo, "/Lamp")) {
    applyState(cleanValue(tempFbdo.stringData()),
               prev_lamp, NightLamp, ADDR_LAMP, "Night Lamp");
  }
  // fan
  if (Firebase.RTDB.getString(&tempFbdo, "/fan")) {
    applyState(cleanValue(tempFbdo.stringData()),
               prev_fan, FanPin, ADDR_FAN, "Fan       ");
  }
  // Door
  if (Firebase.RTDB.getString(&tempFbdo, "/Door")) {
    applyState(cleanValue(tempFbdo.stringData()),
               prev_door, DoorLock, ADDR_DOOR, "Door Lock ");
  }
}

// -----------------------------------------------------------------------
//  SETUP
// -----------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n==============================");
  Serial.println("  HOME AUTOMATION v2.4 FINAL  ");
  Serial.println("==============================");

  EEPROM.begin(EEPROM_SIZE);
  delay(100);
  Serial.println("  EEPROM OK");

  pinMode(MainLamp,  OUTPUT);
  pinMode(NightLamp, OUTPUT);
  pinMode(FanPin,    OUTPUT);
  pinMode(DoorLock,  OUTPUT);

  digitalWrite(MainLamp,  RELAY_OFF);
  digitalWrite(NightLamp, RELAY_OFF);
  digitalWrite(FanPin,    RELAY_OFF);
  digitalWrite(DoorLock,  RELAY_OFF);

  Serial.println("  Restoring EEPROM states...");
  applyEEPROMState(ADDR_LIGHT, MainLamp,  "light (Main Lamp) ");
  applyEEPROMState(ADDR_LAMP,  NightLamp, "Lamp  (Night Lamp)");
  applyEEPROMState(ADDR_FAN,   FanPin,    "fan   (Fan)       ");
  applyEEPROMState(ADDR_DOOR,  DoorLock,  "Door  (Door Lock) ");

  connectWiFi();

  // Firebase config
  config.database_url               = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  config.token_status_callback      = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Set response size - 2048 needed for JSON read of whole database
  fbdo.setResponseSize(2048);

  Serial.println("  Firebase initializing...");
  delay(3000);

  Serial.println("  Free Heap : " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("==============================");
  Serial.println("        SYSTEM READY          ");
  Serial.println("==============================\n");
}

// -----------------------------------------------------------------------
//  LOOP
// -----------------------------------------------------------------------
void loop() {

  // Memory watchdog
  if (ESP.getFreeHeap() < 6000) {
    Serial.println("LOW MEMORY - Restarting...");
    delay(500);
    ESP.restart();
  }

  // WiFi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - Reconnecting...");
    connectWiFi();
    return;
  }

  // Firebase ready check
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready...");
    delay(1000);
    return;
  }

  // Read every 800ms
  if (millis() - lastReadTime < READ_INTERVAL) return;
  lastReadTime = millis();

  // ONE single Firebase call reads ALL 4 devices at once
  readAllDevices();

  // Health report every 60 seconds
  if (millis() - lastHealthTime >= 60000) {
    lastHealthTime = millis();
    Serial.println("\n--- HEALTH ---");
    Serial.println("  Uptime : " + String(millis() / 1000) + "s");
    Serial.println("  Heap   : " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("  light=" + prev_light + " Lamp=" + prev_lamp +
                   " fan="   + prev_fan   + " Door=" + prev_door);
    Serial.println("--------------\n");
  }
}
