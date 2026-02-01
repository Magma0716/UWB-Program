// currently tag is module #5
// The purpose of this code is to set the tag address and antenna delay to default.
// this tag will be used for calibrating the anchors.

#include <SPI.h>
#include "DW1000Ranging.h"
#include "DW1000.h"
#include <WiFiUdp.h>
#include <WiFi.h>
#include "ㄋ.h"

#define SPI_SCK 18
#define SPI_MISO 19
#define SPI_MOSI 23
#define DW_CS 4

// connection pins
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

// TAG antenna delay defaults to 16384
// leftmost two bytes below will become the "short address"
char tag_addr[] = "7D:00:22:EA:82:60:3B:9C";

// WiFi Setting
/*
const char *ssid     = "Alan6711";
const char *password = "bbb520111";
const char *host     = "192.168.0.108";
*/
const char *ssid = "PSS";
const char *password = "047611137";
const char *host = "192.168.2.109";

const int port = 8001;
WiFiUDP udp;

struct MyLink *uwb_data;
long runtime = 0;
String all_json = "";

void setup()
{
  Serial.begin(115200);
  
  // connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
      delay(500);
      Serial.print(".");
  }
  Serial.println("Connected");
  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());
  
  delay(1000);

  // 除錯模式 (印出 KEY / IV / TAG / CT 等資訊)
  DW1000Ranging.setEncryptionDebugFlag(false);

  //init the configuration
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ); //Reset, CS, IRQ pin

  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);

  // start as tag, do not assign random short address
  DW1000Ranging.startAsTag(tag_addr, DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);

  uwb_data = init_link();
}

void loop()
{
  DW1000Ranging.loop();
  if ((millis() - runtime) > 100)
    {
        make_link_json(uwb_data, &all_json);
        send_udp(&all_json);
        runtime = millis();
    }
}

float lastTime = 0;

void newRange()
{
  // calculate time difference
  float nowTime = millis();
  float timeDiff = 0;
  if(lastTime != 0) timeDiff = nowTime - lastTime;
  lastTime = nowTime;

  // variable
  auto ShortAddress = DW1000Ranging.getDistantDevice()->getShortAddress();
  auto Range = DW1000Ranging.getDistantDevice()->getRange();
  auto RXPower = DW1000Ranging.getDistantDevice()->getRXPower();

  Serial.print(timeDiff);
  Serial.print(",");
  Serial.print(Range);
  Serial.print(",");
  Serial.print(timeDiff,6);
  Serial.print(",");
  Serial.println(0);

  fresh_link(uwb_data, ShortAddress, Range, RXPower);
}

void newDevice(DW1000Device *device)
{
  Serial.print("Device added: ");
  Serial.println(device->getShortAddress(), HEX);

  add_link(uwb_data, device->getShortAddress());
}

void inactiveDevice(DW1000Device *device)
{
  Serial.print("delete inactive device: ");
  Serial.println(device->getShortAddress(), HEX);

  delete_link(uwb_data, device->getShortAddress());
}

void send_udp(String *msg_json)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skip send");
        return;
    }

    int len = msg_json->length();
    if (len == 0) {
        Serial.println("Empty JSON, skip");
        return;
    }

    // start packet to host:port
    udp.beginPacket(host, port);
    udp.print(*msg_json);
    udp.endPacket();
    //Serial.println(*msg_json);
}