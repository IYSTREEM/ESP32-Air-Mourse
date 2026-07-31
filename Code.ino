// This Sketch is Made for the Esp32 C3 Super mini 
// Github Of The Project: https://github.com/IYSTREEM/ESP32-Air-Mourse
// MadeBy IYSTREEM

#include <Wire.h>
#include <Preferences.h>

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEHIDDevice.h>
#include <NimBLECharacteristic.h>

struct Btn;

// ---------------- Pins ----------------
const uint8_t LEFT_BTN   = 5;
const uint8_t RIGHT_BTN  = 6;
const uint8_t MIDDLE_BTN = 20;
const uint8_t SCROLL_UP  = 8;
const uint8_t SCROLL_DN  = 9;
const uint8_t I2C_SDA = 4;
const uint8_t I2C_SCL = 3;


const uint8_t MPU_ADDR      = 0x68;
const uint8_t REG_PWR_MGMT1 = 0x6B;
const uint8_t REG_GYRO_CFG  = 0x1B;
const uint8_t GYRO_YOUT_H   = 0x45;
const uint8_t GYRO_ZOUT_H   = 0x47;

bool initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT1);
  Wire.write(0x00); 
  if (Wire.endTransmission(true) != 0) return false;
  delay(100);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_GYRO_CFG);
  Wire.write(0x00); 
  if (Wire.endTransmission(true) != 0) return false;
  delay(50);

  return true;
}


bool readGyroYZ(int16_t* gy, int16_t* gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(GYRO_YOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(MPU_ADDR, (uint8_t)4, (uint8_t)true) != 4) return false;

  *gy = (Wire.read() << 8) | Wire.read();
  *gz = (Wire.read() << 8) | Wire.read();
  return true;
}


float horizontalDivisor = 5.0f;
float verticalDivisor   = 3.0f;
float gyroDeadzone      = 0.5f; 
float smoothFactor      = 0.3f;  
bool  invertX           = false;
bool  invertY           = false;

const unsigned long DEBOUNCE_MS      = 15;
const unsigned long SCROLL_REPEAT_MS = 80;

float gyroYoffset = 0, gyroZoffset = 0;
float smoothVx = 0, smoothVy = 0;


struct Btn {
  uint8_t pin;
  bool stableState;
  bool rawState;
  unsigned long lastChangeTime;
};

Btn leftBtn     = {LEFT_BTN,   false, false, 0};
Btn rightBtn    = {RIGHT_BTN,  false, false, 0};
Btn middleBtn   = {MIDDLE_BTN, false, false, 0};
Btn scrollUpBtn = {SCROLL_UP,  false, false, 0};
Btn scrollDnBtn = {SCROLL_DN,  false, false, 0};

unsigned long lastScrollUpTime = 0;
unsigned long lastScrollDnTime = 0;

uint8_t currentButtonMask = 0;
const uint8_t BTN_LEFT_BIT   = 0x01;
const uint8_t BTN_RIGHT_BIT  = 0x02;
const uint8_t BTN_MIDDLE_BIT = 0x04;

bool updateButton(Btn &btn) {
  bool reading = (digitalRead(btn.pin) == LOW); 

  if (reading != btn.rawState) {
    btn.rawState = reading;
    btn.lastChangeTime = millis();
  }

  if ((millis() - btn.lastChangeTime) > DEBOUNCE_MS) {
    if (btn.stableState != btn.rawState) {
      btn.stableState = btn.rawState;
      return true; // changed
    }
  }
  return false;
}


static const uint8_t hidReportDescriptor[] = {
  0x05, 0x01,        
  0x09, 0x02,       
  0xA1, 0x01,       
  0x09, 0x01,        
  0xA1, 0x00,        
  0x05, 0x09,        
  0x19, 0x01,     
  0x29, 0x05,     
  0x15, 0x00,   
  0x25, 0x01,    
  0x95, 0x05,       
  0x75, 0x01,        
  0x81, 0x02,       
  0x95, 0x01,        
  0x75, 0x03,       
  0x81, 0x01,        
  0x05, 0x01,        
  0x09, 0x30,       
  0x09, 0x31,        
  0x09, 0x38,        
  0x15, 0x81,
  0x25, 0x7F,        
  0x75, 0x08,        
  0x95, 0x03,        
  0x81, 0x06,        
  0xC0,             
  0xC0              
};

NimBLEServer*         pServer      = nullptr;
NimBLEHIDDevice*      pHid         = nullptr;
NimBLECharacteristic* inputMouse   = nullptr;
NimBLEAdvertising*    pAdvertising = nullptr;

volatile bool deviceConnected = false;

class MouseServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
    Serial.println("BLE central connected, requesting security...");
    NimBLEDevice::startSecurity(connInfo.getConnHandle());
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (connInfo.isEncrypted()) {
      Serial.println("Link encrypted — mouse is live.");
      deviceConnected = true;
    } else {
      Serial.println("Encryption failed, disconnecting.");
      NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
    }
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.println("BLE central disconnected, restarting advertising.");
    pAdvertising->start();
  }
};



void setupBle() {
  NimBLEDevice::init("ESP32 Air Mouse");

  NimBLEDevice::setSecurityAuth(true, false, true);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MouseServerCallbacks());

  pHid = new NimBLEHIDDevice(pServer);

  pHid->setManufacturer("DIY");
  pHid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  pHid->setHidInfo(0x00, 0x01);
  pHid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

  inputMouse = pHid->getInputReport(0); 

  pHid->setBatteryLevel(100);
  pHid->startServices();

  pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAppearance(0x03C2); // HID Mouse appearance
  pAdvertising->addServiceUUID(pHid->getHidService()->getUUID());
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

  Serial.println("BLE mouse advertising. Pair it from your computer/phone Bluetooth settings.");
}

void sendMouseReport(int8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!deviceConnected || inputMouse == nullptr) return;

  uint8_t report[4] = {
    (uint8_t)buttons,
    (uint8_t)dx,
    (uint8_t)dy,
    (uint8_t)wheel
  };

  inputMouse->setValue(report, sizeof(report));
  inputMouse->notify();
}

Preferences prefs;


bool calibrateGyro(unsigned long timeoutMs = 6000) {
  const int required = 250;
  const float stillThresholdDegS = 3.0f; 

  int count = 0;
  double sumY = 0, sumZ = 0;
  int16_t prevGY = 0, prevGZ = 0;
  bool havePrev = false;
  unsigned long start = millis();

  while (count < required) {
    if (millis() - start > timeoutMs) break;

    int16_t rawGY, rawGZ;
    if (!readGyroYZ(&rawGY, &rawGZ)) { delay(3); continue; }

    if (havePrev) {
      float dGY = fabs((rawGY - prevGY) / 131.0f);
      float dGZ = fabs((rawGZ - prevGZ) / 131.0f);
      if (dGY > stillThresholdDegS || dGZ > stillThresholdDegS) {
        count = 0;
        sumY = 0;
        sumZ = 0;
      }
    }
    prevGY = rawGY;
    prevGZ = rawGZ;
    havePrev = true;

    sumY += rawGY / 131.0;
    sumZ += rawGZ / 131.0;
    count++;
    delay(3);
  }

  if (count < 30) return false; 
  gyroYoffset = sumY / count;
  gyroZoffset = sumZ / count;
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(LEFT_BTN, INPUT_PULLUP);
  pinMode(RIGHT_BTN, INPUT_PULLUP);
  pinMode(MIDDLE_BTN, INPUT_PULLUP);
  pinMode(SCROLL_UP, INPUT_PULLUP);
  pinMode(SCROLL_DN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!initMPU()) {
    Serial.println("MPU6050 not found. Check wiring (SDA=GPIO4, SCL=GPIO3).");
    while (1) delay(10);
  }

  prefs.begin("airmouse", false);
  bool haveSavedCal = prefs.getBool("calibrated", false);
  bool forceRecal = (digitalRead(MIDDLE_BTN) == LOW); 

  if (haveSavedCal && !forceRecal) {
    gyroYoffset = prefs.getFloat("gyroY", 0);
    gyroZoffset = prefs.getFloat("gyroZ", 0);
    Serial.println("Loaded saved gyro calibration.");
  } else {
    Serial.println("Calibrating gyro, keep the device still...");
    if (calibrateGyro()) {
      prefs.putFloat("gyroY", gyroYoffset);
      prefs.putFloat("gyroZ", gyroZoffset);
      prefs.putBool("calibrated", true);
      Serial.println("Calibration done and saved. Won't be needed again unless you hold the middle button at boot.");
    } else {
      Serial.println("Calibration timed out without a stable reading — using zero offset for now.");
    }
  }
  prefs.end();

  setupBle();
}

void handleClickButtons() {
  bool changed = false;

  if (updateButton(leftBtn)) {
    if (leftBtn.stableState) currentButtonMask |= BTN_LEFT_BIT;
    else                     currentButtonMask &= ~BTN_LEFT_BIT;
    changed = true;
  }

  if (updateButton(rightBtn)) {
    if (rightBtn.stableState) currentButtonMask |= BTN_RIGHT_BIT;
    else                      currentButtonMask &= ~BTN_RIGHT_BIT;
    changed = true;
  }

  if (updateButton(middleBtn)) {
    if (middleBtn.stableState) currentButtonMask |= BTN_MIDDLE_BIT;
    else                       currentButtonMask &= ~BTN_MIDDLE_BIT;
    changed = true;
  }

  if (changed) {
    sendMouseReport(currentButtonMask, 0, 0, 0);
  }
}

void handleScrollButtons() {
  updateButton(scrollUpBtn);
  updateButton(scrollDnBtn);

  unsigned long now = millis();

  if (scrollUpBtn.stableState && (now - lastScrollUpTime > SCROLL_REPEAT_MS)) {
    sendMouseReport(currentButtonMask, 0, 0, 1);
    lastScrollUpTime = now;
  }

  if (scrollDnBtn.stableState && (now - lastScrollDnTime > SCROLL_REPEAT_MS)) {
    sendMouseReport(currentButtonMask, 0, 0, -1);
    lastScrollDnTime = now;
  }
}

void handleMovement() {
  int16_t rawGY, rawGZ;
  if (!readGyroYZ(&rawGY, &rawGZ)) return;

  float gy = (rawGY / 131.0f) - gyroYoffset; 
  float gz = (rawGZ / 131.0f) - gyroZoffset; 

  float vx = -gz / horizontalDivisor * (invertX ? -1 : 1);
  float vy = -gy / verticalDivisor   * (invertY ? -1 : 1);

  if (fabs(vx) < gyroDeadzone) vx = 0;
  if (fabs(vy) < gyroDeadzone) vy = 0;

  smoothVx = (smoothVx * (1 - smoothFactor)) + (vx * smoothFactor);
  smoothVy = (smoothVy * (1 - smoothFactor)) + (vy * smoothFactor);

  if (fabs(smoothVx) > 0.1f || fabs(smoothVy) > 0.1f) {
    int8_t dx = (int8_t)constrain(smoothVx, -127.0f, 127.0f);
    int8_t dy = (int8_t)constrain(smoothVy, -127.0f, 127.0f);
    sendMouseReport(currentButtonMask, dx, dy, 0);
  }
}

void loop() {
  if (!deviceConnected) {
    delay(50);
    return;
  }

  handleMovement();
  handleClickButtons();
  handleScrollButtons();

  delay(8);
}


