/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net> and Leopold Sayous <leosayous@gmail.com>
 * Decawave DW1000 library for arduino.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file DW1000Ranging.h
 * Arduino global library (header file) working with the DW1000 library
 * for the Decawave DW1000 UWB transceiver IC.
 *
 * @TODO
 * - remove or debugmode for Serial.print
 * - move strings to flash to reduce ram usage
 * - do not safe duplicate of pin settings
 * - maybe other object structure
 * - use enums instead of preprocessor constants
 */

// ===== [Add] Header guard：避免此標頭檔被重複 #include，造成重複宣告/編譯錯誤 =====
#ifndef _DW1000Ranging_H_INCLUDED
#define _DW1000Ranging_H_INCLUDED
// ========= [End Add] =========

#include "DW1000.h"
#include "DW1000Time.h"
#include "DW1000Device.h" 
#include "DW1000Mac.h"

// messages used in the ranging protocol
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
#define RANGE_FAILED 255
#define BLINK 4
#define RANGING_INIT 5

#define LEN_DATA 90

// ===== [Add] Encryption definitions =====
// IV 長度
#ifndef ENC_IV_LEN
#define ENC_IV_LEN 12
#endif
// Tag 長度
#ifndef ENC_TAG_LEN
#define ENC_TAG_LEN 16
#endif
// Encryption version
#ifndef ENC_VER
#define ENC_VER 0x01
#endif
// IV 產生模式
#ifndef IV_MODE_COUNTER
#define IV_MODE_COUNTER 0
#endif
// 唯一亂數 IV 模式
#ifndef IV_MODE_RAND_UNIQUE
#define IV_MODE_RAND_UNIQUE 1
#endif

// 唯一亂數 IV 的表容量（一次 session 內最多記錄幾個 IV）
// 4096 筆約佔：4096*12 + 4096*1 ≈ 53KB（ESP32 通常 OK）
// 太小會增加重複機率，太大會浪費 RAM
#ifndef IV_UNIQ_TABLE_SIZE
#define IV_UNIQ_TABLE_SIZE 4096
#endif
// ========= [End Add] =========

//Max devices we put in the networkDevices array ! Each DW1000Device is 74 Bytes in SRAM memory for now.
// ===== [Update] Increase MAX_DEVICES =====
// 原版：MAX_DEVICES = 4
// 意義：_networkDevices[] 可同時管理的裝置上限（TAG/ANCHOR 數量上限）
// 調大可同時管理更多裝置，代價是 SRAM 增加
#define MAX_DEVICES 7
// ========= [End Update] =========

//Default Pin for module:
#define DEFAULT_RST_PIN 9
#define DEFAULT_SPI_SS_PIN 10

//Default value
//in ms
#define DEFAULT_RESET_PERIOD 200
//in us
#define DEFAULT_REPLY_DELAY_TIME 7000

//sketch type (anchor or tag)
#define TAG 0
#define ANCHOR 1

//default timer delay
#define DEFAULT_TIMER_DELAY 80

//debug mode
#ifndef DEBUG
#define DEBUG false
#endif


class DW1000RangingClass {
public:
	//variables
	// data buffer
	static byte data[LEN_DATA];
	
	//initialisation
	static void    initCommunication(uint8_t myRST = DEFAULT_RST_PIN, uint8_t mySS = DEFAULT_SPI_SS_PIN, uint8_t myIRQ = 2);
	static void    configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]);
	static void    generalStart();
	static void    startAsAnchor(char address[], const byte mode[], const bool randomShortAddress = true);
	static void    startAsTag(char address[], const byte mode[], const bool randomShortAddress = true);
	static boolean addNetworkDevices(DW1000Device* device, boolean shortAddress);
	static boolean addNetworkDevices(DW1000Device* device);
	static void    removeNetworkDevices(int16_t index);
	
	//setters
	static void setReplyTime(uint16_t replyDelayTimeUs);
	static void setResetPeriod(uint32_t resetPeriod);
	
	// ===== [Add] Encryption functions ===== 
	// (可以不呼叫設定函式，會使用預設值)
	// 1. 加密開關 
	void setEncryptionFlag(boolean enable);      // 預設 false
	// 2. 負載：設定 Padding 長度
	void setPaddingLength(uint8_t nBytes);       // 預設 0
	// 3. 設定 IV 產生模式 (mode: IV_MODE_COUNTER / IV_MODE_RAND_UNIQUE)
	void setIVMode(uint8_t mode);                // 預設 IV_MODE_COUNTER
	// 3-1. 設定初始 IV 計數器值 (only for IV_MODE_COUNTER)
	void setIVCounter(uint32_t start);           // 預設 0
	// 4. 除錯模式開關 (logging：印出 key、IV、nonce 等資訊)
	void setEncryptionDebugFlag(boolean enable); // 預設 false
	// ========= [End Add] =========

	//getters
	static byte* getCurrentAddress() { return _currentAddress; };
	
	static byte* getCurrentShortAddress() { return _currentShortAddress; };
	
	static uint8_t getNetworkDevicesNumber() { return _networkDevicesNumber; };
	
	//ranging functions
	static int16_t detectMessageType(byte datas[]); // TODO check return type
	static void loop();
	static void useRangeFilter(boolean enabled);
	// Used for the smoothing algorithm (Exponential Moving Average). newValue must be >= 2. Default 15.
	static void setRangeFilterValue(uint16_t newValue);
	
	//Handlers:
	static void attachNewRange(void (* handleNewRange)(void)) { _handleNewRange = handleNewRange; };
	
	static void attachBlinkDevice(void (* handleBlinkDevice)(DW1000Device*)) { _handleBlinkDevice = handleBlinkDevice; };
	
	static void attachNewDevice(void (* handleNewDevice)(DW1000Device*)) { _handleNewDevice = handleNewDevice; };
	
	static void attachInactiveDevice(void (* handleInactiveDevice)(DW1000Device*)) { _handleInactiveDevice = handleInactiveDevice; };
	
	
	
	static DW1000Device* getDistantDevice();
	static DW1000Device* searchDistantDevice(byte shortAddress[]);
	
	//FOR DEBUGGING
	static void visualizeDatas(byte datas[]);


private:
	//other devices in the network
	static DW1000Device _networkDevices[MAX_DEVICES];
	static volatile uint8_t _networkDevicesNumber;
	static int16_t      _lastDistantDevice;
	static byte         _currentAddress[8];
	static byte         _currentShortAddress[2];
	static byte         _lastSentToShortAddress[2];
	static DW1000Mac    _globalMac;
	static int32_t      timer;
	static int16_t      counterForBlink;
	
	//Handlers:
	static void (* _handleNewRange)(void);
	static void (* _handleBlinkDevice)(DW1000Device*);
	static void (* _handleNewDevice)(DW1000Device*);
	static void (* _handleInactiveDevice)(DW1000Device*);
	
	//sketch type (tag or anchor)
	static int16_t          _type; //0 for tag and 1 for anchor
	// TODO check type, maybe enum?
	// message flow state
	static volatile byte    _expectedMsgId;
	// message sent/received state
	static volatile boolean _sentAck;
	static volatile boolean _receivedAck;
	// protocol error state
	static boolean          _protocolFailed;

	// ===== [Add] Encryption state =====
	// (可以不呼叫設定函式，會使用預設值)
	// 1. 是否啟用加密
	static boolean  _isEncryptionEnabled;        // 預設 false
	// 2. 負載：Padding 長度
	static uint8_t  _paddingLength;              // 預設 0
	// 3. IV 產生模式 (mode: IV_MODE_COUNTER / IV_MODE_RAND_UNIQUE)
	static uint8_t  _ivMode;                     // 預設 IV_MODE_COUNTER
	// 3-1. 初始 IV 計數器值 (only for IV_MODE_COUNTER)
	static uint32_t _expIVCounter;               // 預設 0
	// 4. 除錯模式開關 (logging：印出 key、IV、nonce 等資訊)
	static boolean  _isEncryptionDebugEnabled;   // 預設 false
	// ========= [End Add] =========

	// reset line to the chip
	static uint8_t     _RST;
	static uint8_t     _SS;
	// watchdog and reset period
	static uint32_t    _lastActivity;
	static uint32_t    _resetPeriod;
	// reply times (same on both sides for symm. ranging)
	static uint16_t     _replyDelayTimeUS;
	//timer Tick delay
	static uint16_t     _timerDelay;
	// ranging counter (per second)
	static uint16_t     _successRangingCount;
	static uint32_t    _rangingCountPeriod;
	//ranging filter
	static volatile boolean _useRangeFilter;
	static uint16_t         _rangeFilterValue;
	//_bias correction
	static char  _bias_RSL[17]; // TODO remove or use
	//17*2=34 bytes in SRAM
	static int16_t _bias_PRF_16[17]; // TODO remove or use
	//17 bytes in SRAM
	static char  _bias_PRF_64[17]; // TODO remove or use
	
	
	//methods
	static void handleSent();
	static void handleReceived();
	static void noteActivity();
	static void resetInactive();
	
	//global functions:
	static void checkForReset();
	static void checkForInactiveDevices();
	static void copyShortAddress(byte address1[], byte address2[]);
	
	//for ranging protocole (ANCHOR)
	static void transmitInit();
	static void transmit(byte datas[]);
	static void transmit(byte datas[], DW1000Time time);
	static void transmitBlink();
	static void transmitRangingInit(DW1000Device* myDistantDevice);
	static void transmitPollAck(DW1000Device* myDistantDevice);
	static void transmitRangeReport(DW1000Device* myDistantDevice);
	static void transmitRangeFailed(DW1000Device* myDistantDevice);
	static void receiver();
	
	//for ranging protocole (TAG)
	static void transmitPoll(DW1000Device* myDistantDevice);
	static void transmitRange(DW1000Device* myDistantDevice);
	
	//methods for range computation
	static void computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF);
	
	static void timerTick();
	
	//Utils
	static float filterValue(float value, float previousValue, uint16_t numberOfElements);
};

extern DW1000RangingClass DW1000Ranging;

#endif
