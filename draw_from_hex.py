import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch

HEX_FILE = "uwb_final.hex"
OUT_PNG = "uwb_final_packet_diagram.png"

def read_hex_file(path: str) -> bytes:
    s = Path(path).read_text(encoding="utf-8", errors="ignore")
    s = re.sub(r"[^0-9a-fA-F]", "", s)  # 只保留 0-9a-f
    if len(s) % 2 != 0:
        raise ValueError("Hex 長度不是偶數，請確認內容沒有被截斷。")
    return bytes.fromhex(s)

def u16be(b: bytes, off: int) -> int:
    return (b[off] << 8) | b[off+1]

def parse_ipv4_udp_payload(frame: bytes) -> bytes:
    """
    你的 hex 是整包 (可能含 loopback / padding)。
    我們做法：在 bytes 裡找 IPv4 header 開頭 0x45 (version=4, IHL=5)。
    再用 IP header length + UDP length 切出 UDP payload。
    """
    # 找 0x45 ?? (IPv4 v4 + IHL=5)
    idx = frame.find(b"\x45")
    if idx < 0 or idx + 20 > len(frame):
        raise ValueError("找不到 IPv4 header (0x45...)，請確認 hex 是完整封包。")

    ip = frame[idx:]
    ihl = (ip[0] & 0x0F) * 4
    if len(ip) < ihl + 8:
        raise ValueError("IPv4 header 不完整。")

    proto = ip[9]
    if proto != 17:
        raise ValueError(f"IP protocol 不是 UDP (17)，而是 {proto}。")

    udp_off = ihl
    udp_len = u16be(ip, udp_off + 4)  # UDP length = header(8)+payload
    if udp_len < 8:
        raise ValueError("UDP length 不合理。")

    udp_payload_off = udp_off + 8
    udp_payload_len = udp_len - 8
    if udp_payload_off + udp_payload_len > len(ip):
        # 有些抓包可能後面不足，做安全截斷
        udp_payload_len = max(0, len(ip) - udp_payload_off)

    return ip[udp_payload_off: udp_payload_off + udp_payload_len]

def parse_uwb(payload: bytes):
    """
    依你論文格式：Header 9 bytes + Payload (func/devnum/3 timestamps/CRC)
    若 payload 比預期短，能解析多少就解析多少。
    """
    if len(payload) < 11:
        raise ValueError(f"UWB payload 太短：{len(payload)} bytes（至少要 11）")

    # Header(9)
    fc1 = payload[0]
    fc2 = payload[1]
    seq = payload[2]
    pan = payload[3:5]          # little-endian 顯示時可反轉
    dst = payload[5:7]
    src = payload[7:9]

    # Payload basic
    func = payload[9]
    devnum = payload[10]

    # 後面 timestamps / crc：盡量切，不足就切到剩下
    off = 11
    def take(n):
        nonlocal off
        chunk = payload[off: off+n]
        off += len(chunk)
        return chunk

    timePollSent = take(5)
    timePollAckReceived = take(5)
    timeRangeSent = take(5)

    # CRC 最後 2 bytes（如果有）
    crc = b""
    if len(payload) >= 2:
        crc = payload[-2:]

    return {
        "fc1": fc1, "fc2": fc2, "seq": seq,
        "pan": pan, "dst": dst, "src": src,
        "func": func, "devnum": devnum,
        "timePollSent": timePollSent,
        "timePollAckReceived": timePollAckReceived,
        "timeRangeSent": timeRangeSent,
        "crc": crc,
        "payload_len": len(payload)
    }

def hx(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)

def draw_diagram(info: dict, out_png: str):
    # 欄位與長度（bytes）
    header_fields = [
        ("Frame Control", 2, f"{info['fc1']:02X} {info['fc2']:02X}"),
        ("Sequence\nNumber", 1, f"{info['seq']}"),
        ("PAN ID", 2, hx(info["pan"])),
        ("Destination\nShort Addr", 2, hx(info["dst"])),
        ("Source\nShort Addr", 2, hx(info["src"])),
    ]
    payload_fields = [
        ("Function", 1, f"{info['func']}"),
        ("Anchor\nCount", 1, f"{info['devnum']}"),
        ("timePollSent", 5, hx(info["timePollSent"])),
        ("timePollAck\nReceived", 5, hx(info["timePollAckReceived"])),
        ("timeRangeSent", 5, hx(info["timeRangeSent"])),
        ("CRC", 2, hx(info["crc"])),
    ]

    fig, ax = plt.subplots(figsize=(14, 5.4))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 60)
    ax.axis("off")

    def draw_row(y, title, fields):
        ax.text(50, y + 16, title, ha="center", va="center", fontsize=12)
        total = sum(size for _, size, _ in fields)
        unit = 90 / total
        x = 5

        # boxes
        for name, size, val in fields:
            w = size * unit
            ax.add_patch(Rectangle((x, y), w, 11, fill=False, linewidth=1.6))
            ax.text(x + w/2, y + 7.0, name, ha="center", va="center", fontsize=10)
            ax.text(x + w/2, y + 3.2, f"{size} B", ha="center", va="center", fontsize=9)
            ax.text(x + w/2, y + 1.1, val, ha="center", va="center", fontsize=8)
            x += w

        # length arrows
        x = 5
        y_mark = y - 4
        for _, size, _ in fields:
            w = size * unit
            arr = FancyArrowPatch((x, y_mark), (x + w, y_mark),
                                  arrowstyle="<->", mutation_scale=12, linewidth=1.2)
            ax.add_patch(arr)
            ax.text(x + w/2, y_mark + 2.0, f"{size}B", ha="center", va="bottom", fontsize=9)
            x += w

    ax.text(50, 56, f"UWB Final Packet Format (payload {info['payload_len']} bytes)", ha="center", fontsize=14)
    draw_row(37, "MAC Header", header_fields)
    draw_row(12, "RANGE / FINAL Payload", payload_fields)

    ax.add_patch(FancyArrowPatch((95, 42.5), (95, 17.5),
                                 arrowstyle='-|>', mutation_scale=14, linewidth=1.4))
    plt.savefig(out_png, dpi=300, bbox_inches="tight")
    plt.close(fig)

def main():
    frame = read_hex_file(HEX_FILE)
    udp_payload = parse_ipv4_udp_payload(frame)
    info = parse_uwb(udp_payload)

    print("UDP payload length:", len(udp_payload))
    print("UWB Header FC:", f"{info['fc1']:02X} {info['fc2']:02X}", "SEQ:", info["seq"])
    print("PAN:", hx(info["pan"]), "DST:", hx(info["dst"]), "SRC:", hx(info["src"]))
    print("FUNC:", info["func"], "DEVNUM:", info["devnum"])

    draw_diagram(info, OUT_PNG)
    print("Saved:", OUT_PNG)

if __name__ == "__main__":
    main()
