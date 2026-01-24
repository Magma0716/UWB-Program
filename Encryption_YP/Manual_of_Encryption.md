# 加密 DW1000 使用手冊（Anchor / Tag + `DW1000_encryption_example.py`）

> 目的：讓第一次接觸的人，能依照步驟完成：
> 1) 燒錄 `anchor.ino` 與 `tag.ino` 到兩塊板子  
> 2) 以 `DW1000_encryption_example.py` 從序列埠收集資料，輸出 CSV + PNG 報表  
>
> 本手冊內容以現有檔案行為為準（不做假設），並在附錄提供關鍵行號作為對照證據。

---

## 1. 系統構成與資料流

### 1.1 角色
- **Anchor**：負責作為 ranging 的錨點端（`startAsAnchor(...)`）。
- **Tag**：負責與 Anchor ranging，並輸出距離資料（`startAsTag(...)`）。

### 1.2 電腦端 Python 腳本吃什麼資料
`DW1000_encryption_example.py` 只會把「以 `DATA,` 開頭」的行當作有效資料行（證據：Python L234 `if line.startswith("DATA,")`）。  
資料格式要求為：

- `DATA,<seq>,<distance>`

其中：
- `<seq>`：必須能轉成整數 `int`（證據：Python L249 `seq = int(seq_str)`）
- `<distance>`：必須能轉成浮點數 `float`（證據：Python L250 `dist = float(dist_token)`）

### 1.3 目前 `anchor.ino` / `tag.ino` 實際輸出行為（重要）
- `tag.ino` 會印出 `DATA,<timeDiff_ms>,<dist>`（證據：Tag L70~L75）
  - 第二欄實際是 **兩次 `newRange()` 之間的毫秒差 `timeDiff`**，但 Python 仍會把它放進欄位 `seq`（因為只要求 `int`）。
- `anchor.ino` 主要印 `from: ... Range: ... RX power: ...`（證據：Anchor L84 起），**不是 `DATA,*,*` 格式**。

**結論（務必照做）：**
- 想「不改 Python」直接跑起來：`DW1000_encryption_example.py` 的 `COM_PORT` 必須連到 **Tag 那塊板子**（因為只有 Tag 會輸出 `DATA,*,*`）。

---

## 2. 環境需求

### 2.1 硬體
- 兩塊 UWB 板（1 塊燒 Anchor、1 塊燒 Tag）
- 兩條 USB 線（建議兩塊都接電腦供電；Python 一次只讀一個序列埠）

### 2.2 軟體
- Arduino IDE 或 PlatformIO（擇一）
- Python 3.x（建議 3.10+）

### 2.3 Python 套件
`DW1000_encryption_example.py` 需要：
- `pyserial`、`numpy`、`pandas`、`matplotlib`（證據：Python import 區段）

安裝：
```bash
python3 -m pip install -U pyserial numpy pandas matplotlib
````

> 腳本包含 pyserial/serial 裝錯的防呆提示（證據：Python L50~L58）。

---

## 3. 快速開始（最短成功路徑）

### 3.1 燒錄 Anchor

1. 將 `anchor.ino` 改名為 `anchor.ino`（或複製內容到 Arduino IDE 新檔）
2. 選擇正確板子與 Port
3. Upload 上傳

Anchor 端已設定（證據見附錄行號）：

* `Serial.begin(115200)`（Anchor L24）
* `setEncryptionFlag(true)`（Anchor L35）
* `setEncryptionDebugFlag(true)`（Anchor L39）
* `setIVCounter(0)`（Anchor L43）
* `setIVMode(IV_MODE_RAND_UNIQUE)`（Anchor L48）
* `setPaddingLength(16)`（Anchor L52）
* `startAsAnchor(... MODE_LONGDATA_RANGE_LOWPOWER ...)`（Anchor L67）

### 3.2 燒錄 Tag

1. 將 `tag.ino` 改名為 `tag.ino`（或複製內容到 Arduino IDE 新檔）
2. 選擇正確板子與 Port
3. Upload 上傳

Tag 端已設定（證據見附錄行號）：

* `Serial.begin(115200)`（Tag L22）
* `setEncryptionDebugFlag(true)`（Tag L34）
* `startAsTag(... MODE_LONGDATA_RANGE_LOWPOWER)`（Tag L46）
* `newRange()` 會輸出 `DATA,<timeDiff>,<dist>`（Tag L56~L75）

### 3.3 執行 Python 收資料（連 Tag 的序列埠）

1. 編輯 `DW1000_encryption_example.py` 的設定區（證據：Python L60~L69）：

   * `COM_PORT`：填入 Tag 的序列埠路徑
   * `BAUD_RATE = 115200`
   * `MAX_SAMPLES`：收樣本數（預設 200）
2. 執行：

```bash
python3 DW1000_encryption_example.py
```

---

## 4. Python 腳本可調參數（電腦端）

以下全部位於 `DW1000_encryption_example.py` 設定區（證據：Python L60~L69）：

| 變數            |                     預設值 | 作用          | 典型調整時機                                |
| ------------- | ----------------------: | ----------- | ------------------------------------- |
| `COM_PORT`    | `/dev/cu.usbserial-...` | 指定要讀取的序列埠   | 依實際連到 Tag 的 Port 修改                   |
| `BAUD_RATE`   |                `115200` | 序列埠速率       | 必須與 Arduino `Serial.begin(115200)` 一致 |
| `MAX_SAMPLES` |                   `200` | 收集的 DATA 筆數 | 需要更穩定統計時提高（如 500/2000）                |
| `OUTPUT_DIR`  |         `./uwb_reports` | 輸出資料夾       | 需要集中管理報表時修改                           |
| `SAVE_PNG`    |                  `True` | 是否輸出 PNG    | 不需要圖片時可關                              |
| `SAVE_CSV`    |                  `True` | 是否輸出 CSV    | 不需要表格時可關                              |
| `USER_NOTE`   |                    `""` | 檔名附註        | 加上批次、位置、板子版本等                         |

---

## 5. Arduino 端可調參數（加密 / IV / Padding）

### 5.1 Anchor 端（`anchor.ino`）

Anchor 的「實驗參數設定區」可直接控制下列條件（證據：Anchor L33~L52）：

| 參數         | 程式呼叫                                               | 作用                                         |
| ---------- | -------------------------------------------------- | ------------------------------------------ |
| 加密開關       | `setEncryptionFlag(true/false)`                    | 開/關加密（預設註解說明為 false）                       |
| 加密除錯       | `setEncryptionDebugFlag(true/false)`               | 印出加密相關 debug（KEY/IV/TAG/CT 等，依 library 實作） |
| COUNTER 起點 | `setIVCounter(0...)`                               | 只在 COUNTER IV 模式有意義                        |
| IV 模式      | `setIVMode(IV_MODE_COUNTER / IV_MODE_RAND_UNIQUE)` | 切換計數器 IV 或不重複隨機 IV                         |
| Padding 長度 | `setPaddingLength(n)`                              | 在「距離字串」後補 0（ASCII）以增加 payload              |

> `anchor.ino` 預設已選 `IV_MODE_RAND_UNIQUE` 且 `PaddingLength=16`（Anchor L48, L52）。

### 5.2 Tag 端（`tag.ino`）的關鍵差異

`tag.ino` 目前 **只有** `setEncryptionDebugFlag(true)`（Tag L34），沒有 `setEncryptionFlag(true)`、沒有 `setIVMode(...)`、沒有 `setPaddingLength(...)`。

因此若 Python 連到 Tag 收資料，常見現象是：

* 報表標題 `IV=None/UNK`：因為 Python 的 IV 推定只抓 `[ENC] setIVMode = ...` 或 `[ENC][TX] ivMode = ...`（證據：Python L78~L80），但 Tag 端不一定會印出這類行。
* 報表 `PAD=0/UNK`：因為 PAD 推估靠距離字串小數位的「多出且全為 0」判斷（證據：Python L109~L120），而 Tag 端輸出是 `Serial.println(dist)` 的 float 文字，格式可能不保留尾端 0（Tag L75）。

---

## 6. 產出內容（如何驗證「有成功」）

### 6.1 終端機輸出（Python 端）

每收到一筆有效 DATA，會印：

* `[k/MAX_SAMPLES] Seq=... | Dist=... m | Interval=... ms`（證據：Python L271~L272）

`Interval` 為電腦端估算的相鄰到達間隔：

* 用 `time.perf_counter()` 計算（證據：Python L255~L261）
* 會濾掉 `<=0` 或 `>=5000 ms` 的異常間隔（證據：Python L264~L265）

  * 這會導致某些筆被跳過，屬於設計行為（避免序列阻塞造成扭曲）。

### 6.2 CSV 輸出

輸出欄位（證據：Python L306~L311）：

* `seq`：DATA 第二欄（目前 Tag 實作為 `timeDiff_ms`）
* `distance_m`：距離（公尺）
* `interval_ms`：電腦端估計到達間隔（ms）
* `pad_inferred_from_data`：從距離字串推估的 PAD（可為 NaN）

CSV 寫入動作（證據：Python L317）。

### 6.3 PNG 圖表輸出

* 圖檔存檔（證據：Python L369~L371）
* PNG 由兩張子圖構成（證據：Python 設定 title 區段 L338~L360）：

  * 左：距離穩定性（Avg/Std）
  * 右：到達間隔（Avg/Std），尖峰通常代表暫時卡頓或負載抖動
* 圖表總標題：

  * `UWB Experiment | ENC=... | IV=... | PAD=...`（證據：Python L321）
* 圖表底部 footer：

  * `Port=... | Baud=... | Time=...`（證據：Python L322）

### 6.4 檔名規則（用於整理實驗批次）

檔名 label 會自動產生（證據：Python L297~L303）：

* `ENC_{ON/OFF}_IV_{mode/UNK}_PAD{pad/UNK}`
* 若抓到 COUNTER 且抓到 ivCounter，會追加 `_IVCTR{n}`（證據：Python L298~L300）
* 若 `USER_NOTE` 非空，會追加 `__{USER_NOTE}`（證據：Python L301~L303）

---

## 7. 常見問題排除（以「證據」定位）

### 7.1 Python 收不到資料（卡在 0/MAX_SAMPLES）

**最常見原因：Python 連到 Anchor。**
因為 Python 只認 `DATA,`（Python L234），而 Anchor 沒輸出 `DATA,*,*`（Anchor L84 起）。

處理：

* `COM_PORT` 改連 Tag 的序列埠（Python L61）。

### 7.2 `IV=None` 或 `IV=UNK`（報表標題）

IV 推定來源只看兩種 debug 格式（證據：Python L78~L80）：

* `[ENC] setIVMode = COUNTER|RAND_UNIQUE`
* `[ENC][TX] ivMode = COUNTER|RAND_UNIQUE`

若 Python 連到 Tag，而 Tag 端未印出上述行，就會顯示 `None/UNK`，屬於可預期結果。

改善方式（二擇一）：

1. 讓「被 Python 讀取的那塊板子」也印出 `setIVMode` 相關 log
2. 讓 Tag 端也呼叫 `setIVMode(...)`（需修改 Tag 程式碼）

### 7.3 `PAD=0` 長期不變

PAD 推估依規則（證據：Python L109~L120）：

* 以 `base_decimals=4` 為基準
* 只有「超過 4 位的小數位全部為 0」才算 padding
* 若多出位數包含非 0，回傳 None

但 Tag 端輸出 `Serial.println(dist)` 不保證尾端 0，因此 PAD 推估可能永遠是 0 或 NaN，屬於輸出格式造成的限制。

改善方式：

* 需讓輸出的 `<distance>` 以「固定小數位字串」形式印出（會牽涉 Arduino 端格式化輸出）。

### 7.4 加密是否真的啟用（避免只看標題）

Python 的 `ENC=ON/OFF` 只依據「有沒有看到任何 `[ENC` 字樣」判斷（證據：Python L218~L221），屬於「log 偵測」而不是加密學上的硬證明。

更可靠的做法：

* 直接在序列輸出檢查加密 debug（KEY/IV/TAG/CT 等），前提是 `setEncryptionDebugFlag(true)`（Anchor L39、Tag L34）。
* 讓 Tag 與 Anchor 的加密設定一致（尤其 `setEncryptionFlag(true)` 目前只在 Anchor 出現：Anchor L35）。

---

## 8. 建議實驗設計（做比較時避免混雜變因）

建議採「一次只改一個變數」：

1. 固定 UWB mode：兩端皆使用 `DW1000.MODE_LONGDATA_RANGE_LOWPOWER`（Anchor L67、Tag L46）
2. 固定擺放距離/方向/環境干擾
3. 固定樣本數：例如每個條件 `MAX_SAMPLES=200/500/2000`（Python L63）
4. 條件分組建議：

   * 無加密：`setEncryptionFlag(false)`
   * 加密 + COUNTER：`setIVMode(IV_MODE_COUNTER)`
   * 加密 + RAND_UNIQUE：`setIVMode(IV_MODE_RAND_UNIQUE)`
   * Padding：`setPaddingLength(0/16/...)`

> 若 Python 固定讀 Tag，且需要標題/檔名正確帶出 IV/PAD，需讓 Tag 端也能輸出對應 debug 或距離字串格式；否則標題欄位可能呈現 `UNK/None/0`，屬於目前程式設計行為。

---

## 附錄 A：關鍵行號對照（快速稽核）

### A.1 `anchor.ino`

* Anchor 位址：L10 `#define ANCHOR_ADD "AA:...:01"`
* 序列速率：L24 `Serial.begin(115200);`
* 加密開關：L35 `setEncryptionFlag(true);`
* 加密 debug：L39 `setEncryptionDebugFlag(true);`
* IV counter：L43 `setIVCounter(0);`
* IV 模式：L48 `setIVMode(IV_MODE_RAND_UNIQUE);`
* Padding：L52 `setPaddingLength(16);`
* 啟動 Anchor：L67 `startAsAnchor(... MODE_LONGDATA_RANGE_LOWPOWER, false);`
* Anchor 輸出格式（非 DATA）：L84 起 `Serial.print("from: "); ...`

### A.2 `tag.ino`

* 序列速率：L22 `Serial.begin(115200);`
* 加密 debug：L34 `setEncryptionDebugFlag(true);`
* 啟動 Tag：L46 `startAsTag("BB:...:01", DW1000.MODE_LONGDATA_RANGE_LOWPOWER);`
* DATA 輸出：L70~L75 `DATA,<timeDiff>,<dist>`

### A.3 `DW1000_encryption_example.py`

* 設定區：L60~L69（`COM_PORT/BAUD_RATE/MAX_SAMPLES/OUTPUT_DIR/...`）
* 只認 DATA 行：L234 `if line.startswith("DATA,")`
* 型別要求：L249 `int(seq_str)`、L250 `float(dist_token)`
* interval 計算：L255~L261
* interval 異常濾除：L264 `>=5000 ms` 或 `<=0`
* `ENC` 偵測：L219 `if "[ENC" in line:`
* IV 推定 regex：L78~L80
* PAD 推估函式：L109 `def _infer_pad_len_from_distance_token(... base_decimals=4)`
* 檔名 label：L297 `auto_label = f"ENC_{...}_IV_{...}_PAD{...}"`
* 圖表標題 header：L321
* footer：L322
* CSV 輸出：L317
* PNG 輸出：L370
