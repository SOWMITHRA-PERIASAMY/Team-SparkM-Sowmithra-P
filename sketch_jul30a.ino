// Lab Asset Checkout Beacon — Complete Firmware (Stages 1-5 Integrated + Reset Support)
// Stage 1: two-tap checkout flow with LCD feedback
// Stage 2: real checkout/return/conflict logic + LED/buzzer feedback
// Stage 3: local flash logging (SPIFFS) -- survives power loss, tracks usage count
// Stage 4: WiFi sync to backend -- queues transactions, syncs only when online
// Stage 5: Cross-Validation -- prevents double-tapping and user/asset card mismatches

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define WIFI_SSID     "LAPTOP-JEQQ6HNO 2787"
#define WIFI_PASSWORD "12345678"
#define BACKEND_URL   "http://10.169.116.174:8000/api/transactions"
// ------------------------------------

#define SS_PIN     5
#define RST_PIN    4
#define LED_RED    25
#define LED_AMBER  26
#define LED_GREEN  27
#define BUZZER     32
#define TAMPER_PIN 33

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

enum State { WAIT_USER, WAIT_ASSET };
State currentState = WAIT_USER;
String userUID = "";

// ---------- asset state (Stage 2 + 3) ----------
const int MAX_ASSETS = 20;
struct AssetRecord {
  String assetUID;
  String heldBy;      // empty string = available
  int checkoutCount;  // total times ever checked out
};
AssetRecord assets[MAX_ASSETS];
int assetCount = 0;
const char* STATE_FILE = "/assets.json";

// ---------- cross-validation registry ----------
const int MAX_USERS = 50;
String registeredUsers[MAX_USERS];
int userCount = 0;

enum UIDType { TYPE_UNKNOWN, TYPE_USER, TYPE_ASSET };

// ---------- pending sync queue (Stage 4) ----------
const int MAX_PENDING = 50;
struct PendingTx {
  String user;
  String asset;
  String action;     // "checkout", "return", "conflict", "tamper"
  String timestamp;
};
PendingTx pending[MAX_PENDING];
int pendingCount = 0;
const char* PENDING_FILE = "/pending.json";

bool wifiConnected = false;

// ---------- Forward Declarations ----------
void connectWiFi();
void queueTransaction(String user, String asset, String action, String timestamp);
void attemptSync();
void saveStateToFlash();
void loadStateFromFlash();
void savePendingToFlash();
void loadPendingFromFlash();
void checkTamper();
int findAsset(String uid);
void addAsset(String uid, String heldBy);
String getTimestamp();
void showIdle();
void showCheckedOut(String assetUID);
void showReturned(String assetUID);
void showConflict(String assetUID, String heldBy);
void feedbackSuccess();
void feedbackReturn();
void feedbackConflict();
String getUID();
UIDType getUIDType(String uid);
void registerUser(String uid);
void showTypeError(const char* line1, const char* line2);
void wipeStorage();

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_AMBER, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(TAMPER_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Starting up...");

  if (!rtc.begin()) {
    Serial.println("RTC not found -- timestamps will be blank");
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    lcd.clear();
    lcd.print("Storage error!");
    delay(2000);
  } else {
    // RESET TRAP: Hold Tamper lever open/released on boot to format SPIFFS memory
    if (digitalRead(TAMPER_PIN) == HIGH) {
      lcd.clear();
      lcd.print("Wiping Flash...");
      wipeStorage();
      delay(2000);
    } else {
      loadStateFromFlash();
      loadPendingFromFlash();
    }
  }

  // OPTIONAL: Pre-register known User UIDs here so they are never confused with assets
  // registerUser("A1B2C3D4");
  // registerUser("E5F6G7H8");

  connectWiFi();
  showIdle();
}

// ================= LOOP =================

void loop() {
  checkTamper();
  attemptSync();

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = getUID();
  UIDType type = getUIDType(uid);

  if (currentState == WAIT_USER) {
    // REJECT: Card tapped is already known to be an Asset
    if (type == TYPE_ASSET) {
      Serial.println("Validation Error: Asset card tapped during WAIT_USER -> " + uid);
      showTypeError("Invalid ID!", "That's an Asset");
      showIdle();
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }

    // ACCEPT: Card is valid user or new unknown card
    userUID = uid;
    registerUser(userUID);
    Serial.println("User tapped: " + userUID);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hi! Now tap");
    lcd.setCursor(0, 1);
    lcd.print("the item");

    currentState = WAIT_ASSET;
  }
  else if (currentState == WAIT_ASSET) {
    // REJECT: Same user badge tapped twice in a row
    if (uid == userUID) {
      Serial.println("Validation Error: Same UID tapped twice -> " + uid);
      showTypeError("Same card!", "Tap asset card");
      showIdle();
      currentState = WAIT_USER;
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }

    // REJECT: Card tapped is a known User badge
    if (type == TYPE_USER) {
      Serial.println("Validation Error: User badge tapped during WAIT_ASSET -> " + uid);
      showTypeError("Invalid Item!", "That's a User ID");
      showIdle();
      currentState = WAIT_USER;
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }

    // ACCEPT: Valid Asset tap
    String assetUID = uid;
    Serial.println("Asset tapped: " + assetUID);

    int idx = findAsset(assetUID);
    String ts = getTimestamp();

    if (idx == -1) {
      addAsset(assetUID, userUID);
      showCheckedOut(assetUID);
      feedbackSuccess();
      saveStateToFlash();
      queueTransaction(userUID, assetUID, "checkout", ts);
    }
    else if (assets[idx].heldBy == "") {
      assets[idx].heldBy = userUID;
      assets[idx].checkoutCount++;
      showCheckedOut(assetUID);
      feedbackSuccess();
      saveStateToFlash();
      queueTransaction(userUID, assetUID, "checkout", ts);
    }
    else if (assets[idx].heldBy == userUID) {
      assets[idx].heldBy = "";
      showReturned(assetUID);
      feedbackReturn();
      saveStateToFlash();
      queueTransaction(userUID, assetUID, "return", ts);
    }
    else {
      showConflict(assetUID, assets[idx].heldBy);
      feedbackConflict();
      queueTransaction(userUID, assetUID, "conflict", ts);
    }

    delay(2500);
    currentState = WAIT_USER;
    showIdle();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(500);
}

// ================= RESET STORAGE HELPER =================

void wipeStorage() {
  if (SPIFFS.exists(STATE_FILE)) SPIFFS.remove(STATE_FILE);
  if (SPIFFS.exists(PENDING_FILE)) SPIFFS.remove(PENDING_FILE);
  assetCount = 0;
  pendingCount = 0;
  userCount = 0;
  Serial.println("Storage reset complete! All stored assets and queues deleted.");
}

// ================= CROSS-VALIDATION HELPERS =================

UIDType getUIDType(String uid) {
  // Check user registry first to ensure user badges override incorrect flash entries
  for (int i = 0; i < userCount; i++) {
    if (registeredUsers[i] == uid) return TYPE_USER;
  }

  if (findAsset(uid) != -1) return TYPE_ASSET;

  return TYPE_UNKNOWN;
}

void registerUser(String uid) {
  if (getUIDType(uid) == TYPE_UNKNOWN && userCount < MAX_USERS) {
    registeredUsers[userCount++] = uid;
  }
}

void showTypeError(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  digitalWrite(LED_RED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(300);
  digitalWrite(BUZZER, LOW);
  delay(1700);
  digitalWrite(LED_RED, LOW);
}

// ================= WIFI + SYNC (STAGE 4) =================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(300);
    Serial.print(".");
    attempts++;
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nNo WiFi -- running fully offline, will retry syncing later");
  }
}

void queueTransaction(String user, String asset, String action, String timestamp) {
  if (pendingCount >= MAX_PENDING) {
    Serial.println("Pending queue full -- oldest entry dropped");
    for (int i = 1; i < MAX_PENDING; i++) pending[i-1] = pending[i];
    pendingCount--;
  }
  pending[pendingCount].user = user;
  pending[pendingCount].asset = asset;
  pending[pendingCount].action = action;
  pending[pendingCount].timestamp = timestamp;
  pendingCount++;
  savePendingToFlash();
}

void attemptSync() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) Serial.println("WiFi lost -- back to offline mode");
    wifiConnected = false;
    return;
  }
  wifiConnected = true;

  if (pendingCount == 0) return;

  HTTPClient http;
  http.setTimeout(3000);
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["user"] = pending[0].user;
  doc["asset"] = pending[0].asset;
  doc["action"] = pending[0].action;
  doc["timestamp"] = pending[0].timestamp;
  String body;
  serializeJson(doc, body);

  int httpCode = http.POST(body);
  http.end();

  if (httpCode == 200 || httpCode == 201) {
    Serial.println("Synced 1 transaction, " + String(pendingCount - 1) + " remaining");
    for (int i = 1; i < pendingCount; i++) pending[i-1] = pending[i];
    pendingCount--;
    savePendingToFlash();
  } else {
    Serial.println("Sync failed (code " + String(httpCode) + "), will retry");
  }
}

// ================= PERSISTENCE (STAGE 3) =================

void saveStateToFlash() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < assetCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["uid"] = assets[i].assetUID;
    obj["heldBy"] = assets[i].heldBy;
    obj["count"] = assets[i].checkoutCount;
  }
  File f = SPIFFS.open(STATE_FILE, FILE_WRITE);
  if (!f) { Serial.println("Failed to open state file for writing"); return; }
  serializeJson(doc, f);
  f.close();
}

void loadStateFromFlash() {
  if (!SPIFFS.exists(STATE_FILE)) {
    Serial.println("No saved asset state -- starting fresh");
    return;
  }
  File f = SPIFFS.open(STATE_FILE, FILE_READ);
  if (!f) { Serial.println("Failed to open state file for reading"); return; }
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.println("Failed to parse saved state"); return; }
  JsonArray arr = doc.as<JsonArray>();
  assetCount = 0;
  for (JsonObject obj : arr) {
    if (assetCount >= MAX_ASSETS) break;
    assets[assetCount].assetUID = obj["uid"].as<String>();
    assets[assetCount].heldBy = obj["heldBy"].as<String>();
    assets[assetCount].checkoutCount = obj["count"] | 0;
    assetCount++;
  }
  Serial.println("Loaded " + String(assetCount) + " asset record(s) from flash");
}

void savePendingToFlash() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < pendingCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["user"] = pending[i].user;
    obj["asset"] = pending[i].asset;
    obj["action"] = pending[i].action;
    obj["timestamp"] = pending[i].timestamp;
  }
  File f = SPIFFS.open(PENDING_FILE, FILE_WRITE);
  if (!f) { Serial.println("Failed to open pending file for writing"); return; }
  serializeJson(doc, f);
  f.close();
}

void loadPendingFromFlash() {
  if (!SPIFFS.exists(PENDING_FILE)) {
    Serial.println("No pending transactions saved");
    return;
  }
  File f = SPIFFS.open(PENDING_FILE, FILE_READ);
  if (!f) { Serial.println("Failed to open pending file for reading"); return; }
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.println("Failed to parse pending queue"); return; }
  JsonArray arr = doc.as<JsonArray>();
  pendingCount = 0;
  for (JsonObject obj : arr) {
    if (pendingCount >= MAX_PENDING) break;
    pending[pendingCount].user = obj["user"].as<String>();
    pending[pendingCount].asset = obj["asset"].as<String>();
    pending[pendingCount].action = obj["action"].as<String>();
    pending[pendingCount].timestamp = obj["timestamp"].as<String>();
    pendingCount++;
  }
  Serial.println("Loaded " + String(pendingCount) + " unsynced transaction(s) from flash");
}

// ================= TAMPER DETECTION =================

bool lastTamperState = HIGH;
void checkTamper() {
  bool currentTamperState = digitalRead(TAMPER_PIN);
  if (currentTamperState != lastTamperState) {
    if (currentTamperState == HIGH) {
      String ts = getTimestamp();
      Serial.println("TAMPER EVENT: lid opened at " + ts);
      queueTransaction("", "", "tamper", ts);
    }
    lastTamperState = currentTamperState;
  }
}

// ================= ASSET HELPERS =================

int findAsset(String uid) {
  for (int i = 0; i < assetCount; i++) {
    if (assets[i].assetUID == uid) return i;
  }
  return -1;
}

void addAsset(String uid, String heldBy) {
  if (assetCount >= MAX_ASSETS) return;
  assets[assetCount].assetUID = uid;
  assets[assetCount].heldBy = heldBy;
  assets[assetCount].checkoutCount = 1;
  assetCount++;
}

String getTimestamp() {
  if (!rtc.begin()) return String(millis());
  DateTime now = rtc.now();
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  return String(buf);
}

// ================= LCD MESSAGES =================

void showIdle() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tap your ID");
  if (!wifiConnected) {
    lcd.setCursor(0, 1);
    lcd.print("(offline mode)");
  }
}

void showCheckedOut(String assetUID) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checked out!");
  lcd.setCursor(0, 1);
  lcd.print(assetUID.substring(0, 8));
}

void showReturned(String assetUID) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Returned!");
  lcd.setCursor(0, 1);
  lcd.print(assetUID.substring(0, 8));
}

void showConflict(String assetUID, String heldBy) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Already taken");
  lcd.setCursor(0, 1);
  lcd.print("by " + heldBy.substring(0, 8));
}

// ================= LED + BUZZER FEEDBACK =================

void feedbackSuccess() {
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);
  delay(2350);
  digitalWrite(LED_GREEN, LOW);
}

void feedbackReturn() {
  digitalWrite(LED_AMBER, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);
  delay(2350);
  digitalWrite(LED_AMBER, LOW);
}

void feedbackConflict() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(500);
  digitalWrite(BUZZER, LOW);
  delay(2000);
  digitalWrite(LED_RED, LOW);
}

String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}