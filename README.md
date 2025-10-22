# 需要安裝的

- 安裝library，左邊輸入DW1000，下載第一個
- 安裝USB驅動程式 -> https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers

# 執行兩個UWB

**1. 需要硬體**
- 1個Tag (插在電腦USB上)
- 1個Anchor (插在電腦USB上)

**2. 執行**
- 上方開發版選擇 ESP32 Dev Module
- Tools -> 選擇Port -> (應該會是COM7,COM8，兩個版子要不一樣)
- Upload 兩個版子
    * DW1000_2Ranging_ANCHOR
    * DW1000_2Ranging_TAG
- 右上角放大鏡點開，應該就可以看到數值了

# 執行三個UWB

**1. 需要硬體**
- 1個Tag (插在行動充電上)
- 2個Anchor (插在電腦USB上)

**2. 執行**
- 上方開發版選擇 ESP32 Dev Module
- Tools -> 選擇Port -> (應該會是COM7,COM8,COM10，三個版子要不一樣)
- Upload 三個版子
    * DW1000_3Positioning_ANCHOR
    * DW1000_3Positioning_ANCHOR2
    * DW1000_3Positioning_TAG (有三個地方要自己改，有標住在程式裡)
    * UWB_Position_Display.py (有一個地方要自己改，有標住在程式裡)
- 開啟 UWB_Position_Display.py 檔案，就可以看位置了
