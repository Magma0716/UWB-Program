import time
import turtle
import math, cmath
import socket
import json

# ==========================
# UDP Setting
# ==========================
UDP_IP = "10.238.7.37" # 需要改成自己的電腦 IP (cmd -> ipconfig)
UDP_PORT = 8001
print(f"*** UDP listening on {UDP_IP}:{UDP_PORT} ***")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.settimeout(0.5)

# ==========================
# Coordination and Scale Setting
# ==========================
distance_A1_A2 = 2
MeterToPixel = 200.0
range_offset = 0.9

CENTER_X = -250
CENTER_Y = 150

def init_screen(width=600, height=800, t=turtle):
    t.setup(width, height)
    t.tracer(False)
    t.hideturtle()
    t.speed(0)

def init_turtle(t=turtle):
    t.hideturtle()
    t.speed(0)

def draw_cycle(x, y, r, color="black", t=turtle):
    t.pencolor(color)
    t.up()
    t.goto(x, y - r)
    t.setheading(0)
    t.down()
    t.circle(r)
    t.up()

def fill_cycle(x, y, r, color="black", t=turtle):
    t.up()
    t.goto(x, y)
    t.dot(r, color)
    t.up()

def write_txt(x, y, txt, color="black", t=turtle, f=('Arial', 12, 'normal')):
    t.pencolor(color)
    t.up()
    t.goto(x, y)
    t.write(txt, move=False, align='left', font=f)
    t.up()

def draw_rect(x, y, w, h, color="black", t=turtle):
    t.pencolor(color)
    t.up()
    t.goto(x, y)
    t.down()
    for _ in range(2):
        t.forward(w)
        t.left(90)
        t.forward(h)
        t.left(90)
    t.up()

def fill_rect(x, y, w, h, color=("black", "black"), t=turtle):
    t.begin_fill()
    draw_rect(x, y, w, h, color, t)
    t.end_fill()

def clean(t=turtle):
    t.clear()

# ==========================
# Draw UI
# ==========================
def draw_ui(t):
    write_txt(-300, 350, "UWB Positioning", "black",  t, f=('Arial', 32, 'normal'))
    fill_rect(-400, 300, 800, 40, "black", t)
    write_txt(-50, 305, "WALL", "yellow",  t, f=('Arial', 24, 'normal'))

def draw_anchor(x, y, txt, range, t):
    r = 20
    fill_cycle(x, y, r, "green", t)
    write_txt(x + r, y, f"{txt} : {range} M",
              "black",  t, f=('Arial', 16, 'normal'))

def draw_tag(x, y, txt, t):
    pos_x = CENTER_X + int(x * MeterToPixel)
    pos_y = CENTER_Y - int(y * MeterToPixel)
    r = 20
    fill_cycle(pos_x, pos_y, r, "blue", t)
    write_txt(pos_x, pos_y, f"{txt}({x},{y})",
              "black",  t, f=('Arial', 16, 'normal'))

# ==========================
# Positioning Algorithm
# ==========================
def tag_pos1(a):
    return 0, round(a, 2)

def tag_pos2(a, b, c):
    cosA = (b**2 + c**2 - a**2) / (2 * b * c)
    x = b * cosA
    y = b * cmath.sqrt(1 - cosA**2)
    
    return round(x.real, 2), round(y.real, 2)

def tag_pos3(d1, d2, d3, x1, x2, x3, y1, y2, y3):
    a1 = 2 * (x1 - x2)
    b1 = 2 * (y1 - y2)
    c1 = d2**2 - d1**2 + x1**2 + y1**2 - x2**2 - y2**2
    a2 = 2 * (x1 - x3)
    b2 = 2 * (y1 - y3)
    c2 = d3**2 - d1**2 + x1**2 + y1**2 - x3**2 - y3**2
    x = (c1 * b2 - c2 * b1) / (a1 * b2 - a2 * b1)
    y = (a1 * c2 - a2 * c1) / (a1 * b2 - a2 * b1)
    return round(x, 2), round(y, 2)

# ==========================
# Main Program
# ==========================
def main():
    init_screen()
    
    t_ui = turtle.Turtle()
    t_tag = turtle.Turtle() 
    t_a1 = turtle.Turtle()
    t_a2 = turtle.Turtle()
    t_a3 = turtle.Turtle()
    
    init_turtle(t_ui)
    init_turtle(t_tag)
    init_turtle(t_a1)
    init_turtle(t_a2)
    init_turtle(t_a3)

    draw_ui(t_ui)
    
    while True:
        try:
            # receive data through the UDP
            data, address = sock.recvfrom(4096)
        except socket.timeout:
            data = None

        #data = '{"links":[{"A":"1785","R":"1.5"},{"A":"1786","R":"1.5"},{"A":"1787","R":"1.5"}]}'
        
        if data:
            print(f"Received message: {data} from {address}")

            try:
                List = json.loads(data)["links"]
            except Exception as e:
                print("JSON decode error:", e)
                List = []

            Positioning = 0
            # CENTER_X = -250
            # CENTER_Y = 150
            for pos in List:
                Aid = pos["A"]
                Range = max(0, (float(pos["R"])-0.78)*(5/6)) # 誤差
                # Range = max((0.9972 * float(pos["R"]) * 1000 - 613.42) / 1000, 0) # 誤差
                Range = round(Range,2)
                if Aid == "1785":
                    t_a1.clear()
                    draw_anchor(CENTER_X, CENTER_Y, "A1785(0, 0)", Range, t_a1)
                    draw_cycle(CENTER_X, CENTER_Y, Range * MeterToPixel, "black", t_a1)
                    d1 = Range
                    Positioning += 1
                    
                elif Aid == "1786":
                    t_a2.clear()
                    draw_anchor(CENTER_X + MeterToPixel * distance_A1_A2, 
                                CENTER_Y,
                                f"A1786({distance_A1_A2}, 0)", Range, t_a2)
                    draw_cycle(CENTER_X + MeterToPixel * distance_A1_A2, CENTER_Y, Range * MeterToPixel, "black", t_a2)
                    d2 = Range
                    Positioning += 1
                    
                elif Aid == "1787":
                    t_a3.clear()
                    draw_anchor(CENTER_X + MeterToPixel * distance_A1_A2 / 2, 
                                CENTER_Y - MeterToPixel * (math.sqrt(3) / 2 * distance_A1_A2),
                                f"A1787({distance_A1_A2/2:.2f}, {-distance_A1_A2*math.sqrt(3)/2:.2f})", Range, t_a3)
                    draw_cycle(CENTER_X + MeterToPixel * distance_A1_A2 / 2, 
                               CENTER_Y - MeterToPixel * (math.sqrt(3) / 2 * distance_A1_A2), 
                               Range * MeterToPixel, "black", t_a3)
                    d3 = Range
                    Positioning += 1
            
            # --- Positioning ---
            if Positioning == 1:
                x, y = tag_pos1(d1)
            elif Positioning == 2:
                x, y = tag_pos2(d2, d1, distance_A1_A2)
            elif Positioning == 3:
                x1, x2, x3 = 0, distance_A1_A2, distance_A1_A2 / 2
                y1, y2, y3 = 0, 0, distance_A1_A2 * math.sqrt(3) / 2
                x, y = tag_pos3(d1, d2 ,d3, x1, x2, x3, y1, y2, y3)
            else:
                turtle.update()
                continue 
            
            clean(t_tag)
            draw_tag(x, y, "TAG", t_tag)
            print(f"TAG Position: ({x:.2f}, {y:.2f})")
            
        turtle.update()
        time.sleep(0.01)

# ==========================
# Into Main Point
# ==========================
if __name__ == '__main__':
    main()