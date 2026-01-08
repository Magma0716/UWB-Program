import matplotlib.pyplot as plt
import numpy as np
import matplotlib

matplotlib.rcParams['font.sans-serif'] = ['Microsoft JhengHei']  # 支援中文 (Windows)
matplotlib.rcParams['axes.unicode_minus'] = False  # 避免負號顯示成方框

# =======================
# 實際量測資料
# =======================
actual = np.array([
    0,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,
    1.0,1.1,1.2,1.3,1.4,1.5,1.6,1.7,1.8,1.9,
    2.0,2.1,2.2,2.3,2.4,2.5,2.6,2.7,2.8,2.9,3.0
])
measured = np.array([
    0.6,0.76,1.0,1.2,1.26,1.38,1.48,1.7,1.76,1.83,
    2.0,2.13,2.27,2.4,2.45,2.57,2.83,3.0,3.11,3.12,
    3.21,3.27,3.45,3.48,3.65,3.7,3.8,4.0,4.15,4.16,4.35
])

# =======================
# 線性回歸 (y = ax + b)
# =======================
coeffs = np.polyfit(actual, measured, 1)
a, b = coeffs
print(f"線性擬合結果：量測距離 = {a:.4f} × 實際距離 + {b:.4f}")

# 擬合線
fit_line = a * actual + b

# =======================
# 視覺化圖表
# =======================
plt.figure(figsize=(10,5))

# --- (1) 散點 + 回歸線 ---
plt.subplot(1, 2, 1)
plt.scatter(actual, measured, color='blue', label='實際量測資料')
plt.plot(actual, fit_line, color='red', label=f'線性擬合：y = {a:.2f}x + {b:.2f}')
plt.xlabel("實際距離 (m)")
plt.ylabel("量測距離 (m)")
plt.title("UWB 距離量測誤差模型")
plt.legend()
plt.grid(True)

# --- (2) 誤差曲線 ---
error = measured - actual
plt.subplot(1, 2, 2)
plt.plot(actual, error, marker='o', color='green')
plt.xlabel("實際距離 (m)")
plt.ylabel("誤差 (量測 - 實際) (m)")
plt.title("UWB 距離誤差分佈")
plt.grid(True)

plt.tight_layout()
plt.show()
