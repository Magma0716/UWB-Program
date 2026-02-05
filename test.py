import matplotlib.pyplot as plt

# YP
padding1 = [-1, 0, 2, 4, 8, 16, 32]
dis_std1 = [0.0158,0.0164,0.0152,0.0264,0.0219,0.0142,0.0287]
ms_std1 = []
pac1 = []

# Me1 word
padding2 = [0, 45, 75]
dis_std2 = [0.0275, 0.0295, 0.0401]
ms_std2 = [32.1, 51.5, 33.5]
pac2 = [20, 36, 26]

# Me2 word
padding3 = [0, 45, 75]
dis_std3 = [0.0230, 0.0196, 0.0216]
ms_std3 = [32.6, 31.7, 33.2]
pac3 = [23, 11, 21]

# Me3 上次
padding4 = [0, 2, 4, 8, 16, 32]
dis_std4 = [0.0900, 0.0411, 0.0313, 0.0291, 0.0850, 0.0244] 
ms_std4 = [47.7, 45.5, 42.7, 43.2, 47.1, 45.5]
pac4 = [31, 19, 17, 24, 35, 27]

# 0204
padding5 = [-1, 0]
dis_std5 = [0.0167, 0.0174]
ms_std5 = [35.9, 28.2]
pac5 = [32, 12]

# 繪圖
plt.figure(figsize=(10, 6))
'''
plt.plot(padding1, dis_std1, marker='o', linestyle='--', color='y', label='YP')
plt.plot(padding2, dis_std2, marker='o', linestyle='-', color='r', label='0203')
plt.plot(padding3, dis_std3, marker='s', linestyle='-', color='g', label='0203')
plt.plot(padding4, dis_std4, marker='^', linestyle='-', color='b', label='0129')
plt.plot(padding5, dis_std5, marker='^', linestyle='-', color='b', label='0204')
'''
#plt.plot(padding1, ms_std1, marker='o', linestyle='--', color='b', label='YP')
plt.plot(padding2, ms_std2, marker='o', linestyle='-', color='r', label='0203')
plt.plot(padding3, ms_std3, marker='s', linestyle='-', color='g', label='0203')
plt.plot(padding4, ms_std4, marker='^', linestyle='-', color='b', label='0129')

# 圖表
plt.rcParams['font.sans-serif'] = ['Microsoft JhengHei'] 
plt.rcParams['axes.unicode_minus'] = False 
plt.xlabel('Padding')
plt.ylabel('延遲std')
plt.title('Padding vs. 延遲std')

# 設定 X 軸刻度，涵蓋所有出現過的 Padding 值
all_paddings = sorted(list(set(padding1 + padding2)))
plt.xticks(all_paddings)

plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()

# 在點上標註數值 (自動標註 ms1)
for i, txt in enumerate(ms_std1):
    plt.annotate(f"{txt}", (padding1[i], ms_std1[i]), textcoords="offset points", xytext=(0,10), ha='center', color='b')

# 在點上標註數值 (自動標註 ms2)
for i, txt in enumerate(ms_std2):
    plt.annotate(f"{txt}", (padding2[i], ms_std2[i]), textcoords="offset points", xytext=(0,-15), ha='center', color='r')
'''
# 標註 Me1 的 pac
for i, val in enumerate(pac2):
    plt.annotate(f'pac:{val}', (padding2[i], ms_std2[i]), textcoords="offset points", 
                 xytext=(0, 10), ha='center', color='r', fontweight='bold')

# 標註 Me2 的 pac
for i, val in enumerate(pac3):
    plt.annotate(f'pac:{val}', (padding3[i], ms_std3[i]), textcoords="offset points", 
                 xytext=(0, 10), ha='center', color='g', fontweight='bold')

# 標註 Me3 的 pac
for i, val in enumerate(pac4):
    plt.annotate(f'pac:{val}', (padding4[i], ms_std4[i]), textcoords="offset points", 
                 xytext=(0, -15), ha='center', color='b')
'''
plt.tight_layout()
plt.show()