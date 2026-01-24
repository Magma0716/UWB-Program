import pandas as pd
import matplotlib.pyplot as plt

'''
seq: rounded delay
distance_m: distance
interval_ms: raw delay
pad_inferred_from_data: padding
'''

# read data
df = pd.read_csv(r'.\Encryption_YP\UWB_Reports\範例_UWB_Result_ENC_ON_IV_UNK_PAD0_20260115-213740.csv')

# pre-data
dist = df['distance_m']
distAvg = dist.mean()
distStd = dist.std()

intv = df['interval_ms']
intvAvg = intv.mean()
intvStd = intv.std()

# draw plot
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# left chart
ax1.plot(
    df.index, dist, 
    color='#6441ff', marker='o', markersize='4', alpha=0.7, linewidth=1
)
ax1.axhline(
    distAvg, 
    color='red', linestyle='--', label=f'Avg: {distAvg:.4f} m'
)

ax1.set_title(f"Distance\nAvg={distAvg:.4f} m | Std={distStd:.4f} m | DataCount={len(df)}")
ax1.set_ylabel("Measured Distance (m)")
ax1.set_xlabel("Sample Index")
ax1.grid(True, which='both', linestyle='-', alpha=0.2)
ax1.legend(loc='upper right')

# right chart
ax2.plot(
    df.index, intv, 
    color='#ffb347', marker='x', markersize=4, alpha=0.8, linewidth=1
)
ax2.axhline(
    intvAvg, 
    color='red', linestyle='--', label=f'Avg: {intvAvg:.1f} ms'
)

ax2.set_title(f"System Load / Inter-arrival Time\nAvg={intvAvg:.1f} ms | Std={intvStd:.1f} ms | DataCount={len(df)}")
ax2.set_ylabel("Inter-arrival Time (ms)")
ax2.set_xlabel("Sample Index")
ax2.grid(True, which='both', linestyle='-', alpha=0.2)
ax2.legend(loc='upper right')

plt.tight_layout()
plt.show()