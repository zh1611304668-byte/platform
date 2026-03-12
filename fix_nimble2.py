import os

path = "src/BluetoothManager.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# Fix NimNim
content = content.replace("NimNim", "Nim")

# Fix NimBLEAdvertisedDeviceCallbacks -> NimBLEScanCallbacks
content = content.replace("public NimBLEAdvertisedDeviceCallbacks", "public NimBLEScanCallbacks")
# Fix onResult signature
content = content.replace("void onResult(NimBLEAdvertisedDevice advertisedDevice)", "void onResult(NimBLEAdvertisedDevice* advertisedDevice)")
# Fix advertisedDevice. -> advertisedDevice->
content = content.replace("advertisedDevice.haveName()", "advertisedDevice->haveName()")
content = content.replace("advertisedDevice.getAddress()", "advertisedDevice->getAddress()")
content = content.replace("advertisedDevice.getAddressType()", "advertisedDevice->getAddressType()")

# Remove MySecurity totally
sec_class = """class MySecurity : public NimBLESecurityCallbacks {
  uint32_t onPassKeyRequest() { return 0; }
  void onPassKeyNotify(uint32_t pass_key) {}
  bool onConfirmPIN(uint32_t pin) { return true; }
  bool onSecurityRequest() { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) {
    if (desc->sec_state.encrypted)
      Serial.println("[SEC] Pairing success");
    else
      Serial.println("[SEC] Pairing failed");
  }
};"""
content = content.replace(sec_class, "")

sec_class_old = """class MySecurity : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 123456; }
  void onPassKeyNotify(uint32_t pass_key) override {}
  bool onConfirmPIN(uint32_t pass_key) override { return true; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    if (cmpl.success) Serial.println("[BT] Security bonded");
  }
};"""
content = content.replace(sec_class_old, "")

# Fix references to s_secInstance
content = content.replace("static MySecurity    s_secInstance;", "")
content = content.replace("NimBLEDevice::setSecurityCallbacks(&s_secInstance);", "NimBLEDevice::setSecurityAuth(true, true, true);\n  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);")

# Fix registerForNotify -> subscribe
content = content.replace("ch->registerForNotify(notifyCallback);", "ch->subscribe(true, notifyCallback, false);")
content = content.replace("bCh->registerForNotify(batteryNotifyCallback);", "bCh->subscribe(true, batteryNotifyCallback, false);")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
