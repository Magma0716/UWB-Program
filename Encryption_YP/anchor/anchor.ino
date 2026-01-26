/*

For ESP32 UWB or ESP32 UWB Pro

*/

#include <SPI.h>
#include "DW1000Ranging.h"

#define ANCHOR_ADD "AA:AA:AA:AA:AA:AA:AA:01"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

// connection pins
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

void setup()
{
    Serial.begin(115200);
    delay(1000);
    //init the configuration
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    // ============================================
    // ★★★ 實驗參數設定區 ★★★（不寫則使用預設值）
    // ============================================

    // 1) 加密開關 (true=開, false=關)
    //    預設：false（不加密）
    DW1000Ranging.setEncryptionFlag(true);

    // 2) 除錯模式 (true=印出 KEY / IV / TAG / CT 等資訊)
    //    預設：false（不輸出）
    DW1000Ranging.setEncryptionDebugFlag(true);

    // 3) 設定 COUNTER IV 起始值（只在 IV_MODE_COUNTER 有效）
    //    預設：0
    DW1000Ranging.setIVCounter(0);

    // 4) IV 模式：
    //    - IV_MODE_COUNTER：IV 前 4 bytes = counter，其餘 0（預設）
    //    - IV_MODE_RAND_UNIQUE：每次抽 12 bytes 亂數，並用表格檢查「本次 session 不重複」；切換到此模式會清空表
    DW1000Ranging.setIVMode(IV_MODE_RAND_UNIQUE);

    // 5) Padding 測試：在「距離字串」後面補 '0'（ASCII）增加 payload
    //    預設：0
    DW1000Ranging.setPaddingLength(16);

    // ============================================

    DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin
    //define the sketch as anchor. It will be great to dynamically change the type of module
    DW1000Ranging.attachNewRange(newRange);
    DW1000Ranging.attachBlinkDevice(newBlink);
    DW1000Ranging.attachInactiveDevice(inactiveDevice);
    //Enable the filter to smooth the distance
    //DW1000Ranging.useRangeFilter(true);

    //we start the module as an anchor
    // DW1000Ranging.startAsAnchor("82:17:5B:D5:A9:9A:E2:9C", DW1000.MODE_LONGDATA_RANGE_ACCURACY);

    DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
    // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_SHORTDATA_FAST_LOWPOWER);
    // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_FAST_LOWPOWER);
    // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_SHORTDATA_FAST_ACCURACY);
    // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_FAST_ACCURACY);
    // DW1000Ranging.startAsAnchor(ANCHOR_ADD, DW1000.MODE_LONGDATA_RANGE_ACCURACY);
}

void loop()
{
    DW1000Ranging.loop();
}

void newRange()
{
    // 輸出: from: {短地址}	 Range: {距離} m  RX power: {接收功率} dBm
    // 例如: from: A1B2	    Range: 3.01 m	 RX power: -45.00 dBm
    Serial.print("from: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
    Serial.print("\t Range: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getRange());
    Serial.print(" m");
    Serial.print("\t RX power: ");
    Serial.print(DW1000Ranging.getDistantDevice()->getRXPower());
    Serial.println(" dBm");
}

void newBlink(DW1000Device *device)
{
    Serial.print("blink; 1 device added ! -> ");
    Serial.print(" short:");
    Serial.println(device->getShortAddress(), HEX);
}

void inactiveDevice(DW1000Device *device)
{
    Serial.print("delete inactive device: ");
    Serial.println(device->getShortAddress(), HEX);
}
