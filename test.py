import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

# ==========================================
# ─── 填入數值區 ───────────────────────────
# ==========================================
paddings =     [-1, 0, 2, 4, 8, 16, 32] 
ms_std_list =  [34.0, 40.8, 39.5, 37.9, 37.4, 39.4, 42.1]
dis_std_list = [0.0695, 0.0632, 0.0788, 0.0750, 0.0864, 0.0854, 0.0873]
# ==========================================

df = pd.DataFrame({
    'padding': paddings,
    'ms_std': ms_std_list,
    'dis_std': dis_std_list
})

sns.set_theme(style="whitegrid")
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))

# --- 圖表 1: ms_std ---
sns.lineplot(data=df, x='padding', y='ms_std', marker='o', ax=ax1, color='royalblue', markersize=8)
ax1.set_title('Padding vs. ms_std', fontsize=14, fontweight='bold')
ax1.set_xlabel('Padding', fontsize=12)
ax1.set_ylabel('ms_std', fontsize=12)

# 【關鍵修改點】：只顯示陣列內的 X 軸刻度
ax1.set_xticks(paddings) 

if -1 in paddings:
    val_at_minus_1 = df[df['padding'] == -1]['ms_std'].values[0]
    ax1.axvline(x=-1, color='red', linestyle='--', alpha=0.5)


# --- 圖表 2: dis_std ---
sns.lineplot(data=df, x='padding', y='dis_std', marker='s', ax=ax2, color='darkorange', markersize=8)
ax2.set_title('Padding vs. dis_std', fontsize=14, fontweight='bold')
ax2.set_xlabel('Padding', fontsize=12)
ax2.set_ylabel('dis_std', fontsize=12)

# 【關鍵修改點】：只顯示陣列內的 X 軸刻度
ax2.set_xticks(paddings) 

if -1 in paddings:
    val_at_minus_1_dis = df[df['padding'] == -1]['dis_std'].values[0]
    ax2.axvline(x=-1, color='red', linestyle='--', alpha=0.5)


plt.tight_layout()
plt.show()