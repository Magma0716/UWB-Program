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

    // 參數設定
    DW1000Ranging.setEncryptionFlag(true); // 加密

    DW1000Ranging.setEncryptionDebugFlag(false); // 印出 (KEY / IV / TAG / CT)

    DW1000Ranging.setIVCounter(0); // Counter IV 起始值

    DW1000Ranging.setIVMode(IV_MODE_RAND_UNIQUE); // IV_MODE_COUNTER (4byte + 0), IV_MODE_RAND_UNIQUE (12byte亂數)

    DW1000Ranging.setPaddingLength(44); // padding

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

unsigned long lastTime = 0;

void newRange()
{
    unsigned long now = micros();
    unsigned long timeDiff = 0;

    // 計算距離上一次收到數據經過了多久 (Latency / 負載指標)
    if (lastTime != 0) {
        timeDiff = now - lastTime;
    }
    lastTime = now;

    // 取得距離數據
    float dist = DW1000Ranging.getDistantDevice()->getRange();

    // 格式化輸出: "DATA,毫秒,公尺"
    // 例如: DATA,105,3.01
    Serial.print("DATA,");
    Serial.print(timeDiff);
    Serial.print(",");
    Serial.println(dist);
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
