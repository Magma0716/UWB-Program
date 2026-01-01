import re
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch

# ===== 字體：避免中文變成 □□□□ =====
# Windows 常見可用字型：Microsoft JhengHei（微軟正黑體）
mpl.rcParams["font.family"] = "Microsoft JhengHei"
mpl.rcParams["axes.unicode_minus"] = False

# ===== 會自動找這些檔名（不用改副檔名也能跑）=====
HEX_FILE_CANDIDATES = [
    "uwb_final.hex",
    "uwb_final.hex.txt",
]

OUT_PNG = "uwb_thesis_layout_clean.png"


def read_hex_file_auto() -> bytes:
    for name in HEX_FILE_CANDIDATES:
        p = Path(name)
        if p.exists():
            s = p.read_text(encoding="utf-8", errors="ignore")
            s = re.sub(r"[^0-9a-fA-F]", "", s)
            if len(s) % 2 != 0:
                raise ValueError(f"Hex 長度不是偶數：{name} 可能被截斷。")
            return bytes.fromhex(s)
    raise FileNotFoundError(f"找不到 hex 檔，請確認同資料夾有：{', '.join(HEX_FILE_CANDIDATES)}")


def u16be(b: bytes, off: int) -> int:
    return (b[off] << 8) | b[off + 1]


def parse_ipv4_udp_payload(frame: bytes) -> bytes:
    # 找 IPv4 v4/IHL=5 開頭（0x45）
    idx = frame.find(b"\x45")
    if idx < 0:
        raise ValueError("找不到 IPv4 header (0x45...)，請確認 hex 內容是封包 (包含 IP/UDP)。")

    ip = frame[idx:]
    ihl = (ip[0] & 0x0F) * 4
    if len(ip) < ihl + 8:
        raise ValueError("IPv4/UDP header 不完整")

    if ip[9] != 17:
        raise ValueError("不是 UDP 封包 (Protocol != 17)")

    udp_off = ihl
    udp_len = u16be(ip, udp_off + 4)
    payload_off = udp_off + 8
    payload_len = max(0, udp_len - 8)

    if payload_off + payload_len > len(ip):
        payload_len = max(0, len(ip) - payload_off)

    return ip[payload_off : payload_off + payload_len]


def hx2(b2: bytes) -> str:
    return f"0x{b2[0]:02X} 0x{b2[1]:02X}"


def fit_font_for_width(w, base=12):
    # 格子越窄字越小，避免跑版/重疊
    if w < 8:
        return max(7, base - 5)
    if w < 12:
        return max(8, base - 4)
    if w < 16:
        return max(9, base - 3)
    if w < 20:
        return max(10, base - 2)
    return base


def draw():
    frame = read_hex_file_auto()
    uwb = parse_ipv4_udp_payload(frame)

    if len(uwb) < 9:
        raise ValueError(f"UWB payload 太短：{len(uwb)} bytes（至少要 9 bytes header）")

    # 依你 lua 的 header（9 bytes）
    fc1, fc2 = uwb[0], uwb[1]
    seq = uwb[2]
    pan = uwb[3:5]
    dst = uwb[5:7]
    src = uwb[7:9]

    # ===== 畫布 =====
    fig, ax = plt.subplots(figsize=(14, 5.0))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 60)
    ax.axis("off")

    def box(x, y, w, h, title, value_text):
        ax.add_patch(Rectangle((x, y), w, h, fill=False, linewidth=1.8))
        fs = fit_font_for_width(w, base=12)
        ax.text(x + w / 2, y + h * 0.62, title, ha="center", va="center", fontsize=fs)
        if value_text:
            ax.text(
                x + w / 2,
                y + h * 0.30,
                value_text,
                ha="center",
                va="center",
                fontsize=max(8, fs - 2),
            )

    def length_marker(x0, x1, y, text):
        arr = FancyArrowPatch(
            (x0, y),
            (x1, y),
            arrowstyle="<->",
            mutation_scale=12,
            linewidth=1.2,
        )
        ax.add_patch(arr)
        ax.text((x0 + x1) / 2, y + 1.8, text, ha="center", va="bottom", fontsize=10)

    # ===== 大標題（只留一個，避免重疊）=====
    ax.text(
        50,
        58,
        "MAC Header 與 RANGE Payload 封包格式",
        ha="center",
        va="center",
        fontsize=16,
    )

    x0 = 5
    total_w = 90
    h = 12

    # ===== MAC Header =====
    y_top = 40
    ax.text(50, y_top + h + 2.0, "MAC Header", ha="center", va="center", fontsize=12)

    header = [
        ("Frame Control", f"(0x{fc1:02X} 0x{fc2:02X})", 2),
        ("Sequence\nNumber", f"(0x{seq:02X})", 1),
        ("PAN ID", f"({hx2(pan)})", 2),
        ("Destination\nShort Addr", f"({hx2(dst)})", 2),
        ("Source\nShort Addr", f"({hx2(src)})", 2),
    ]
    unit = total_w / sum(b for _, _, b in header)

    x = x0
    for name, val, b in header:
        w = b * unit
        box(x, y_top, w, h, name, val)
        x += w

    # 只保留箭頭的 2B/1B/2B...
    y_mark = y_top - 4
    x = x0
    for _, _, b in header:
        w = b * unit
        length_marker(x, x + w, y_mark, f"{b}B")
        x += w

    # ===== RANGE Payload =====
    y2 = 14
    ax.text(50, y2 + h + 2.0, "RANGE Payload", ha="center", va="center", fontsize=12)

    payload = [
        ("Function", "(RANGE)", 1),
        ("Anchor\nCount", "", 1),
        ("timePollSent", "", 5),
        ("timePollAck\nReceived", "", 5),
        ("timeRangeSent", "", 5),
        ("CRC", "", 2),
    ]
    unit_p = total_w / sum(b for _, _, b in payload)

    x = x0
    for name, val, b in payload:
        w = b * unit_p
        box(x, y2, w, h, name, val)
        x += w

    # 只保留箭頭的 1B/1B/5B...
    y_mark2 = y2 - 4
    x = x0
    for _, _, b in payload:
        w = b * unit_p
        length_marker(x, x + w, y_mark2, f"{b}B")
        x += w

    # 右側連接箭頭
    ax.add_patch(
        FancyArrowPatch(
            (95, y_top + h / 2),
            (95, y2 + h / 2),
            arrowstyle="-|>",
            mutation_scale=14,
            linewidth=1.4,
        )
    )

    plt.savefig(OUT_PNG, dpi=300, bbox_inches="tight")
    plt.show()
    print("Saved:", OUT_PNG)


if __name__ == "__main__":
    draw()
