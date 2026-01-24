# -*- coding: utf-8 -*-
"""
DW1000_encryption_example.py

用途：
1) 從序列埠（pyserial）讀取 UWB 裝置輸出文字行。
2) 解析兩大類資訊：
   A. 實驗資料行（DATA,seq,distance）：
      - seq：封包序號
      - distance：距離（公尺），通常是字串型態的數字
      - interval_ms：以電腦端 time.perf_counter() 估算相鄰兩筆資料的到達間隔（毫秒）
      - pad 推估：依「小數點後多出且全是 0 的位數」推估 padding 長度
   B. Debug 行（例如 [ENC]、[ENC][TX]）：
      - 角色（TAG / ANCHOR）
      - 是否看過加密相關訊息（enc_seen）
      - IV 模式（COUNTER / RAND_UNIQUE）
      - counter 模式下的 ivCounter（若有印出）

輸入格式假設：
- 裝置會輸出類似：
  - "DATA,<seq>,<distance>"
  - "### TAG ###" 或 "### ANCHOR ###"
  - "[ENC] setIVMode = COUNTER" / "[ENC][TX] ivMode = RAND_UNIQUE"
  - "ivCounter = 12345"

輸出：
- CSV：每筆 seq / distance / interval / pad 推估
- PNG：兩張圖（距離穩定性、到達間隔），標題自動帶入 ENC/IV/PAD 資訊
"""

import re
import time
from collections import Counter
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# -------------------- pyserial 匯入防呆 --------------------
try:
    import serial  # 需要 pyserial
except Exception as e:
    raise SystemExit(
        "[ERROR] Cannot import 'serial'. You likely need pyserial.\n"
        "Fix (Python 3.11): python3 -m pip install pyserial\n"
        f"Original error: {e}"
    )

if not hasattr(serial, "Serial"):
    raise SystemExit(
        "[ERROR] 'serial' module has no attribute 'Serial'.\n"
        "Usually means you installed the wrong package ('serial') or a local serial.py shadows pyserial.\n"
        "Fix:\n"
        "  1) python3 -m pip uninstall -y serial\n"
        "  2) python3 -m pip install -U pyserial\n"
        "  3) python3 -c \"import serial; print(serial.__file__)\"  (confirm it's pyserial)\n"
    )

# ================= 設定區 (請修改這裡) =================
COM_PORT = "/dev/cu.usbserial-02E2277A"
BAUD_RATE = 115200
MAX_SAMPLES = 200

OUTPUT_DIR = Path(__file__).resolve().parent / "uwb_reports"
SAVE_PNG = True
SAVE_CSV = True

USER_NOTE = ""  # optional：可以在檔名上追加一段自訂註記
# ======================================================

"""
Debug parsing（從裝置的 debug log 推出實驗模式）
- _RE_IVMODE_1：對應在 setIVMode() 印的 log
- _RE_IVMODE_2：對應在 TX 時印的 log
- _RE_IVCTR  ：擷取 counter 模式的 ivCounter
"""
_RE_IVMODE_1 = re.compile(r"\[ENC\]\s*setIVMode\s*=\s*(COUNTER|RAND_UNIQUE)")
_RE_IVMODE_2 = re.compile(r"\[ENC\]\[TX\]\s*ivMode\s*=\s*(COUNTER|RAND_UNIQUE)")
_RE_IVCTR = re.compile(r"ivCounter\s*=\s*(\d+)")


def _pick_iv_mode(line):
    """
    從單行 log 嘗試抓出 IV 模式（COUNTER / RAND_UNIQUE）

    允許兩種來源：
    - "[ENC] setIVMode = COUNTER"
    - "[ENC][TX] ivMode = RAND_UNIQUE"
    """
    m = _RE_IVMODE_1.search(line)
    if m:
        return m.group(1)
    m = _RE_IVMODE_2.search(line)
    if m:
        return m.group(1)
    return None


def _pick_iv_counter(line):
    """
    從單行 log 擷取 ivCounter（僅 counter 模式常見）
    例："[ENC][TX] ivCounter = 12345"
    """
    m = _RE_IVCTR.search(line)
    return int(m.group(1)) if m else None


def _infer_pad_len_from_distance_token(dist_token, base_decimals=4):
    """
    依規則：PAD 由距離字串的小數位長度推估

    規則（base_decimals=4）：
      PAD=0:  0.7000   (decimals=4)
      PAD=2:  0.700000 (decimals=6) -> 6-4=2

    注意：
    - 只有「超過 base_decimals 的多出位數全部都是 0」才算 padding
    - 若多出位數出現非 0（例如 0.700012），代表不是 padding，回傳 None
    """
    s = dist_token.strip()
    m = re.search(r"[-+]?\d+(?:\.\d+)?", s)
    if not m:
        return None
    num = m.group(0)
    if "." not in num:
        return 0
    dec = num.split(".", 1)[1]
    if len(dec) <= base_decimals:
        return 0
    extra = dec[base_decimals:]
    if set(extra) <= {"0"}:
        return len(extra)
    return None


def _mode_int(values):
    """
    取眾數（mode）。
    用在：多筆 pad 推估值中，挑出最常見的 pad 當作本次實驗的 PAD 標記。
    """
    if not values:
        return None
    return Counter(values).most_common(1)[0][0]


def _nanmean_std(x):
    """
    安全版平均/標準差：
    - 空列表：回 (0.0, 0.0)
    - 全是 NaN：回 (0.0, 0.0)
    """
    if not x:
        return 0.0, 0.0
    a = np.array(x, dtype=float)
    if np.all(np.isnan(a)):
        return 0.0, 0.0
    return float(np.nanmean(a)), float(np.nanstd(a))


def main():
    """
    主流程：
    1) 開序列埠，逐行讀取
    2) 解析 meta（角色、加密模式、IV 模式、ivCounter）
    3) 解析 DATA 行：seq / distance / interval / pad 推估
    4) 統計、輸出 CSV、畫圖輸出 PNG
    """
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # 實驗資料緩衝（依序 append）
    data_seq = []
    data_dist = []
    data_interval_ms = []
    data_pad_each = []

    # meta：從 debug log 推出（不影響資料解析，只用於標題/檔名）
    meta = {
        "role": None,          # "TAG" / "ANCHOR"
        "enc_seen": False,     # 有看到任何 [ENC...] 就算 True（代表這次跑過加密相關 log）
        "iv_mode": None,       # "COUNTER" / "RAND_UNIQUE"
        "iv_counter": None,    # 若 log 有印出就記下來（通常只有 COUNTER 會有）
    }

    print("=== UWB Experiment Tool (debug-aware titles) ===")
    print(f"Port        : {COM_PORT}")
    print(f"Baud        : {BAUD_RATE}")
    print(f"Max samples : {MAX_SAMPLES}")
    print(f"Output dir  : {OUTPUT_DIR}")
    print("-----------------------------------------------")

    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=2)
    except Exception as e:
        print(f"[ERROR] Cannot open port: {COM_PORT}")
        print("Reason:", e)
        return

    print("[OK] Connected. Start reading... (Press Ctrl+C to stop)\n")

    # last_t：用於估算相鄰資料到達間隔（ms）
    last_t = None

    try:
        while len(data_dist) < MAX_SAMPLES:
            # 讀一行（bytes->str），忽略無法解碼的字元，最後 strip 掉換行
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            # ---- meta from debug ----
            # 角色標記：由裝置啟動時印出（若沒有也不影響資料解析）
            if "### TAG ###" in line:
                meta["role"] = "TAG"
            elif "### ANCHOR ###" in line:
                meta["role"] = "ANCHOR"

            # 只要遇到任何 [ENC...] 字樣，就視為「本次跑過加密相關版本」
            if "[ENC" in line:
                meta["enc_seen"] = True

                # 嘗試從該行推 IV 模式（COUNTER/RAND_UNIQUE）
                m = _pick_iv_mode(line)
                if m:
                    meta["iv_mode"] = m

                # 嘗試從該行推 ivCounter（若有）
                c = _pick_iv_counter(line)
                if c is not None:
                    meta["iv_counter"] = c

            # ---- DATA parsing ----
            # DATA 行格式：DATA,<seq>,<distance>
            if line.startswith("DATA,"):
                parts = line.split(",")
                if len(parts) < 3:
                    continue

                seq_str = parts[1].strip()
                dist_token = parts[2].strip()

                # PAD 推估：只要符合「多出位數皆為 0」就記錄
                pad_guess = _infer_pad_len_from_distance_token(dist_token, base_decimals=4)
                if pad_guess is not None:
                    data_pad_each.append(pad_guess)

                # 轉型：seq 必須是 int、distance 必須是 float
                try:
                    seq = int(seq_str)
                    dist = float(dist_token)
                except ValueError:
                    continue

                # interval：用電腦端時間估算相鄰兩筆到達間隔（ms）
                now = time.perf_counter()
                if last_t is None:
                    interval_ms = float("nan")  # 第一筆沒有上一筆可比
                    last_t = now
                else:
                    interval_ms = (now - last_t) * 1000.0
                    last_t = now

                    # 濾掉不合理值（避免剛好卡到序列埠阻塞、或很久才來一筆造成扭曲）
                    if interval_ms <= 0 or interval_ms >= 5000:
                        continue

                data_seq.append(seq)
                data_dist.append(dist)
                data_interval_ms.append(interval_ms)

                interval_str = "NA" if np.isnan(interval_ms) else f"{interval_ms:.1f} ms"
                print(f"[{len(data_dist)}/{MAX_SAMPLES}] Seq={seq} | Dist={dist:.4f} m | Interval={interval_str}")

    except KeyboardInterrupt:
        print("\n[INFO] User interrupted.")
    finally:
        ser.close()

    # ---- stats ----
    n_dist = len(data_dist)
    avg_d, std_d = _nanmean_std(data_dist)
    avg_i, std_i = _nanmean_std(data_interval_ms)

    # pad_mode：用眾數代表「本次實驗大多數封包的 PAD 推估值」
    pad_mode = _mode_int(data_pad_each)

    # meta 整理成簡短字串（用於檔名/標題）
    enc_str = "ON" if meta["enc_seen"] else "OFF"
    iv_mode = meta["iv_mode"]
    iv_ctr = meta["iv_counter"]

    timestamp = time.strftime("%Y%m%d-%H%M%S")

    # filename label（檔名允許 UNK；圖上用較短標題避免爆版）
    pad_str = str(pad_mode) if pad_mode is not None else "UNK"
    iv_label = iv_mode if iv_mode else "UNK"
    auto_label = f"ENC_{enc_str}_IV_{iv_label}_PAD{pad_str}"
    if iv_mode == "COUNTER" and iv_ctr is not None:
        auto_label += f"_IVCTR{iv_ctr}"

    # USER_NOTE 可選：在檔名加上自訂資訊（例如實驗批次/裝置版本）
    label = auto_label if USER_NOTE.strip() == "" else f"{auto_label}__{USER_NOTE.strip()}"

    # ---- dataframe ----
    # pad_inferred_from_data：可能比 distance 筆數少（例如某些距離字串解析失敗），用 NaN 補齊
    df = pd.DataFrame({
        "seq": data_seq,
        "distance_m": data_dist,
        "interval_ms": data_interval_ms,
        "pad_inferred_from_data": (data_pad_each + [np.nan] * max(0, n_dist - len(data_pad_each)))[:n_dist],
    })

    csv_path = OUTPUT_DIR / f"UWB_Result_{label}_{timestamp}.csv"
    png_path = OUTPUT_DIR / f"UWB_Result_{label}_{timestamp}.png"

    if SAVE_CSV:
        df.to_csv(csv_path, index=False)

    # 圖標題用短字串（避免 title overflow）
    iv_short = iv_mode if iv_mode != "UNKNOWN" else "UNK"
    header = f"UWB Experiment | ENC={enc_str} | IV={iv_short} | PAD={pad_str}"
    footer = f"Port={COM_PORT} | Baud={BAUD_RATE} | Time={timestamp}"

    # ================== PLOT (fix title overflow) ==================
    fig = plt.figure(figsize=(12, 6))

    # suptitle：全圖主標題（短就好）
    fig.suptitle(header, y=0.985, fontsize=14)

    # footer：固定一行（放底部）
    fig.text(0.5, 0.015, footer, ha="center", va="bottom", fontsize=9)

    # ---- left subplot: distance ----
    ax1 = fig.add_subplot(1, 2, 1)
    ax1.plot(data_dist, marker="o", linestyle="-", color="blue", alpha=0.6, markersize=4)
    ax1.axhline(y=avg_d, color="red", linestyle="--", label=f"Avg: {avg_d:.4f} m")

    # 標題合併：避免 title 與文字重疊
    ax1.set_title(
        "Distance Stability\n"
        f"Avg={avg_d:.4f} m | Std={std_d:.4f} m | N={MAX_SAMPLES}",
        fontsize=12,
        pad=10
    )
    ax1.set_xlabel("Sample Index")
    ax1.set_ylabel("Measured Distance (m)")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # ---- right subplot: interval ----
    ax2 = fig.add_subplot(1, 2, 2)
    ax2.plot(data_interval_ms, marker="x", linestyle="-", color="orange", alpha=0.6, markersize=4)
    ax2.axhline(y=avg_i, color="red", linestyle="--", label=f"Avg: {avg_i:.1f} ms")

    ax2.set_title(
        "System Load / Inter-arrival Time\n"
        f"Avg={avg_i:.1f} ms | Std={std_i:.1f} ms | N={MAX_SAMPLES}",
        fontsize=12,
        pad=10
    )
    ax2.set_xlabel("Sample Index")
    ax2.set_ylabel("Inter-arrival Time (ms)")
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    # tight_layout：上緣留給 suptitle、下緣留給 footer
    fig.tight_layout(rect=[0, 0.05, 1, 0.90])

    if SAVE_PNG:
        plt.savefig(png_path, dpi=200)

    plt.show()


if __name__ == "__main__":
    main()
