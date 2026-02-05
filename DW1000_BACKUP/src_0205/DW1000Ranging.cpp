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
		memcpy((uint8_t *)&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device)); //3_16_24 add pointer cast sjr
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
		memcpy((uint8_t *)&_networkDevices[_networkDevicesNumber], device, sizeof(DW1000Device));  //3_16_24 pointer cast sjr
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
			memcpy((uint8_t *)&_networkDevices[i], &_networkDevices[i+1], sizeof(DW1000Device));  //3_16_24 pointer cast sjr
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
	return -1; // Default return value to prevent compilation error
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
		
		int messageType = detectMessageType(data); // 解析 msgid
		
		// (B1) ANCHOR 收到 BLINK：TAG 在找 anchor
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

				// ===== [Delete] Original RANGE_REPORT (plaintext floats) =====
				// 原版 payload 格式（固定 8 bytes）：
				//   - curRange   : float @ data + (1 + SHORT_MAC_LEN)
				//   - curRXPower : float @ data + (5 + SHORT_MAC_LEN)
				/*
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
				// ========= [End Delete] =========


				// ===== [Add] New RANGE_REPORT (ver+plen+payload, supports plaintext or AES-GCM) =====
				// 新版 payload 格式（自描述）：
				//   [ver:1][plen:1][payload:plen]
				//   - ver==0x00   ：明文（payload 是 ASCII 距離字串，可能含 padding '0'）
				//   - ver==ENC_VER：加密（payload = IV + TAG + CT；解密後得到 ASCII 距離字串）
				else if(messageType == RANGE_REPORT) {

					const int payloadStart = SHORT_MAC_LEN + 1;  // [ver] 的位置（msgid 後面第一個 byte）
					const uint8_t ver  = data[payloadStart];     // 版本/格式：0x00 明文、ENC_VER 加密
					const uint8_t plen = data[payloadStart + 1]; // payload 長度
					const int p0 = payloadStart + 2;             // payload 起點

					// 邊界檢查：用「實際收到的長度」rxLen 當上限（避免讀到殘留 data）
					uint16_t rxLen = DW1000.getDataLength();
					if (rxLen > LEN_DATA) rxLen = LEN_DATA;
					if (p0 + (int)plen > (int)rxLen) {
						return; // 格式不合理：直接丟掉
					}

					float curRange = 0.0f;
					float curRXPower = DW1000.getReceivePower(); // RXPower 改由「此刻接收」直接量測
					bool ok = false;                             // 是否成功解析/解密距離

					if (ver == 0x00) {
						// 明文：payload 直接是 ASCII 距離字串（可能含 padding '0'）
						char buf[128];
						int n = (int)plen;
						if (n > 127) n = 127;            // 防止 buf overflow
						memcpy(buf, &data[p0], (size_t)n);
						buf[n] = '\0';
						curRange = (float)atof(buf);     // ASCII -> float
						ok = true;

					} 
					else if (ver == ENC_VER) {

						#if defined(ARDUINO_ARCH_ESP32)
						// 加密：payload = IV(12) + TAG(16) + CT(ctLen)
						if (plen < (ENC_IV_LEN + ENC_TAG_LEN + 1)) {
							return; // 至少要有 1 byte CT
						}
						const int ivOff  = p0;                            // IV 起點
						const int tagOff = p0 + ENC_IV_LEN;               // TAG 起點
						const int ctOff  = p0 + ENC_IV_LEN + ENC_TAG_LEN; // CT 起點

						int ctLen = (int)plen - (ENC_IV_LEN + ENC_TAG_LEN);
						if (ctLen < 1) return;

						// decrypted 緩衝 128 bytes：超過就拒收（避免硬截短造成資料不一致）
						if (ctLen > 127) {
							if (_isEncryptionDebugEnabled) {
							Serial.print("[ENC][RX][DROP] ctLen too large = ");
							Serial.println(ctLen);
							}
							return;
						}

						unsigned char decrypted[128];
						memset(decrypted, 0, sizeof(decrypted));

						// AES-GCM 驗證 + 解密（auth_decrypt：TAG 不對就會 fail）
						mbedtls_gcm_context gcm;
						mbedtls_gcm_init(&gcm);
						mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, UWB_AES_KEY, 256);

						if (_isEncryptionDebugEnabled) {
							dumpHex("[ENC][RX] IV  = ", (const uint8_t*)(&data[ivOff]), ENC_IV_LEN);
							dumpHex("[ENC][RX] TAG = ", (const uint8_t*)(&data[tagOff]), ENC_TAG_LEN);
							dumpHex("[ENC][RX] CT  = ", (const uint8_t*)(&data[ctOff]), (size_t)ctLen);
						}

						int ret = mbedtls_gcm_auth_decrypt(
							&gcm,
							(size_t)ctLen,
							(const unsigned char*)(&data[ivOff]), ENC_IV_LEN,
							NULL, 0, // AAD（目前未使用）
							(const unsigned char*)(&data[tagOff]), ENC_TAG_LEN,
							(const unsigned char*)(&data[ctOff]),
							decrypted
						);
						mbedtls_gcm_free(&gcm);

						if (ret == 0) {
							// 解密成功：decrypted 是 ASCII 距離字串
							decrypted[ctLen] = '\0';
							if (_isEncryptionDebugEnabled) {
								Serial.print("[ENC][RX] PLAIN(str) = ");
								Serial.println((char*)decrypted);
							}
							curRange = (float)atof((char*)decrypted);
							ok = true;
						} else {
							// 解密失敗：可能 IV/TAG/KEY 不一致或封包被破壞
								if (_isEncryptionDebugEnabled) {
								Serial.print("[ENC][RX][ERR] auth_decrypt ret=");
								Serial.println(ret);
							}
							ok = false;
						}

						#else
						ok = false; // 非 ESP32：不支援 mbedtls-gcm，直接不解密
						#endif
					} 
					else {
						ok = false; // 未知 ver：直接視為不支援
					}

					if (!ok) {
						return;
					}

					// range filter：與原版一致（略過第一筆）
					if (_useRangeFilter) {
						if (myDistantDevice->getRange() != 0.0f) {
						curRange = filterValue(curRange, myDistantDevice->getRange(), _rangeFilterValue);
						}
					}

					// 更新此 device 的距離與 RXPower（交給上層 callback 使用）
					myDistantDevice->setRange(curRange);
					myDistantDevice->setRXPower(curRXPower);

					_lastDistantDevice = myDistantDevice->getIndex();
					if(_handleNewRange != 0) {
						(*_handleNewRange)(); // callback：通知上層「有新距離」
					}
				}
				// ========= [End Add] =========


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
			_networkDevices[i].timePollSent.getTimestamp(data+SHORT_MAC_LEN+4+17*i);
			_networkDevices[i].timePollAckReceived.getTimestamp(data+SHORT_MAC_LEN+9+17*i);
			_networkDevices[i].timeRangeSent.getTimestamp(data+SHORT_MAC_LEN+14+17*i);
			
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


void DW1000RangingClass::transmitRangeReport(DW1000Device* myDistantDevice) {
    transmitInit();

    // Short MAC header
    _globalMac.generateShortMACFrame(data, _currentShortAddress, myDistantDevice->getByteShortAddress());
    data[SHORT_MAC_LEN] = RANGE_REPORT;  // msgid


	// ===== [Delete] Original RANGE_REPORT payload (2 floats, fixed 8 bytes) =====
	// 原版封包格式（固定長度）：
	//   [Short MAC][msgid=RANGE_REPORT][curRange(float)][curRXPower(float)]
	/*
	// write final ranging result
	float curRange   = myDistantDevice->getRange();
	float curRXPower = myDistantDevice->getRXPower();
	//We add the Range and then the RXPower
	memcpy(data+1+SHORT_MAC_LEN, &curRange, 4);
	memcpy(data+5+SHORT_MAC_LEN, &curRXPower, 4);
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress());
	transmit(data, DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));

	*/
	// ========= [End Delete] =========


	// ===== [Add] New RANGE_REPORT payload (ver+plen+payload, supports plaintext or AES-GCM) =====
	// 新版先把距離做成字串（ASCII），再視需要：
	//   - 明文：直接塞 payload
	//   - 加密：AES-GCM 加密後塞 payload（IV+TAG+CT）
    float dist = myDistantDevice->getRange();         // 讀出本次量測距離（meters）
    char plainBuf[128];                               // 明文緩衝：存 ASCII 距離字串（含 padding）

	// 固定 4 位小數
    int baseLen = snprintf(plainBuf, sizeof(plainBuf), "%.2f", dist);             // 例："2.0310"
    if (baseLen < 0) baseLen = 0;                                                 // snprintf 失敗保底
    if (baseLen > (int)sizeof(plainBuf) - 1) baseLen = (int)sizeof(plainBuf) - 1; // 避免越界

	// padding：在距離字串後面補 '0'（ASCII）以增加負載
    int pad = (int)_paddingLength;                    // 使用者設定的 padding bytes（0 表示不補）
    if (pad < 0) pad = 0;                             // uint8_t 理論上不會 <0，這行是防呆

    int canAppend = (int)sizeof(plainBuf) - 1 - baseLen; // plainBuf 還能塞多少（留 '\0'）
    if (pad > canAppend) pad = canAppend;                // padding 太大就截到 buffer 能容納的上限

    memset(plainBuf + baseLen, '0', (size_t)pad);     // 直接補 ASCII '0'（不是 0x00）

    int plainLen = baseLen + pad;                     /* 明文實際長度（不含 '\0' */
    plainBuf[plainLen] = '\0';                        // 方便 debug 印字串；加密仍用 plainLen

    int idx = SHORT_MAC_LEN + 1;                      // idx 指向 msgid 後面的第一個位置（payload 區起點）

    const int verPos  = idx++;                        // ver 欄位位置（1 byte）
    const int plenPos = idx++;                        // payload_len 欄位位置（1 byte）

    data[verPos]  = (_isEncryptionEnabled ? ENC_VER : 0x00); // ver=ENC_VER 表示 AES-GCM；0x00 表示明文
    data[plenPos] = 0;                                       // 先填 0，後面組好 payload 才回填真正長度

	// msgid 佔 1 byte、ver 佔 1、plen 佔 1
    const int headerBytes = (SHORT_MAC_LEN + 1) + 1 + 1; // short-mac + msgid + ver + plen
    int maxPayload = (int)LEN_DATA - headerBytes;        // payload 允許最大長度（避免超過 data[]）
    if (maxPayload < 0) maxPayload = 0;

    if (_isEncryptionEnabled) {
		
		#if defined(ARDUINO_ARCH_ESP32)

		// payload = IV(12) + TAG(16) + CIPHERTEXT(plainLen)
		int maxPlain = maxPayload - (ENC_IV_LEN + ENC_TAG_LEN); // 留出 IV+TAG 後，明文最多可加密多少
		if (maxPlain < 0) maxPlain = 0;

		if (plainLen > maxPlain) {                       // 若明文太長，截短以免塞不下
		plainLen = maxPlain;
		plainBuf[plainLen] = '\0';                       // 只是為了 debug 可讀
		}

		uint8_t iv[ENC_IV_LEN];
		memset(iv, 0, sizeof(iv));                      // IV(Nonce) 緩衝（12 bytes）

		uint32_t usedCtr = 0xFFFFFFFF;                  // debug 用：counter 模式下印出用到的 counter
		bool ivOk = true;                               // 生成 IV 是否成功（RAND_UNIQUE 可能失敗）

		if (_ivMode == IV_MODE_COUNTER) {
			// COUNTER：前 4 bytes 放 counter，其餘 0（同 key 下 counter 必須單調不重複）
			usedCtr = _expIVCounter;
			memset(iv, 0, sizeof(iv));
			memcpy(iv, &_expIVCounter, sizeof(uint32_t)); // IV[0..3] = counter
			_expIVCounter++;                              // 下一包用下一個 counter

		} else if (_ivMode == IV_MODE_RAND_UNIQUE) {
			#if defined(ARDUINO_ARCH_ESP32)
			// RAND_UNIQUE：用 RNG 產 12 bytes，並用 table 檢查「本次開機期間不重複」
			ivOk = gen_unique_random_iv(iv);

			if (!ivOk) {
				// 為了實驗公平：失敗就丟包，不 fallback 成 counter（避免混到另一種模式）
				if (_isEncryptionDebugEnabled) {
					Serial.println("[ENC][TX][DROP] Unique random IV failed (table full or RNG issue).");
				}
				return;  // 直接停止：這次 RANGE_REPORT 不送
			}
			#else
			// 非 ESP32 沒有 esp_random：直接判定失敗
			ivOk = false;
			if (_isEncryptionDebugEnabled) {
				Serial.println("[ENC][TX][DROP] RAND_UNIQUE not supported on non-ESP32.");
			}
			return;
			#endif

		} else {
			// 不認得的模式：保守用 COUNTER（避免 iv 未初始化造成重複）
			usedCtr = _expIVCounter;
			memset(iv, 0, sizeof(iv));
			memcpy(iv, &_expIVCounter, sizeof(uint32_t));
			_expIVCounter++;
		}

		uint8_t tag[ENC_TAG_LEN];                        // AES-GCM authentication tag（16 bytes）
		uint8_t cipher[128];                             // 密文緩衝（大小需 >= plainLen）
		if (plainLen > (int)sizeof(cipher)) {            // 防呆：加密輸出 buffer 不夠就再截短
			plainLen = (int)sizeof(cipher);
			plainBuf[plainLen] = '\0';
		}

		// AES-GCM 加密：plainBuf -> cipher，同時輸出 tag
		mbedtls_gcm_context gcm;
		mbedtls_gcm_init(&gcm);
		mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, UWB_AES_KEY, 256);

		int ret = mbedtls_gcm_crypt_and_tag(
			&gcm, MBEDTLS_GCM_ENCRYPT,
			(size_t)plainLen,
			iv, ENC_IV_LEN,                               // nonce/IV（12 bytes）
			NULL, 0,                                      // AAD：目前未使用
			(const unsigned char*)plainBuf,               // input plaintext
			cipher,                                       // output ciphertext
			ENC_TAG_LEN, tag                              // output tag
		);
		mbedtls_gcm_free(&gcm);

		if (ret != 0) {
			// 加密失敗：退回明文（避免整個 ranging 因加密掛掉）
			data[verPos] = 0x00;                          // 改成明文版本

			int copyLen = plainLen;
			if (copyLen > maxPayload) copyLen = maxPayload;  // payload 上限保護

			memcpy(&data[idx], plainBuf, (size_t)copyLen);   // payload = plain bytes
			idx += copyLen;
			data[plenPos] = (uint8_t)copyLen;                // 回填 payload_len
		} else {
			// 加密成功：payload = IV + TAG + CIPHERTEXT
			memcpy(&data[idx], iv, ENC_IV_LEN);   idx += ENC_IV_LEN;
			memcpy(&data[idx], tag, ENC_TAG_LEN); idx += ENC_TAG_LEN;
			memcpy(&data[idx], cipher, (size_t)plainLen); idx += plainLen;

			data[plenPos] = (uint8_t)(ENC_IV_LEN + ENC_TAG_LEN + plainLen); // 回填 payload_len

			// debug：印出 key/IV/plain/tag/cipher（用於驗證格式與解密一致）
			if (_isEncryptionDebugEnabled) {
				if (!_encDbgKeyPrinted) {
					dumpHex("[ENC][TX] KEY = ", UWB_AES_KEY, 32); // key 只印一次，避免洗版
					_encDbgKeyPrinted = true;
				}

				Serial.print("[ENC][TX] ivMode = ");
				Serial.println((_ivMode == IV_MODE_COUNTER) ? "COUNTER" : "RAND_UNIQUE");
				
				if (_ivMode == IV_MODE_COUNTER) {
					Serial.print("[ENC][TX] ivCounter = ");
					Serial.println(usedCtr);                // 方便核對 counter 是否連續/不跳號
				} else {
					Serial.println("[ENC][TX] ivCounter = (n/a)");
				}

				dumpHex("[ENC][TX] IV  = ", iv, ENC_IV_LEN);
				Serial.print("[ENC][TX] PLAIN(str) = ");
				Serial.println(plainBuf);
				dumpHex("[ENC][TX] TAG = ", tag, ENC_TAG_LEN);
				dumpHex("[ENC][TX] CT  = ", cipher, (size_t)plainLen);
			}
		}
		#else
		// 非 ESP32：沒有 mbedtls AES-GCM，強制當明文送（保持可編譯/可跑）
		data[verPos] = 0x00;

		int copyLen = plainLen;
		if (copyLen > maxPayload) copyLen = maxPayload;

		memcpy(&data[idx], plainBuf, (size_t)copyLen); idx += copyLen;
		data[plenPos] = (uint8_t)copyLen;
		#endif

	} else {
		// 明文 payload = plainBuf bytes（不含 '\0'）
		if (plainLen > maxPayload) {                     // payload 不夠就截短
			plainLen = maxPayload;
			plainBuf[plainLen] = '\0';
		}
		memcpy(&data[idx], plainBuf, (size_t)plainLen); 
		idx += plainLen;
		data[plenPos] = (uint8_t)plainLen;               // 回填 payload_len
	}
/*
	if (idx + 2 <= (int)LEN_DATA) {
        data[idx] = 0x00;     // CRC 位置 1
        data[idx + 1] = 0x00; // CRC 位置 2
        idx += 2;             // 最終發送長度包含這兩格
    }
*/
	// 變長度發送（不再硬塞 LEN_DATA）
	copyShortAddress(_lastSentToShortAddress, myDistantDevice->getByteShortAddress()); // 記住這次送給誰（供 _sentAck 使用）
	DW1000.setDelay(DW1000Time(_replyDelayTimeUS, DW1000Time::MICROSECONDS));          // 設定回覆延遲（符合 ranging 時序）
	DW1000.setData(data, (uint16_t)idx);                                               // 用 idx 當「實際封包長度」
	DW1000.startTransmit();                                                            // 送出 RANGE_REPORT
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