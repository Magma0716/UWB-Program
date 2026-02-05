import pandas as pd
import serial
import matplotlib.pyplot as plt
import time
from datetime import datetime

'''
seq: rounded delay
distance_m: distance
interval_ms: raw delay
pad_inferred_from_data: padding
'''

'''
no encryption 0 padding - 

'''


# connect
PORT = 'COM4'
BAUD_RATE = 115200
DATA_LIMIT = 30

data = []

ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
print('connect to uwb...')
time.sleep(2)

print('start receive date...')

count = 1
packageLoss = 0
while len(data) < DATA_LIMIT:
    if ser.in_waiting > 0:
        line = ser.readline().decode('utf-8').strip()
        
        if line:
            arr = line.split(',')
            print(f'{count} -> {arr}')
            if len(arr) == 3 and arr[0] == 'DATA':
                try:
                    data.append({
                        'dis': float(arr[2]),
                        'ms':  float(arr[1]) / 1000
                    })
                    
                    if float(arr[1]) / 1000 > 150:
                        packageLoss += 1
                    
                    count += 1
                except:
                    print('can\'t received')
                    continue
ser.close()
print('receive finish!!')
    
# read data
#df = pd.read_csv(r'.\Encryption_YP\UWB_Reports\範例_UWB_Result_ENC_ON_IV_UNK_PAD0_20260115-213740.csv')
df = pd.DataFrame(data)

# pre-data
dist = df['dis']
distAvg = dist.mean()
distStd = dist.std()

intv = df['ms']
intvAvg = intv.mean()
intvStd = intv.std()

# draw plot
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# left chart
ax1.plot(
    df.index, dist, 
    color="#5441ff", marker='o', markersize='4', alpha=0.7, linewidth=1
)
ax1.axhline(
    distAvg, 
    color='red', linestyle='--', label=f'Avg: {distAvg:.4f} m'
)

ax1.set_title(f"Distance\nAvg={distAvg:.4f} m | Std={distStd:.4f} m | N={len(df)}")
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

ax2.set_title(f"System Load / Inter-arrival Time\nAvg={intvAvg:.1f} ms | Std={intvStd:.1f} ms | N={len(df)} | Packet loss={packageLoss}")
ax2.set_ylabel("Inter-arrival Time (ms)")
ax2.set_xlabel("Sample Index")
ax2.grid(True, which='both', linestyle='-', alpha=0.2)
ax2.legend(loc='upper right')

plt.tight_layout()
padding = '0'
date = str(datetime.now().strftime('%H%M'))
plt.savefig(f'.\\Encryption_YP\\UWB_Reports\\Encryption_{padding}padding_{date}.png', dpi=300)