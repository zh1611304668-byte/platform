import os

def replace_in_file(path):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Headers
    content = content.replace("#include <BLEDevice.h>", "#include <NimBLEDevice.h>")
    content = content.replace("#include <BLEAdvertisedDevice.h>\n", "")
    content = content.replace("#include <BLEClient.h>\n", "")
    content = content.replace("#include <BLEScan.h>\n", "")
    content = content.replace("#include <BLEUtils.h>\n", "")
    content = content.replace("#include <BLESecurity.h>\n", "")

    # Types
    replaces = [
        ("BLEDevice", "NimBLEDevice"),
        ("BLEClient", "NimBLEClient"),
        ("BLEScan", "NimBLEScan"),
        ("BLEUUID", "NimBLEUUID"),
        ("BLERemoteCharacteristic", "NimBLERemoteCharacteristic"),
        ("BLERemoteService", "NimBLERemoteService"),
        ("BLESecurityCallbacks", "NimBLESecurityCallbacks"),
        ("BLEClientCallbacks", "NimBLEClientCallbacks"),
        ("BLERemoteDescriptor", "NimBLERemoteDescriptor"),
        ("BLEAdvertisedDeviceCallbacks", "NimBLEAdvertisedDeviceCallbacks"),
        ("BLEAdvertisedDevice", "NimBLEAdvertisedDevice"),
        ("BLEAddress", "NimBLEAddress")
    ]
    for old, new in replaces:
        content = content.replace(old, new)

    # Security replacements in BluetoothManager.cpp
    if "BluetoothManager.cpp" in path:
        content = content.replace(
            "esp_ble_auth_cmpl_t cmpl", 
            "ble_gap_conn_desc* desc"
        )
        content = content.replace(
            "cmpl.success", 
            "desc->sec_state.encrypted"
        )
        content = content.replace(
            "static NimBLESecurity   s_bleSecInstance;\n",
            ""
        )
        content = content.replace(
            "NimBLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);",
            "NimBLEDevice::setSecurityAuth(true, true, true);"
        )
        content = content.replace(
            "s_bleSecInstance.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);",
            ""
        )
        content = content.replace(
            "s_bleSecInstance.setCapability(ESP_IO_CAP_NONE);",
            "NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);"
        )
        
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

replace_in_file("src/BluetoothManager.h")
replace_in_file("src/BluetoothManager.cpp")
