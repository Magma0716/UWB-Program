import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

def hex_to_bytes(hex_string):
    hex_string = "".join(hex_string.split())
    return [int(hex_string[i:i+2], 16) for i in range(0, len(hex_string), 2)]

def count_trailing_zeros(bytes_list, start_idx):
    count = 0
    idx = len(bytes_list) - 1
    while idx >= start_idx and bytes_list[idx] == 0:
        count += 1
        idx -= 1
    return count

def get_packet_structure(bytes_list):
    if not bytes_list: return "Empty", []
    
    # 1. Blink 
    if bytes_list[0] == 0xC5:
        return "Blink", [("Header", 1), ("Src Long Addr", 8), ("Src Short Addr", 2), ("CRC", 2)]

    # 2. 802.15.4 Short MAC (41 88)
    if len(bytes_list) >= 2 and bytes_list[0] == 0x41 and bytes_list[1] == 0x88:
        SHORT_MAC_LEN = 9
        if len(bytes_list) <= SHORT_MAC_LEN:
            return "Truncated MAC", [("MAC Header", len(bytes_list))]
        
        msgid = bytes_list[9]
        msg_map = {
            0x00: "Poll", 0xE0: "Poll",
            0x01: "Poll_ACK", 0xE1: "Poll_ACK",
            0x02: "Final", 0xE2: "Final",
            0x03: "Range_Report", 0xE3: "Range_Report",
            0x04: "Blink", 0x05: "Ranging_Init", 0xFF: "Range_Failed"
        }
        packet_type = msg_map.get(msgid, f"Unknown({msgid:02X})")

        structure = [
            ("MAC Header", 2), ("Seq", 1), ("PAN ID", 2), 
            ("Dest(S)", 2), ("Src(S)", 2), (f"MsgID({msgid:02X})", 1)
        ]

        if packet_type == "Range_Report":
            after_msgid = 10
            is_new = False
            if len(bytes_list) > after_msgid + 1:
                ver = bytes_list[after_msgid]
                plen = bytes_list[after_msgid + 1]
                if (ver == 0x00 or ver == 0x01) and (plen <= len(bytes_list) - (after_msgid + 2)):
                    is_new = True
            
            if is_new:
                ver = bytes_list[after_msgid]
                plen = bytes_list[after_msgid + 1]
                
                is_encrypted = (ver == 0x01)
                enc_label = f"Encryption: {'True' if is_encrypted else 'False'}"
                structure.append((enc_label, 1))
                structure.append((f"Plen({plen})", 1))
                
                payload_start = after_msgid + 2
                if is_encrypted:
                    structure.append(("IV", 12))
                    structure.append(("Tag", 16))
                    cipher_len = plen - 12 - 16
                    if cipher_len > 0:
                        structure.append(("payload", cipher_len))
                else:
                    structure.append(("Plaintext", plen))
                
                payload_end = payload_start + plen
                pad_count = count_trailing_zeros(bytes_list, payload_end)
                if pad_count > 0:
                    structure.append(("Padding", pad_count))
            else:
                structure.append(("Encryption: True (Legacy)", 1))
                structure.append(("IV", 12))
                structure.append(("Tag", 16))
                cip_start = after_msgid + 1 + 12 + 16
                pad_count = count_trailing_zeros(bytes_list, cip_start)
                cipher_len = len(bytes_list) - cip_start - pad_count
                if cipher_len > 0:
                    structure.append(("payload", cipher_len))
                if pad_count > 0:
                    structure.append(("Padding", pad_count))
            
            return "Range_Report", structure

        structure.append(("Payload", len(bytes_list) - 10))
        return packet_type, structure

    return "Unknown", [("Data", len(bytes_list))]

def draw_packet(hex_string):
    bytes_list = hex_to_bytes(hex_string)
    packet_type, structure = get_packet_structure(bytes_list)
    
    colors = {
        "Header": "#D1E8FF", "Seq": "#E2F0D9", "PAN": "#D9EAD3",
        "Dest": "#FFF2CC", "Src": "#E2F0D9", "MsgID": "#FBE5D6",
        "Encryption: True": "#C6EFCE",
        "Encryption: False": "#FFC7CE",
        "Plen": "#D1E8FF", "IV": "#E2F0D9",
        "Tag": "#FFF2CC", "Cipher": "#F8F9FA", "Plain": "#F8F9FA",
        "Padding": "#EEEEEE"
    }

    fig, ax = plt.subplots(figsize=(18, 5))
    x = 0
    idx = 0
    min_width = 2.0
    
    for i, (name, length) in enumerate(structure):
        v_width = max(length, min_width)
        color = "#FFFFFF"
        for key in colors:
            if key in name: color = colors[key]; break
        
        rect = Rectangle((x, 2), v_width, 1.5, facecolor=color, edgecolor="black", linewidth=1.2)
        ax.add_patch(rect)
        
        y_offset = 4.2 if i % 2 == 0 else 3.7
        ax.text(x + v_width/2, y_offset, name, ha="center", va="bottom", fontsize=9, fontweight='bold')
        
        data_segment = bytes_list[idx : idx + length]
        if length > 4:
            hex_text = "\n".join([" ".join(f"{b:02X}" for b in data_segment[j:j+4]) for j in range(0, len(data_segment), 4)])
        else:
            hex_text = " ".join(f"{b:02X}" for b in data_segment)
            
        ax.text(x + v_width/2, 2.75, hex_text, ha="center", va="center", fontsize=8, family='monospace')
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
    hex_input = input("請輸入封包數據 (Hex): ").strip().rstrip("0")
    if len(hex_input) % 2 != 0: hex_input += "0"
    draw_packet(hex_input)