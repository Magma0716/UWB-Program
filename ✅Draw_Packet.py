import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

# Utils
def hex_to_bytes(hex_string):
    hex_string = "".join(hex_string.split())
    return [int(hex_string[i:i+2], 16) for i in range(0, len(hex_string), 2)]

def cut(start_lua, length):
    """
    Lua: arr[1] 開始
    Python: bytes[0] 開始
    """
    py_start = start_lua - 1
    return py_start, length


# Packet structure
def get_packet_structure(b):
    if not b:
        return "Empty", []
    struct = []

    # Blink
    if b[0] == 0xC5:
        struct += [
            ("Mac\nHeader",      1, "#FEFDE8"),
            ("Seq\nNum",      1, "#FEFDE8"),
            ("Tag\nLongAdr",  8, "#FEFDE8"),
            ("Tag\nShortAdr", 2, "#FEFDE8"),
            ("GarbageData", 33,"#F2F2F2"),
            ("CRC",         2, "#FEE8E8")
        ]
        return "Blink", struct

    # Ranging_init (need to fix)
    if len(b) >= 2 and b[0] == 0x41 and b[1] == 0x8C:
        struct += [
            ("Header", 2),
            ("SeqNum", 1),
            ("PanID", 2),
            ("TagLongAdr", 8),
            ("AncShortAdr", 2),
            ("Function", 1),
            ("CRC", 2),
        ]
        remain = len(b) - sum(l for _, l in struct)
        if remain > 0:
            struct.append(("GarbageData", remain))
        return "Ranging_init", struct

    # Short MAC
    if len(b) >= 2 and b[0] == 0x41 and b[1] == 0x88:

        func = b[9]
        encryption = b[10]
        
        # Poll
        if func == 0x00:
            struct += [
                ("Mac\nHeader",   2,  "#FEFDE8"),
                ("Seq\nNum",      1,  "#FEFDE8"),
                ("PanID",         2,  "#FEFDE8"),
                ("Broadcast",     2,  "#FEFDE8"),
                ("Tag\nShortAdr", 2,  "#FEFDE8"),
                ("Func",          1,  "#ECFEE8"),
                ("Anc\nNum",      1,  "#ECFEE8"),
                ("Anc\nShortAdr", 2,  "#ECFEE8"),
                ("Anc\nReply",    2,  "#ECFEE8"),
                ("GarbageData",   30, "#F2F2F2"),
                ("CRC",           2,  "#FEE8E8")
            ]
            return "Poll", struct

        # Poll_Ack (need to fix)
        if func == 0x01:
            struct += [
                ("Header", 2),
                ("SeqNum", 1),
                ("PanID", 2),
                ("TagShortAdr", 2),
                ("AncShortAdr", 2),
                ("Function", 1),
                ("CRC", 2),
            ]
            remain = len(b) - sum(l for _, l in struct)
            if remain:
                struct.append(("GarbageData", remain))
            return "Poll_Ack", struct

        # Range / Final
        if func == 0x02:
            struct += [
                ("Mac\nHeader",     2, "#FEFDE8"),
                ("Seq\nNum",        1, "#FEFDE8"),
                ("PanID",           2, "#FEFDE8"),
                ("Broadcast",       2, "#FEFDE8"),
                ("Tag\nShortAdr",   2, "#FEFDE8"),
                ("Func",            1, "#ECFEE8"),
                ("Anc\nNum",        1, "#ECFEE8"),
                ("Anc\nShortAdr",   2, "#ECFEE8"),
                ("Tag\nPollSent",   5, "#ECFEE8"),
                ("Tag\nPollAckRec", 5, "#ECFEE8"),
                ("Anc\nRangeSent",  5, "#ECFEE8"),
                ("GarbageData",     17,"#F2F2F2"),
                ("CRC",             2, "#FEE8E8")
            ]
            return "Range/Final", struct

        # ---------- Range_Report ----------
        
        # 未加密
        if func == 0x03 and encryption == 0x00:
            struct += [
                ("Mac\nHeader",         2, "#FEFDE8"),
                ("Seq\nNum",            1, "#FEFDE8"),
                ("PanID",               2, "#FEFDE8"),
                ("Broadcast",           2, "#FEFDE8"),
                ("Tag\nShortAdr",       2, "#FEFDE8"),
                ("Func",                1, "#ECFEE8"),
                ("Encryption\n(False)", 1, "#FDE8FE"),
                ("Chiptext",            4, "#FDE8FE"),
                ("CRC",                 2, "#FEE8E8")
            ]
            return "Range_Report (Encryption - False)", struct
            
        # 加密
        if func == 0x03 and encryption == 0x01:
            struct += [
                ("Mac\nHeader",        2, "#FEFDE8"),
                ("Seq\nNum",           1, "#FEFDE8"),
                ("PanID",              2, "#FEFDE8"),
                ("Broadcast",          2, "#FEFDE8"),
                ("Tag\nShortAdr",      2, "#FEFDE8"),
                ("Func",               1, "#ECFEE8"),
                ("Encryption\n(True)", 1, "#FDE8FE"),
                ("IV",                 12,"#FDE8FE"),
                ("Tag",                16,"#FDE8FE"),
                ("Chiptext",           4, "#FDE8FE"),
                ("CRC",                2, "#FEE8E8")
            ]
            return "Range_Report (Encryption - True)", struct

        return "Range_Failed", [("Data", len(b))]

    return "Unknown", [("Data", len(b))]


# Drawing
def draw_packet(hex_string):
    b = hex_to_bytes(hex_string)
    pkt_type, struct = get_packet_structure(b)

    fig, ax = plt.subplots(figsize=(14, 4))
    x = 0
    idx = 0

    for name, length, color in struct:
        rect = Rectangle(
            (x, 1.84), length, 0.6,
            edgecolor="black", facecolor=color
        )
        ax.add_patch(rect)

        seg = b[idx:idx+length]
        text = " ".join(f"{v:02X}" for v in seg)

        ax.text(x + length/2, 2.5, name, ha="center", fontsize=5.5, weight="bold")
        ax.text(x + length/2, 2.1, text, ha="center", fontsize=8, family="monospace")
        ax.text(x + length/2, 1.7, f"{length}B", ha="center", fontsize=8, color="gray")

        x += max(length, 1)
        idx += length

    ax.set_xlim(0, x + 1)
    ax.set_ylim(0.5, 3.6)
    ax.set_title(f"UWB Packet: {pkt_type}", fontsize=14)
    ax.axis("off")
    plt.tight_layout()
    plt.show()

# Main
if __name__ == "__main__":
    Ihex = input("Hex Packet: ").strip()
    if len(Ihex) % 2:
        Ihex += "0"
    draw_packet(Ihex)
    
'''
41 88 77 CA DE A4 9C AA AA 03 01 2D 07 00 00 00 00 00 00 00 00 00 00 E2 9B 5B 75 E8 6E 4B E2 D8 38 8C 3F 5A 9F 26 62 2A 57 7D 24 6A 9D
41 88 77 CA DE A4 9C AA AA 03 00 2A 57 7D 24 6A 9D
'''