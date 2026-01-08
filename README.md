```
更新：
2025-10-19 -> v1.0 使用 Makerfabs 的量測程式去改編
https://github.com/Makerfabs/Makerfabs-ESP32-UWB

2025-10-27 -> v2.0 使用 Jim Remington 的量測程式去改編 (比v1.0計算更精準)
https://github.com/jremington/UWB-Indoor-Localization_Arduino
```

# 需要安裝的

- 安裝library，把DW1000這個資料夾，放入Arduino的函式庫裡 (預設：文件 -> Arduino -> libraries)
- 安裝USB驅動程式 -> https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers

# Tag設定

- 開啟 ESP32_UWB_setup_tag.ino
   * 設定 ssid = 自己的 WiFi 名稱
   * 設定 password = 需要改成自己的 WiFi 密碼
   * 設定 host = 需要改成自己的電腦 IP (cmd -> ipconfig)
- 接上 UWB 板子
- 上方開發版選擇 ESP32 Dev Module
- Tools -> 選擇Port -> (應該會是COM7,COM8...)
- Upload 程式

# Anchor設定

- 開啟 ESP32_anchor_autocalibrate.ino
   * 設定 Anchor1 = "81:00:22:EA:82:60:3B:9C"
   * 設定 Anchor2 = "82:00:22:EA:82:60:3B:9C"
   * 設定 Anchor3 = "83:00:22:EA:82:60:3B:9C"
   * 設定 Anchor4 = "84:00:22:EA:82:60:3B:9C"
   * this_anchor_target_distance = 實際的距離 (7~8公尺之間都可以)
- 把預先用好的 Tag 跟現在的 Anchor 保持剛剛你設定的距離 (在真實世界)
- 接上 UWB 板子
- 上方開發版選擇 ESP32 Dev Module
- Tools -> 選擇Port -> (應該會是COM7,COM8...)
- Upload 程式

```
程式執行完之後，會出現 final Adelay 的數值
這個是用來校準距離誤差用的，先把他記下來
```

- 開啟 ESP32_UWB_setup_anchor.ino
   * 設定 Anchor1 = "81:00:22:EA:82:60:3B:9C"
   * 設定 Anchor2 = "82:00:22:EA:82:60:3B:9C"
   * 設定 Anchor3 = "83:00:22:EA:82:60:3B:9C"
   * 設定 Anchor4 = "84:00:22:EA:82:60:3B:9C"
   * Adelay = 剛剛記下的 final Adelay
   * Adelay 下方的註解，可以用來記錄每個 Anchor 的校準誤差
- Tools -> 選擇Port -> (應該會是COM7,COM8...)
- Upload 程式

```
整個過程完成後，一個 Anchor 就校準完畢了
之後的 Anchor 就是重複上面的步驟
```

# Python設定

- 開啟 UWB_Position_Display.py
  * 設定 distance_A1_A2 = 實際的Anchor公尺距離 (需要自己拿捲尺量)
  * 設定 MeterToPixel = Python畫面的放大程度 (像素)
