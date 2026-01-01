import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

def hex_to_bytes(hex_string):
    hex_string = "".join(hex_string.split())
    return [int(hex_string[i:i+2], 16) for i in range(0, len(hex_string), 2)]

def get_packet_structure(bytes_list):
    if not bytes_list: return "Empty", []
    
    # 1. Blink
    if bytes_list[0] == 0xC5:
        return "Blink", [("Header", 1), ("Src Long Addr", 8), ("Src Short Addr", 2), ("CRC", 2)]

    # 2. Ranging 系列 (41 88 ...)
    if len(bytes_list) >= 2 and bytes_list[0] == 0x41 and bytes_list[1] == 0x88:
        func_code = None
        for i in range(2, min(len(bytes_list), 40)):
            if bytes_list[i] in [0x10, 0xE0, 0xE1, 0xE2, 0xE3, 0xFF]:
                func_code = bytes_list[i]
                break
        
        if func_code == 0x10:
            return "Ranging_Init", [("Header", 2), ("Seq", 1), ("PAN ID", 2), ("Dest Addr(L)", 8), ("Src Addr(S)", 2), ("Func(10)", 1), ("CRC", 2)]
        elif func_code == 0xE0:
            return "Poll", [("Header", 2), ("Seq", 1), ("PAN ID", 2), ("Dest(S)", 2), ("Src(S)", 2), ("Func(E0)", 1), ("Anchor Num", 1), ("Anchor", 2), ("ReplyTime", 2), ("CRC", 2)]
        elif func_code == 0xE1:
            return "Poll_ACK", [("Header", 2), ("Seq", 1), ("PAN ID", 2), ("Dest(S)", 2), ("Src(S)", 2), ("Func(E1)", 1), ("CRC", 2)]
        elif func_code == 0xE2:
            return "Range", [("Header", 2), ("Seq", 1), ("PAN ID", 2), ("Dest(S)", 2), ("Src(S)", 2), ("Func(E2)", 1), ("Anchor Num", 1), ("tPollSent", 5), ("tPollAck", 5), ("tRangeSent", 5), ("CRC", 2)]
        elif func_code == 0xE3:
            return "Range_Report", [("Header", 2), ("Seq", 1), ("PAN ID", 2), ("Dest(S)", 2), ("Src(S)", 2), ("Func(E3)", 1), ("Range(m)", 4), ("tPollSent", 4), ("CRC", 2)]
        elif func_code == 0xFF:
            return "Range_Failed", [("Header", 2), ("Seq", 1), ("Error", len(bytes_list)-3)]

    return "Unknown", [("Data", len(bytes_list))]

def draw_packet(hex_string):
    bytes_list = hex_to_bytes(hex_string)
    packet_type, structure = get_packet_structure(bytes_list)
    
    colors = {
        "Header": "#D1E8FF", "Seq": "#E2F0D9", "PAN": "#D9EAD3",
        "Dest": "#FFF2CC", "Src": "#E2F0D9", "Func": "#FBE5D6",
        "CRC": "#FFD9D9", "tPoll": "#F8F9FA", "tRange": "#F8F9FA"
    }

    # 調整畫布大小，增加垂直空間處理交錯標籤
    fig, ax = plt.subplots(figsize=(18, 5))
    x = 0
    idx = 0
    
    # 計算視覺寬度 (設定最小寬度避免文字擠壓)
    min_width = 1.8 
    
    for i, (name, length) in enumerate(structure):
        # 視覺上的寬度，如果實際 byte 太短，給它一點 buffer
        v_width = max(length, min_width)
        
        # 匹配顏色
        color = "#FFFFFF"
        for key in colors:
            if key in name: color = colors[key]; break
        
        # 繪製矩形
        rect = Rectangle((x, 2), v_width, 1.5, facecolor=color, edgecolor="black", linewidth=1.2)
        ax.add_patch(rect)
        
        # 1. 處理標題 (奇偶交錯顯示在上方，避免碰撞)
        y_offset = 4.2 if i % 2 == 0 else 3.7
        ax.text(x + v_width/2, y_offset, name, ha="center", va="bottom", fontsize=9, fontweight='bold')
        
        # 2. 處理數據 (如果數據太長，在畫面上換行)
        data_segment = bytes_list[idx : idx + length]
        if length > 3:
            # 每 3 個 byte 換一行顯示
            hex_text = "\n".join([" ".join(f"{b:02X}" for b in data_segment[j:j+3]) for j in range(0, len(data_segment), 3)])
        else:
            hex_text = " ".join(f"{b:02X}" for b in data_segment)
            
        ax.text(x + v_width/2, 2.75, hex_text, ha="center", va="center", fontsize=9, family='monospace')
        
        # 3. 處理下方 Bytes 標記
        ax.text(x + v_width/2, 1.8, f"{length}B", ha="center", va="top", fontsize=8, color="gray")
        
        x += v_width
        idx += length

    ax.set_xlim(-1, x + 1)
    ax.set_ylim(0, 6)
    ax.set_title(f"UWB Frame Analysis: {packet_type}", fontsize=15, pad=30)
    ax.axis("off")
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    print("提示：請輸入 16 進位字串，例如：C5 01 02 03 04 05 06 07 08 09 0A BB CC")
    hex_input = input("請輸入封包數據: ").strip().rstrip("0")
    word = ""
    for i in range(len(hex_input)//2):
        word += hex_input[2*i] + hex_input[2*i+1] + " "
    word = word[:-1:]
    
    draw_packet(word)