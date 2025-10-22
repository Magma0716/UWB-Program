
# 測試用程式

'''
# 送出端
import socket
import json
import time

UDP_IP = "192.168.0.108"
UDP_PORT = 8001     

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

data = {
    "links": [
        {"A": "1785", "R": "1.2"},
        {"A": "1786", "R": "1.1"}
    ]
}

while True:
    msg = json.dumps(data)
    sock.sendto(msg.encode('utf-8'), (UDP_IP, UDP_PORT))
    print("Sent:", msg)
    time.sleep(1)

# 接收端
import socket
import json

UDP_IP = "10.238.7.37"
UDP_PORT = 8001

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Listening on {UDP_PORT}...")

while True:
    data, addr = sock.recvfrom(4096)
    print(f"data:{data}")
    
    if data:
        json_data = json.loads(data.decode('utf-8').strip())
        print(f"Raw data from {json_data}")
'''        
    


