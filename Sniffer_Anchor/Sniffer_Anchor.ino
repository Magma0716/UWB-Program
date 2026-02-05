#include <SPI.h>
#include <DW1000.h>

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

byte rxBuffer[1024];
volatile bool packetReady = false;   // ISR flag

void IRAM_ATTR handleReceived() {
  packetReady = true;
  // 只通知，不做任何 heavy 工作
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== DW1000 UWB Sniffer Mode (Safe ISR) ===");

  SPI.begin(18, 19, 23);

  DW1000.begin(PIN_IRQ, PIN_RST);
  DW1000.select(PIN_SS);

  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.commitConfiguration();

  DW1000.newReceive();
  DW1000.receivePermanently(true);

  DW1000.attachReceivedHandler(handleReceived);
  DW1000.startReceive();
}

void loop() {
  if (packetReady) {
    packetReady = false;

    uint16_t len = DW1000.getDataLength() + 2;
    if (len > 1024) len = 1024;

    DW1000.getData(rxBuffer, len);

    for (int i = 0; i < len; i++) {
      if (rxBuffer[i] < 16) Serial.print("0");
      Serial.print(rxBuffer[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    // 開始接收下一包
    DW1000.newReceive();
    DW1000.startReceive();
  }
}
