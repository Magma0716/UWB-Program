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
 * Arduino global library (source file) working with the DW1000 library 
 * for the Decawave DW1000 UWB transceiver IC.
 *
 * @TODO
 * - remove or debugmode for Serial.print
 * - move strings to flash to reduce ram usage
 * - do not safe duplicate of pin settings
 * - maybe other object structure
 * - use enums instead of preprocessor constants
 */


#include "DW1000Ranging.h"
#include "DW1000Device.h"

// # ====== 新增 (Add) ======
// 初始化實驗變數 (預設全關，保持原始 Library 行為)
boolean  DW1000RangingClass::_isEncryptionEnabled = false; 
uint16_t DW1000RangingClass::_expPaddingLen = 0;
uint32_t DW1000RangingClass::_expIVCounter = 0;
// 記住「同一組 Poll/Final 的 CRC 兩個 bytes」供 Range_Report 使用
static byte g_group_crc[2] = {0x00, 0x00};


// 實作設定函式
void DW1000RangingClass::setEncryptionFlag(boolean enable) {
    _isEncryptionEnabled = enable;
    Serial.print("[Setup] Encryption: "); Serial.println(enable ? "ON" : "OFF");
}

void DW1000RangingClass::setPaddingLength(uint16_t len) {
    _expPaddingLen = len;
    Serial.print("[Setup] Padding Len: "); Serial.println(len);
}

void DW1000RangingClass::setIVCounter(uint32_t startVal) {
    _expIVCounter = startVal;
}
// # ========================

// # ====== 新增 (Add) ======
// 說明：為了除錯方便，新增印出 16 進位數據的輔助函式
// 用途：在 Serial Monitor 顯示 IV, Tag, Ciphertext 進行比對
void printHex(const char* label, const unsigned char* data, int len) {
    Serial.print(label);
    for(int i=0; i<len; i++) {
        if(data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}
// # ========================
// ===== [Add] Encryption includes =====
// ESP32 才有用到 mbedTLS AES-GCM；非 ESP32 直接略過以保持可編譯
#if defined(ARDUINO_ARCH_ESP32)
#include "mbedtls/gcm.h"     // AES-GCM      
#include "mbedtls/cipher.h"  // cipher helper
#endif

// 32 bytes = 256-bit AES key（示範用 key，可自行更換）
// static：只在本 .cpp 可見，避免 multiple definition
static const uint8_t UWB_AES_KEY[32] = {
  0x00,0x01,0x02,0x03, 0x04,0x05,0x06,0x07,
  0x08,0x09,0x0A,0x0B, 0x0C,0x0D,0x0E,0x0F,
  0x10,0x11,0x12,0x13, 0x14,0x15,0x16,0x17,
  0x18,0x19,0x1A,0x1B, 0x1C,0x1D,0x1E,0x1F
};

// 除錯工具：印出 bytes 的 HEX（用於檢查 key/IV/nonce/tag/密文）
static bool _encDbgKeyPrinted = false;
static void dumpHex(const char* label, const uint8_t* p, size_t n) {
  Serial.print(label);
  for (size_t i = 0; i < n; i++) {
    if (p[i] < 16) Serial.print("0");
    Serial.print(p[i], HEX);
    if (i + 1 < n) Serial.print(" ");
  }
  Serial.println();
}
// ========= [End Add] =========


// ===== [Add] Unique-random IV tracker (ESP32 only) =====
// AES-GCM 要求：同一把 key 下 IV(Nonce) 不可重複
// 功能：IV_MODE_RAND_UNIQUE 產生 12-byte 隨機 IV，並用表格查重（僅保證「本次開機/session」）
#if defined(ARDUINO_ARCH_ESP32)
#include "esp_system.h" // for esp_random()

// RAM 注意：IV_UNIQ_TABLE_SIZE=4096 時約 52KB（iv + flag）
static uint8_t  _iv_used[IV_UNIQ_TABLE_SIZE][ENC_IV_LEN];
static uint8_t  _iv_used_flag[IV_UNIQ_TABLE_SIZE];
static uint32_t _iv_used_count = 0;

// FNV-1a：用於把 IV 映射到 table index（用途是查重，不是保密）
static inline uint32_t fnv1a32_iv(const uint8_t* iv, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint32_t)iv[i];
    h *= 16777619u;
  }
  return h;
}

// 清空表：重新開始一輪量測/模式切換時可呼叫
static void iv_table_clear() {
  memset(_iv_used_flag, 0, sizeof(_iv_used_flag));
  _iv_used_count = 0;
}

// true=新 IV 已登記；false=重複或表滿（表滿後無法再保證不重複）
static bool iv_table_insert_if_new(const uint8_t iv[ENC_IV_LEN]) {
  if (_iv_used_count >= IV_UNIQ_TABLE_SIZE) {
    return false; // 表滿：沒辦法再保證「全程不重複」
  }

  uint32_t h = fnv1a32_iv(iv, ENC_IV_LEN);
  uint32_t idx = h % IV_UNIQ_TABLE_SIZE;

  for (uint32_t step = 0; step < IV_UNIQ_TABLE_SIZE; step++) {
    uint32_t j = (idx + step) % IV_UNIQ_TABLE_SIZE;

    if (_iv_used_flag[j] == 0) {
      memcpy(_iv_used[j], iv, ENC_IV_LEN);
      _iv_used_flag[j] = 1;
      _iv_used_count++;
      return true;
    }

    // 位置已用：比對是否同一個 IV（真的檢查 12 bytes）
    if (memcmp(_iv_used[j], iv, ENC_IV_LEN) == 0) {
      return false; // 重複了
    }
  }

  return false; // 理論上不該走到這（表滿/探測完）
}

// 生成唯一亂數 IV：成功回 true；失敗回 false（極端：表滿或碰撞過多）
static bool gen_unique_random_iv(uint8_t outIV[ENC_IV_LEN]) {
  // 最多嘗試 100,000 次（理論上不該超過幾十次就成功）
  for (int tries = 0; tries < 100000; tries++) {
    uint32_t r0 = esp_random();
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    memcpy(outIV + 0, &r0, 4);
    memcpy(outIV + 4, &r1, 4);
    memcpy(outIV + 8, &r2, 4);

    if (iv_table_insert_if_new(outIV)) {
      return true;
    }
  }
  return false; 
}
#endif
// ========= [End Add] =========

DW1000RangingClass DW1000Ranging;


//other devices we are going to communicate with which are on our network:
DW1000Device DW1000RangingClass::_networkDevices[MAX_DEVICES];
byte         DW1000RangingClass::_currentAddress[8];
byte         DW1000RangingClass::_currentShortAddress[2];
byte         DW1000RangingClass::_lastSentToShortAddress[2];
volatile uint8_t DW1000RangingClass::_networkDevicesNumber = 0; // TODO short, 8bit?
int16_t      DW1000RangingClass::_lastDistantDevice    = 0; // TODO short, 8bit?
DW1000Mac    DW1000RangingClass::_globalMac;

//module type (anchor or tag)
int16_t      DW1000RangingClass::_type; // TODO enum??

// message flow state
volatile byte    DW1000RangingClass::_expectedMsgId;

// range filter
volatile boolean DW1000RangingClass::_useRangeFilter = false;
uint16_t DW1000RangingClass::_rangeFilterValue = 15;

// message sent/received state
volatile boolean DW1000RangingClass::_sentAck     = false;
volatile boolean DW1000RangingClass::_receivedAck = false;

// protocol error state
boolean          DW1000RangingClass::_protocolFailed = false;

// ===== [Add] Encryption state =====
// (可以不呼叫設定函式，會使用預設值)
// 1. 是否啟用加密
boolean  DW1000RangingClass::_isEncryptionEnabled = false;        // 預設 false
// 2. 負載：Padding 長度
uint8_t  DW1000RangingClass::_paddingLength = 0;                  // 預設 0
// 3. IV 產生模式 (mode: IV_MODE_COUNTER / IV_MODE_RAND_UNIQUE)
uint8_t  DW1000RangingClass::_ivMode = IV_MODE_COUNTER;           // 預設 counter
// 3-1. 初始 IV 計數器值 (only for IV_MODE_COUNTER)
uint32_t DW1000RangingClass::_expIVCounter = 0;                   // 預設 0
// 4. 除錯模式開關 (logging：印出 key、IV、nonce 等資訊)
boolean  DW1000RangingClass::_isEncryptionDebugEnabled = false;   // 預設 false
// ======== [End Add] =========

// timestamps to remember
int32_t            DW1000RangingClass::timer           = 0;
int16_t            DW1000RangingClass::counterForBlink = 0; // TODO 8 bit?


// data buffer
byte          DW1000RangingClass::data[LEN_DATA];
// reset line to the chip
uint8_t   DW1000RangingClass::_RST;
uint8_t   DW1000RangingClass::_SS;
// watchdog and reset period
uint32_t  DW1000RangingClass::_lastActivity;
uint32_t  DW1000RangingClass::_resetPeriod;
// reply times (same on both sides for symm. ranging)
uint16_t  DW1000RangingClass::_replyDelayTimeUS;
//timer delay
uint16_t  DW1000RangingClass::_timerDelay;
// ranging counter (per second)
uint16_t  DW1000RangingClass::_successRangingCount = 0;
uint32_t  DW1000RangingClass::_rangingCountPeriod  = 0;
//Here our handlers
void (* DW1000RangingClass::_handleNewRange)(void) = 0;
void (* DW1000RangingClass::_handleBlinkDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleNewDevice)(DW1000Device*) = 0;
void (* DW1000RangingClass::_handleInactiveDevice)(DW1000Device*) = 0;

// # ====== 新增 (Add) ======
// 說明：定義 AES-GCM 加密所需的參數與密鑰
// 注意：Anchor 與 Tag 的 UWB_AES_KEY 必須完全一致
const unsigned char UWB_AES_KEY[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

#define ENC_IV_LEN 12   // GCM 初始向量長度 (標準 12 bytes)
#define ENC_TAG_LEN 16  // GCM 驗證標籤長度 (標準 16 bytes)
#define ENC_VER 0x01    // 自定義協議版本號
// # ========================

/* ###########################################################################
 * #### Init and end #######################################################
 * ######################################################################### */

void DW1000RangingClass::initCommunication(uint8_t myRST, uint8_t mySS, uint8_t myIRQ) {
	// reset line to the chip
	_RST              = myRST;
	_SS               = mySS;
	_resetPeriod      = DEFAULT_RESET_PERIOD;
	// reply times (same on both sides for symm. ranging)
	_replyDelayTimeUS = DEFAULT_REPLY_DELAY_TIME;
	//we set our timer delay
	_timerDelay       = DEFAULT_TIMER_DELAY;
	
	
	DW1000.begin(myIRQ, myRST);
	DW1000.select(mySS);
}


void DW1000RangingClass::configureNetwork(uint16_t deviceAddress, uint16_t networkId, const byte mode[]) {
	// general configuration
	DW1000.newConfiguration();
	DW1000.setDefaults();
	DW1000.setDeviceAddress(deviceAddress);
	DW1000.setNetworkId(networkId);
	DW1000.enableMode(mode);
	DW1000.commitConfiguration();
	
}

void DW1000RangingClass::generalStart() {
	// attach callback for (successfully) sent and received messages
	DW1000.attachSentHandler(handleSent);
	DW1000.attachReceivedHandler(handleReceived);
	// anchor starts in receiving mode, awaiting a ranging poll message
	
	
	if(DEBUG) {
		// DEBUG monitoring
		Serial.println("DW1000-arduino");
		// initialize the driver
		
		
		Serial.println("configuration..");
		// DEBUG chip info and registers pretty printed
		char msg[90];
		DW1000.getPrintableDeviceIdentifier(msg);
		Serial.print("Device ID: ");
		Serial.println(msg);
		DW1000.getPrintableExtendedUniqueIdentifier(msg);
		Serial.print("Unique ID: ");
		Serial.print(msg);
		char string[6];
		sprintf(string, "%02X:%02X", _currentShortAddress[0], _currentShortAddress[1]);
		Serial.print(" short: ");
		Serial.println(string);
		
		DW1000.getPrintableNetworkIdAndShortAddress(msg);
		Serial.print("Network ID & Device Address: ");
		Serial.println(msg);
		DW1000.getPrintableDeviceMode(msg);
		Serial.print("Device mode: ");
		Serial.println(msg);
	}
	
	// Vincent changes
	//DW1000.large_power_init();
	
	// anchor starts in receiving mode, awaiting a ranging poll message
	receiver();
	// for first time ranging frequency computation
	_rangingCountPeriod = millis();
}


void DW1000RangingClass::startAsAnchor(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print("device address: ");
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);
	
	//general start:
	generalStart();
	
	//defined type as anchor
	_type = ANCHOR;
	
	Serial.println("### ANCHOR ###");
	
}

void DW1000RangingClass::startAsTag(char address[], const byte mode[], const bool randomShortAddress) {
	//save the address
	DW1000.convertToByte(address, _currentAddress);
	//write the address on the DW1000 chip
	DW1000.setEUI(address);
	Serial.print("device address: ");
	Serial.println(address);
	if (randomShortAddress) {
		//we need to define a random short address:
		randomSeed(analogRead(0));
		_currentShortAddress[0] = random(0, 256);
		_currentShortAddress[1] = random(0, 256);
	}
	else {
		// we use first two bytes in addess for short address
		_currentShortAddress[0] = _currentAddress[0];
		_currentShortAddress[1] = _currentAddress[1];
	}
	
	//we configur the network for mac filtering
	//(device Address, network ID, frequency)
	DW1000Ranging.configureNetwork(_currentShortAddress[0]*256+_currentShortAddress[1], 0xDECA, mode);
	
	generalStart();
	//defined type as tag
	_type = TAG;
	
	Serial.println("### TAG ###");
}

boolean DW1000RangingClass::addNetworkDevices(DW1000Device* device, boolean shortAddress) {
	boolean   addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && !shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		else if(_networkDevices[i].isShortAddressEqual(device) && shortAddress) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		device->setRange(0);
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

boolean DW1000RangingClass::addNetworkDevices(DW1000Device* device) {
	boolean addDevice = true;
	//we test our network devices array to check
	//we don't already have it
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isAddressEqual(device) && _networkDevices[i].isShortAddressEqual(device)) {
			//the device already exists
			addDevice = false;
			return false;
		}
		
	}
	
	if(addDevice) {
		if(_type == ANCHOR) //for now let's start with 1 TAG
		{
			_networkDevicesNumber = 0;
		}
		memcpy(&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));
		_networkDevices[_networkDevicesNumber].setIndex(_networkDevicesNumber);
		_networkDevicesNumber++;
		return true;
	}
	
	return false;
}

void DW1000RangingClass::removeNetworkDevices(int16_t index) {
	//if we have just 1 element
	if(_networkDevicesNumber == 1) {
		_networkDevicesNumber = 0;
	}
	else if(index == _networkDevicesNumber-1) //if we delete the last element
	{
		_networkDevicesNumber--;
	}
	else {
		//we translate all the element wich are after the one we want to delete.
		for(int16_t i = index; i < _networkDevicesNumber-1; i++) { // TODO 8bit?
			memcpy(&_networkDevices[i], &_networkDevices[i+1], sizeof(DW1000Device));
			_networkDevices[i].setIndex(i);
		}
		_networkDevicesNumber--;
	}
}

/* ###########################################################################
 * #### Setters and Getters ##################################################
 * ######################################################################### */

//setters
void DW1000RangingClass::setReplyTime(uint16_t replyDelayTimeUs) { _replyDelayTimeUS = replyDelayTimeUs; }

void DW1000RangingClass::setResetPeriod(uint32_t resetPeriod) { _resetPeriod = resetPeriod; }

// ===== [Add] Encryption functions =====
// (可以不呼叫設定函式，會使用預設值)
// 1. 加密開關
void DW1000RangingClass::setEncryptionFlag(boolean enable) {
  _isEncryptionEnabled = enable;
}
// 2. 負載：設定 Padding 長度
void DW1000RangingClass::setPaddingLength(uint8_t nBytes) {
  _paddingLength = nBytes;
}
// 3. 設定 IV 產生模式 (mode: IV_MODE_COUNTER / IV_MODE_RAND_UNIQUE)
void DW1000RangingClass::setIVMode(uint8_t mode) {
  _ivMode = mode;

#if defined(ARDUINO_ARCH_ESP32)
  if (_ivMode == IV_MODE_RAND_UNIQUE) {
    iv_table_clear(); // 重新開始記錄：本次 session 保證不重複
  }
#endif

  if (_isEncryptionDebugEnabled) {
    Serial.print("[ENC] setIVMode = ");
    Serial.println((_ivMode == IV_MODE_COUNTER) ? "COUNTER" : "RAND_UNIQUE");
#if defined(ARDUINO_ARCH_ESP32)
    if (_ivMode == IV_MODE_RAND_UNIQUE) {
      Serial.print("[ENC] IV_UNIQ_TABLE_SIZE = ");
      Serial.println(IV_UNIQ_TABLE_SIZE);
    }
#endif
  }
}
// 3-1. 設定初始 IV 計數器值 (only for IV_MODE_COUNTER)
void DW1000RangingClass::setIVCounter(uint32_t start) {
  // 只在 counter 模式下生效
  if (_ivMode == IV_MODE_COUNTER) {
    _expIVCounter = start;
  } else {
    // 非 counter 模式：忽略
    if (_isEncryptionDebugEnabled) {
      Serial.println("[ENC][WARN] setIVCounter() ignored because IV mode is not COUNTER.");
    }
  }
}
// 4. 除錯模式開關 (logging：印出 key、IV、nonce 等資訊)
void DW1000RangingClass::setEncryptionDebugFlag(boolean enable) {
  _isEncryptionDebugEnabled = enable;
  if (!enable) {
    _encDbgKeyPrinted = false; // 關掉後重開，允許再印一次 key
  }
}
// ========= [End Add] =========

DW1000Device* DW1000RangingClass::searchDistantDevice(byte shortAddress[]) {
	//we compare the 2 bytes address with the others
	for(uint16_t i = 0; i < _networkDevicesNumber; i++) { // TODO 8bit?
		if(memcmp(shortAddress, _networkDevices[i].getByteShortAddress(), 2) == 0) {
			//we have found our device !
			return &_networkDevices[i];
		}
	}
	
	return nullptr;
}

DW1000Device* DW1000RangingClass::getDistantDevice() {
	//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
	
	return &_networkDevices[_lastDistantDevice];
	
}


/* ###########################################################################
 * #### Public methods #######################################################
 * ######################################################################### */

void DW1000RangingClass::checkForReset() {
	uint32_t curMillis = millis();
	if(!_sentAck && !_receivedAck) {
		// check if inactive
		if(curMillis-_lastActivity > _resetPeriod) {
			resetInactive();
		}
		return; // TODO cc
	}
}

void DW1000RangingClass::checkForInactiveDevices() {
	for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
		if(_networkDevices[i].isInactive()) {
			if(_handleInactiveDevice != 0) {
				(*_handleInactiveDevice)(&_networkDevices[i]);
			}
			//we need to delete the device from the array:
			removeNetworkDevices(i);
			
		}
	}
}

// TODO check return type
int16_t DW1000RangingClass::detectMessageType(byte datas[]) {
	if(datas[0] == FC_1_BLINK) {
		return BLINK;
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2) {
		//we have a long MAC frame message (ranging init)
		return datas[LONG_MAC_LEN];
	}
	else if(datas[0] == FC_1 && datas[1] == FC_2_SHORT) {
		//we have a short mac frame message (poll, range, range report, etc..)
		return datas[SHORT_MAC_LEN];
	}
}

void DW1000RangingClass::loop() {
	// 檢查是否超時/異常，需要重置 DW1000 狀態機
	checkForReset();
	uint32_t time = millis(); // TODO other name - too close to "timer"
	if(time-timer > _timerDelay) {    // 週期性 tick（用來做 timeout / inactive device 檢查等）
		timer = time;
		timerTick();
	}
	
	// (A) TX 完成事件：剛剛有封包送出
	if(_sentAck) {
		_sentAck = false;  // 清除旗標：避免重複處理同一次 TX
		
		// TODO cc
		int messageType = detectMessageType(data);  // 從 data buffer 解析 msgid（POLL/POLL_ACK/RANGE...）
		
		// 只處理本 ranging 流程會用到的 msgid，其他直接忽略
		if(messageType != POLL_ACK && messageType != POLL && messageType != RANGE)
			return;
		
		//A msg was sent. We launch the ranging protocole when a message was sent
		if(_type == ANCHOR) {
			if(messageType == POLL_ACK) {
				// ANCHOR：送出 POLL_ACK 後，記下「送出的時間戳」供後續 TOF 計算用
				DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
				
				if (myDistantDevice) {
					DW1000.getTransmitTimestamp(myDistantDevice->timePollAckSent);
				}
			}
		}
		else if(_type == TAG) {
			if(messageType == POLL) {
				// TAG：送出 POLL 後，記下「送出的時間戳」
				DW1000Time timePollSent;
				DW1000.getTransmitTimestamp(timePollSent);

				// 若上次是 broadcast（0xFFFF），代表一次對多個 device 發 POLL：每個 device 都要記同一個 timePollSent
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timePollSent = timePollSent;
					}
				}
				else {
					// 非 broadcast：只更新「那一台」對應的 device
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					if (myDistantDevice) {
						myDistantDevice->timePollSent = timePollSent;
					}
				}
			}
			else if(messageType == RANGE) {
				// TAG：送出 RANGE 後，記下「送出的時間戳」
				DW1000Time timeRangeSent;
				DW1000.getTransmitTimestamp(timeRangeSent);

				// 同樣要區分 broadcast vs 單一 device
				if(_lastSentToShortAddress[0] == 0xFF && _lastSentToShortAddress[1] == 0xFF) {
					for(uint16_t i = 0; i < _networkDevicesNumber; i++) {
						_networkDevices[i].timeRangeSent = timeRangeSent;
					}
				}
				else {
					// 非 broadcast：只更新「那一台」對應的 device
					DW1000Device* myDistantDevice = searchDistantDevice(_lastSentToShortAddress);
					if (myDistantDevice) {
						myDistantDevice->timeRangeSent = timeRangeSent;
					}
				}
				
			}
		}
		
	}
	
	// (B) RX 完成事件：剛剛有封包收到
	if(_receivedAck) {
		_receivedAck = false;  // 清除旗標：避免重複處理同一次 RX
		
		//we read the datas from the modules:
		// get message and parse
		DW1000.getData(data, LEN_DATA); // 把 DW1000 RX buffer 讀到 data[]（注意：LEN_DATA 是上限）
		
		int messageType = detectMessageType(data);
		if (_type == ANCHOR && (messageType == POLL || messageType == RANGE)) {
    		uint16_t rxLen = DW1000.getDataLength();
    		if (rxLen >= 2) {
        		g_group_crc[0] = data[rxLen - 2];
        		g_group_crc[1] = data[rxLen - 1];
    	}
	}	

		//we have just received a BLINK message from tag
		if(messageType == BLINK && _type == ANCHOR) {
			byte address[8];
			byte shortAddress[2];
			_globalMac.decodeBlinkFrame(data, address, shortAddress); // 解出對方的 long/short address
			//we crate a new device with th tag
			DW1000Device myTag(address, shortAddress); // 建立 TAG device 物件（暫時在 stack）
			
			if(addNetworkDevices(&myTag)) {            // 加入 networkDevices（會 memcpy 進陣列）
				if(_handleBlinkDevice != 0) {
					(*_handleBlinkDevice)(&myTag);     // callback：通知使用者「有 tag 來了」
				}
				transmitRangingInit(&myTag);           // 回覆 RANGING_INIT，開始建連線/流程
				noteActivity();                        // 更新 watchdog/activity（避免被當作 inactive）
			}
			_expectedMsgId = POLL;                     // 下一步期待收到 POLL
		}

		// (B2) TAG 收到 RANGING_INIT：anchor 回覆了
		else if(messageType == RANGING_INIT && _type == TAG) {
			
			byte address[2];
			_globalMac.decodeLongMACFrame(data, address);  // 解出對方 short address

			//we crate a new device with the anchor
			DW1000Device myAnchor(address, true);         // true 表示「用 short address」初始化
			
			if(addNetworkDevices(&myAnchor, true)) {      // 加入 device list（以 short address 判斷重複）
				if(_handleNewDevice != 0) {
					(*_handleNewDevice)(&myAnchor);       // callback：通知使用者「新增 anchor」
				}
			}
			noteActivity();
		}

		// (B3) 其他：一般 short-MAC frame（POLL / RANGE / POLL_ACK / RANGE_REPORT...）
		else {
			byte address[2];
			_globalMac.decodeShortMACFrame(data, address); // 取出送方 short address（用來找對應 device）
			
			//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
			DW1000Device* myDistantDevice = searchDistantDevice(address); // 找 device 物件
			
			// 若 device list 空或找不到，代表尚未建立/記錄對方 short addr
			if((_networkDevicesNumber == 0) || (myDistantDevice == nullptr)) {
				if (DEBUG) {
					Serial.println("Not found");
					/*
					Serial.print("unknown: ");
					Serial.print(address[0], HEX);
					Serial.print(":");
					Serial.println(address[1], HEX);
					*/
				}
				return;
			}
			
			//then we proceed to range protocole
			// (C) ANCHOR 狀態機：等 POLL -> 回 POLL_ACK -> 等 RANGE -> 回 RANGE_REPORT
			if(_type == ANCHOR) {

				// 若收到的 msgid 不符合預期，視為 protocol 失敗（但不立刻終止，後面會用 _protocolFailed 決定回報）
				if(messageType != _expectedMsgId) {
					// unexpected message, start over again (except if already POLL)
					_protocolFailed = true;
				}
				if(messageType == POLL) {
					// POLL 是 broadcast：裡面帶「多台 anchor 的 replyTime 表」
					int16_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1); 
					
					for(uint16_t i = 0; i < numberDevices; i++) {
						// 每筆：shortAddress(2) + replyTime(2) => stride = 4
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*4, 2);
						
						// 如果表格中的 shortAddress 是「自己」，就拿出對應 replyTime 來設定回覆延遲
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {

							uint16_t replyTime;
							memcpy(&replyTime, data+SHORT_MAC_LEN+2+i*4+2, 2);
							_replyDelayTimeUS = replyTime;    // ANCHOR 依照 TAG 指定的 replyTime 回覆
							
							_protocolFailed = false;          // 收到 POLL 視為重新開始流程：清掉 fail
							
							DW1000.getReceiveTimestamp(myDistantDevice->timePollReceived);  // 記下 POLL RX timestamp
							myDistantDevice->noteActivity();                                // 更新此 device 的活躍狀態

							_expectedMsgId = RANGE;           // 下一步要等 RANGE
							transmitPollAck(myDistantDevice); // 回 POLL_ACK（包含我的回覆時間點會被記錄）
							noteActivity();

							return;  // 已處理完本封包
						}
						
					}
					
					
				}
				else if(messageType == RANGE) {
					// RANGE 也是 broadcast：裡面對每個 anchor 有一段 17 bytes 的資料
					uint8_t numberDevices = 0;
					memcpy(&numberDevices, data+SHORT_MAC_LEN+1, 1);
					
					
					for(uint8_t i = 0; i < numberDevices; i++) {
						// 每筆 stride = 17，前 2 bytes 是 shortAddress
						byte shortAddress[2];
						memcpy(shortAddress, data+SHORT_MAC_LEN+2+i*17, 2);
						
						// 找到是「自己」的 shortAddress
						if(shortAddress[0] == _currentShortAddress[0] && shortAddress[1] == _currentShortAddress[1]) {

							DW1000.getReceiveTimestamp(myDistantDevice->timeRangeReceived);
							noteActivity();
							_expectedMsgId = POLL; // RANGE 處理完後下一輪回到等 POLL
							
							if(!_protocolFailed) {
								// 從 RANGE payload 取回 TAG 填的三個 timestamp（POLL sent / POLL_ACK recv / RANGE sent）
								myDistantDevice->timePollSent.setTimestamp(data+SHORT_MAC_LEN+4+17*i);
								myDistantDevice->timePollAckReceived.setTimestamp(data+SHORT_MAC_LEN+9+17*i);
								myDistantDevice->timeRangeSent.setTimestamp(data+SHORT_MAC_LEN+14+17*i);
								
								// (re-)compute range as two-way ranging is done
								DW1000Time myTOF;
								computeRangeAsymmetric(myDistantDevice, &myTOF); // CHOSEN RANGING ALGORITHM（非對稱 TW-TWR）
								
								float distance = myTOF.getAsMeters();            // TOF -> meters

								// range filter：用上一筆距離做簡單濾波（略過第一筆）
								if (_useRangeFilter) {
									//Skip first range
									if (myDistantDevice->getRange() != 0.0f) {
										distance = filterValue(distance, myDistantDevice->getRange(), _rangeFilterValue);
									}
								}

								// 量測品質：RX power / FP power / quality 都在 ANCHOR 端此刻讀取
								myDistantDevice->setRXPower(DW1000.getReceivePower());
								myDistantDevice->setRange(distance);
								
								myDistantDevice->setFPPower(DW1000.getFirstPathPower());
								myDistantDevice->setQuality(DW1000.getReceiveQuality());
								
								// 回 RANGE_REPORT 給 TAG（包含距離等資訊）
								transmitRangeReport(myDistantDevice);
								
								_lastDistantDevice = myDistantDevice->getIndex();
								if(_handleNewRange != 0) {
									(*_handleNewRange)(); // callback：通知上層「有新距離」
								}
							}
							else {
								// 若流程失敗：回報 RANGE_FAILED（讓 TAG 知道此輪失敗）
								transmitRangeFailed(myDistantDevice);
							}
							
							return;
						}
						
					}
					
					
				}
			}


			// (D) TAG 狀態機：送 POLL -> 收 POLL_ACK(多台) -> 送 RANGE(broadcast) -> 收 RANGE_REPORT(多台)
			else if(_type == TAG) {
				// 若 msgid 不符預期：原作者這裡直接 return（等下一輪）
				if(messageType != _expectedMsgId) {
					// unexpected message, start over again
					//not needed ?
					return;
					_expectedMsgId = POLL_ACK;
					return;
				}
				if(messageType == POLL_ACK) {
					// 收到某一台 anchor 的 POLL_ACK：記 RX timestamp
					DW1000.getReceiveTimestamp(myDistantDevice->timePollAckReceived);
					myDistantDevice->noteActivity();
					
					// 若已收到最後一台（以 index 判斷）：開始送 RANGE(broadcast)
					if(myDistantDevice->getIndex() == _networkDevicesNumber-1) {
						_expectedMsgId = RANGE_REPORT;
						transmitRange(nullptr); // broadcast RANGE 給所有 anchor
					}
				}

				/* # ====== 原本的邏輯 (Original) ======
                // 原始版本直接從封包固定位置讀取 float

				else if(messageType == RANGE_REPORT) {

					float curRange;
					memcpy(&curRange, data+1+SHORT_MAC_LEN, 4);
					float curRXPower;
					memcpy(&curRXPower, data+5+SHORT_MAC_LEN, 4);

					if (_useRangeFilter) {
						//Skip first range
						if (myDistantDevice->getRange() != 0.0f) {
							curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
						}
					}

					//we have a new range to save !
					myDistantDevice->setRange(curRange);
					myDistantDevice->setRXPower(curRXPower);
					
					
					//We can call our handler !
					//we have finished our range computation. We send the corresponding handler
					_lastDistantDevice = myDistantDevice->getIndex();
					if(_handleNewRange != 0) {
						(*_handleNewRange)();
					}
				}
				*/

				// # ====== 改動 (Modify) ======
                // 說明：替換為 AES-GCM 解密與驗證流程
                // 流程：解析 Header -> 取得 IV/Tag -> 計算密文長度 -> 解密 -> 還原數據
				else if(messageType == RANGE_REPORT) {
                    // # ====== 新增 (Add) ======
                    // 解析 Header，判斷是加密 (0x01) 還是明文 (0x00)
					int header_size = SHORT_MAC_LEN + 1; 
					int offset = header_size;
					
					byte modeVer = data[offset++]; // 讀取版本號
					
					float curRange = 0.0f;
                    float curRXPower = -100.0f; // 簡化處理
					boolean validReport = false;

					// ====== 分支 1: 加密模式 (Ver == 0x01) ======
					if (modeVer == ENC_VER) {
						// 解析 IV, Tag
						unsigned char iv[ENC_IV_LEN];
						memcpy(iv, &data[offset], ENC_IV_LEN); offset += ENC_IV_LEN;
						
						unsigned char tag[ENC_TAG_LEN];
						memcpy(tag, &data[offset], ENC_TAG_LEN); offset += ENC_TAG_LEN;

						int dataLen = DW1000.getDataLength();
						int cipher_len = dataLen - offset;

						if (cipher_len > 0) {
							unsigned char decrypted[128];
							memset(decrypted, 0, sizeof(decrypted)); 

							mbedtls_gcm_context gcm;
							mbedtls_gcm_init(&gcm);
							mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, UWB_AES_KEY, 256);

							int ret = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, 
															iv, ENC_IV_LEN, 
															NULL, 0, 
															tag, ENC_TAG_LEN, 
															&data[offset], decrypted); 
							mbedtls_gcm_free(&gcm);

							if (ret == 0) {
                                // # ====== 驗證 (Verify) ======
								// 解密成功，String().toFloat() 會自動忽略後面的 padding 0
								curRange = String((char*)decrypted).toFloat();
								validReport = true;
								
                                // # ====== 除錯 (Debug) ======
								// uint32_t recvCnt;
								// memcpy(&recvCnt, iv, 4);
								// Serial.print("[Tag] Decrypt OK. Count:"); Serial.println(recvCnt);
							} else {
                                // # ====== 除錯 (Debug) ======
								Serial.print("[Tag] Decrypt Fail! Err: -0x");
                                Serial.println(-ret, HEX);
							}
						}
					}
					// ====== 分支 2: 明文模式 (Ver == 0x00) ======
					else if (modeVer == 0x00) {
						int dataLen = DW1000.getDataLength();
						int plain_len = dataLen - offset;
						char plainBuf[64];
						
						if(plain_len > 0 && plain_len < 64) {
							memcpy(plainBuf, &data[offset], plain_len);
							plainBuf[plain_len] = '\0'; // 補結尾
							curRange = String(plainBuf).toFloat();
							validReport = true;
                            // Serial.println("[Tag] Received Plaintext.");
						}
					}

					// 更新距離資訊 (共用邏輯)
					if (validReport) {
						if (_useRangeFilter && myDistantDevice->getRange() != 0.0f) {
							curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
						}
						myDistantDevice->setRange(curRange);
						myDistantDevice->setRXPower(curRXPower);
						
						_lastDistantDevice = myDistantDevice->getIndex();
						if(_handleNewRange != 0) {
							(*_handleNewRange)();
						}
					}
					// ================= [解密修改結束] =================
				}
							
				else if(messageType == RANGE_FAILED) {
					//not needed as we have a timer;
					return;
					_expectedMsgId = POLL_ACK;
				}
			}
		}
		
	}
}

void DW1000RangingClass::useRangeFilter(boolean enabled) {
	_useRangeFilter = enabled;
}

void DW1000RangingClass::setRangeFilterValue(uint16_t newValue) {
	if (newValue < 2) {
		_rangeFilterValue = 2;
	}else{
		_rangeFilterValue = newValue;
	}
}


/* ###########################################################################
 * #### Private methods and Handlers for transmit & Receive reply ############
 * ######################################################################### */


void DW1000RangingClass::handleSent() {
	// status change on sent success
	_sentAck = true;
}

void DW1000RangingClass::handleReceived() {
	// status change on received success
	_receivedAck = true;
}


void DW1000RangingClass::noteActivity() {
	// update activity timestamp, so that we do not reach "resetPeriod"
	_lastActivity = millis();
}

void DW1000RangingClass::resetInactive() {
	//if inactive
	if(_type == ANCHOR) {
		_expectedMsgId = POLL;
		receiver();
	}
	noteActivity();
}

void DW1000RangingClass::timerTick() {
	if(_networkDevicesNumber > 0 && counterForBlink != 0) {
		if(_type == TAG) {
			_expectedMsgId = POLL_ACK;
			//send a prodcast poll
			transmitPoll(nullptr);
		}
	}
	else if(counterForBlink == 0) {
		if(_type == TAG) {
			transmitBlink();
		}
		//check for inactive devices if we are a TAG or ANCHOR
		checkForInactiveDevices();
	}
	counterForBlink++;
	if(counterForBlink > 20) {
		counterForBlink = 0;
	}
}


void DW1000RangingClass::copyShortAddress(byte address1[], byte address2[]) {
	*address1     = *address2;
	*(address1+1) = *(address2+1);
}

/* ###########################################################################
 * #### Methods for ranging protocole   ######################################
 * ######################################################################### */

void DW1000RangingClass::transmitInit() {
	DW1000.newTransmit();
	DW1000.setDefaults();
}


void DW1000RangingClass::transmit(byte datas[]) {
	DW1000.setData(datas, LEN_DATA);
	DW1000.startTransmit();
}


void DW1000RangingClass::transmit(byte datas[], DW1000Time time) {
	DW1000.setDelay(time);
	DW1000.setData(data, LEN_DATA);
	DW1000.startTransmit();
}

void DW1000RangingClass::transmitBlink() {
	transmitInit();
	_globalMac.generateBlinkFrame(data, _currentAddress, _currentShortAddress);
	transmit(data);
}

void DW1000RangingClass::transmitRangingInit(DW1000Device* myDistantDevice) {
	transmitInit();

	//we generate the mac frame for a ranging init message
	_globalMac.generateLongMACFrame(data, _currentShortAddress, myDistantDevice->getByteAddress());
	//we define the function code
	data[LONG_MAC_LEN] = RANGING_INIT;
	
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	
	transmit(data);
}

void DW1000RangingClass::transmitPoll(DW1000Device* myDistantDevice) {
	
	transmitInit();
	
	if(myDistantDevice == nullptr) {
		//we need to set our timerDelay:
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);
		
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = POLL;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;
		//11
		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
			//each devices have a different reply delay time.
			_networkDevices[i].setReplyTime((2*i+1)*DEFAULT_REPLY_DELAY_TIME);
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+4*i, _networkDevices[i].getByteShortAddress(), 2);
			
			//we add the replyTime
			uint16_t replyTime = _networkDevices[i].getReplyTime();
			memcpy(data+SHORT_MAC_LEN+2+2+4*i, &replyTime, 2);
			
		}
		
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
	}
	else {
		//we redefine our default_timer_delay for just 1 device;
		_timerDelay = DEFAULT_TIMER_DELAY;
		
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		
		data[SHORT_MAC_LEN]   = POLL;
		data[SHORT_MAC_LEN+1] = 1;
		uint16_t replyTime = myDistantDevice->getReplyTime();
		memcpy(data+SHORT_MAC_LEN+2, &replyTime, sizeof(uint16_t)); // todo is code correct?
		
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	}
	
	transmit(data);
}


void DW1000RangingClass::transmitPollAck(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = POLL_ACK;
	// delay the same amount as ranging tag
	DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data, deltaTime);
}

void DW1000RangingClass::transmitRange(DW1000Device* myDistantDevice) {
	//transmit range need to accept broadcast for multiple anchor
	transmitInit();
	
	
	if(myDistantDevice == nullptr) {
		//we need to set our timerDelay:
		_timerDelay = DEFAULT_TIMER_DELAY+(uint16_t)(_networkDevicesNumber*3*DEFAULT_REPLY_DELAY_TIME/1000);
		
		byte shortBroadcast[2] = {0xFF, 0xFF};
		_globalMac.generateShortMACFrame(data, _currentShortAddress, shortBroadcast);
		data[SHORT_MAC_LEN]   = RANGE;
		//we enter the number of devices
		data[SHORT_MAC_LEN+1] = _networkDevicesNumber;
		
		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime     = DW1000Time(DEFAULT_REPLY_DELAY_TIME, DW1000Time::MICROSECONDS);
		DW1000Time timeRangeSent = DW1000.setDelay(deltaTime);
		
		for(uint8_t i = 0; i < _networkDevicesNumber; i++) {
			//we write the short address of our device:
			memcpy(data+SHORT_MAC_LEN+2+17*i, _networkDevices[i].getByteShortAddress(), 2);
			
			
			//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
			_networkDevices[i].timeRangeSent = timeRangeSent;
			_networkDevices[i].timePollSent.getTimestamp(data+SHORT_MAC_LEN+2 +2+17*i);
			_networkDevices[i].timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+2+2 +5+17*i);
			_networkDevices[i].timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+2+2+5 +5+17*i);
			
		}
		
		copyShortAddress(_lastSentToShortAddress, shortBroadcast);
		
	}
	else {
		_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
		data[SHORT_MAC_LEN] = RANGE;
		// delay sending the message and remember expected future sent timestamp
		DW1000Time deltaTime = DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS);
		//we get the device which correspond to the message which was sent (need to be filtered by MAC address)
		myDistantDevice->timeRangeSent = DW1000.setDelay(deltaTime);
		myDistantDevice->timePollSent.getTimestamp(data+1+SHORT_MAC_LEN);
		myDistantDevice->timePollAckReceived.getTimestamp(data+6+SHORT_MAC_LEN);
		myDistantDevice->timeRangeSent.getTimestamp(data+11+SHORT_MAC_LEN);
		copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	}
	
	
	transmit(data);
}

// # ====== 改動 (Modify) ======
// 說明：Anchor 端發送加密報告
// 流程：float轉字串 -> 生成IV -> AES-GCM加密 -> 打包 [Ver|IV|Tag|Ciphertext]

// void DW1000RangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
// 	transmitInit();
// 	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
// 	data[SHORT_MAC_LEN] = RANGE_REPORT;
// 	// write final ranging result
// 	float curRange   = myDistantDevice->getRange();
// 	float curRXPower = myDistantDevice->getRXPower();
// 	//We add the Range and then the RXPower
// 	memcpy(data+1+SHORT_MAC_LEN, &curRange, 4);
// 	memcpy(data+5+SHORT_MAC_LEN, &curRXPower, 4);
// 	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
// 	transmit(data, DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));
// }

void DW1000RangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
    transmitInit();

	// # ====== 除錯 (Debug) ======
    // Serial.println("[DEBUG] Sending Encrypted Report..."); 
    // # =========================
    
    // 1. 產生標準 MAC 表頭 (維持原本邏輯)
    _globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
    data[SHORT_MAC_LEN] = RANGE_REPORT; // 設定功能碼
    
    // --------------- [加密修改開始] ---------------
    // 2. 準備明文
    float dist = myDistantDevice->getRange();
    char plainBuf[128]; // 開大一點給 Padding 用
    int baseLen = snprintf(plainBuf, sizeof(plainBuf), "%.2f", dist); // 保留2位小數

	int offset = SHORT_MAC_LEN + 1; //10

    // 判斷是否啟用加密
    if (_isEncryptionEnabled) {
        // --- 模式 A: 加密開啟 (AES-GCM + IV Counter) ---
        
        // 1) 處理 Padding (實驗要求)
        if (_expPaddingLen > 0) {
            // 在數字字串後面補 0，增加長度但不影響數值解析
            if (baseLen + _expPaddingLen < sizeof(plainBuf)) {
                memset(plainBuf + baseLen, '0', _expPaddingLen);
                baseLen += _expPaddingLen;
                plainBuf[baseLen] = '\0'; // 確保結尾
            }
        }

        // 2) 準備 IV (使用 Counter)
        unsigned char iv[ENC_IV_LEN];
        memset(iv, 0, ENC_IV_LEN);
        // 將 Counter 複製到 IV 前 4 bytes
        memcpy(iv, &_expIVCounter, sizeof(uint32_t));

		// # ====== 驗證 (Verify) ======
        // 確保 Counter 每次發送後遞增，避免重放攻擊
        _expIVCounter++;

		// 3. 加密
        unsigned char tag[ENC_TAG_LEN];
        unsigned char ciphertext[128];
        
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        // 注意: 這裡使用 256 bits key，請確保 UWB_AES_KEY 定義長度正確
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, UWB_AES_KEY, 256);
        
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 
                                  baseLen, iv, ENC_IV_LEN, 
                                  NULL, 0, 
                                  (const unsigned char*)plainBuf, ciphertext, 
                                  ENC_TAG_LEN, tag);
        mbedtls_gcm_free(&gcm);

        // 4. 組裝封包: [Ver(0x01)] [IV] [Tag] [Ciphertext]
        data[offset++] = ENC_VER; // 0x01 代表加密
        memcpy(&data[offset], iv, ENC_IV_LEN);    offset += ENC_IV_LEN;
        memcpy(&data[offset], tag, ENC_TAG_LEN);  offset += ENC_TAG_LEN;
        memcpy(&data[offset], ciphertext, baseLen); offset += baseLen;
        data[offset++] = g_group_crc[0];
		data[offset++] = g_group_crc[1];

        // # ====== 除錯 (Debug) ======
        if(DEBUG) { 
            Serial.print("[Anchor] Sent Encrypted. Payload Size: "); 
            Serial.println(offset); 
            // printHex("IV: ", iv, ENC_IV_LEN); // 需要詳細除錯時可打開
        }

    } else {
        // --- 模式 B: 加密關閉 (明文傳送) ---
        // 為了讓 Tag 容易分辨，我們送版本號 0x00，後面直接接明文
        data[offset++] = 0x00; // 0x00 代表明文
        memcpy(&data[offset], plainBuf, baseLen);
        offset += baseLen;
        
        // # ====== 除錯 (Debug) ======
        if(DEBUG) { Serial.println("[Anchor] Sent Plaintext."); }
    }

    // 發送
    copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
    DW1000.setData(data, offset); 
    DW1000.setDelay(DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));
    DW1000.startTransmit();
    // ======================== [加密修改結束] ========================
}
	// ========= [End Add] =========

void DW1000RangingClass::transmitRangeFailed(DW1000Device* myDistantDevice) {
	transmitInit();
	_globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
	data[SHORT_MAC_LEN] = RANGE_FAILED;
	
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data);
}

void DW1000RangingClass::receiver() {
	DW1000.newReceive();
	DW1000.setDefaults();
	// so we don't need to restart the receiver manually
	DW1000.receivePermanently(true);
	DW1000.startReceive();
}
// =============================================================


/* ###########################################################################
 * #### Methods for range computation and corrections  #######################
 * ######################################################################### */


void DW1000RangingClass::computeRangeAsymmetric(DW1000Device* myDistantDevice, DW1000Time* myTOF) {
	// asymmetric two-way ranging (more computation intense, less error prone)
	DW1000Time round1 = (myDistantDevice->timePollAckReceived-myDistantDevice->timePollSent).wrap();
	DW1000Time reply1 = (myDistantDevice->timePollAckSent-myDistantDevice->timePollReceived).wrap();
	DW1000Time round2 = (myDistantDevice->timeRangeReceived-myDistantDevice->timePollAckSent).wrap();
	DW1000Time reply2 = (myDistantDevice->timeRangeSent-myDistantDevice->timePollAckReceived).wrap();
	
	myTOF->setTimestamp((round1*round2-reply1*reply2)/(round1+round2+reply1+reply2));
	/*
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("timePollSent ");myDistantDevice->timePollSent.print();
	Serial.print("round1 "); Serial.println((long)round1.getTimestamp());
	
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("timePollReceived ");myDistantDevice->timePollReceived.print();
	Serial.print("reply1 "); Serial.println((long)reply1.getTimestamp());
	
	Serial.print("timeRangeReceived ");myDistantDevice->timeRangeReceived.print();
	Serial.print("timePollAckSent ");myDistantDevice->timePollAckSent.print();
	Serial.print("round2 "); Serial.println((long)round2.getTimestamp());
	
	Serial.print("timeRangeSent ");myDistantDevice->timeRangeSent.print();
	Serial.print("timePollAckReceived ");myDistantDevice->timePollAckReceived.print();
	Serial.print("reply2 "); Serial.println((long)reply2.getTimestamp());
	 */
}


/* FOR DEBUGGING*/
void DW1000RangingClass::visualizeDatas(byte datas[]) {
	char string[60];
	sprintf(string, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
					datas[0], datas[1], datas[2], datas[3], datas[4], datas[5], datas[6], datas[7], datas[8], datas[9], datas[10], datas[11], datas[12], datas[13], datas[14], datas[15]);
	Serial.println(string);
}



/* ###########################################################################
 * #### Utils  ###############################################################
 * ######################################################################### */

float DW1000RangingClass::filterValue(float value, float previousValue, uint16_t numberOfElements) {
	
	float k = 2.0f / ((float)numberOfElements + 1.0f);
	return (value * k) + previousValue * (1.0f - k);
}



