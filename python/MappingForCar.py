# rom digi.xbee.devices import XBeeDevice
import serial
import threading
import queue
import math
import time
import tkinter as tk
from tkinter import ttk

PORT = "COM17"
BAUD = 9600

V_IN_S = 18.3
V_CM_S = V_IN_S * 2.54

CANVAS_W, CANVAS_H = 1400, 900
SCALE = .65
ORIGIN_X = CANVAS_W // 6
ORIGIN_Y = CANVAS_H // 2
MAX_POINTS = 2500

q = queue.Queue()

def reader(ser):
    while True:
        try:
            line = ser.readline()
            if line:
                text = line.decode(errors="replace").strip()
                q.put(("LINE", text))
        except Exception as e:
            q.put(("ERR", str(e)))
            break


class MapApp:
    def __init__(self, root, ser):
        self.root = root
        self.ser = ser

        root.title("Arduino Robot Map (DRIVE_START/STOP integration)")

        self.canvas = tk.Canvas(root, width=CANVAS_W, height=CANVAS_H, bg="white")
        self.canvas.grid(row=0, column=0, sticky="nsew")

        self.status = ttk.Label(root, text="Connected. Waiting for data...", anchor="w")
        self.status.grid(row=1, column=0, sticky="ew", padx=6)

        self.cmd_entry = ttk.Entry(root)
        self.cmd_entry.grid(row=2, column=0, sticky="ew", padx=6, pady=6)
        self.cmd_entry.bind("<Return>", self.send_command)

        root.grid_rowconfigure(0, weight=1)
        root.grid_columnconfigure(0, weight=1)

        self.x = 0.0
        self.y = 0.0
        self.heading = 0.0

        # motion integration state
        self.moving = False
        self.last_move_time = time.time()

        self.last_range_cm = None

        # NEW: last temperature (assumed Fahrenheit if Arduino sends TEMP,<value in F>)
        self.last_temp = None

        self.points = []          # obstacle dots (existing)
        self.temp_labels = []     # NEW: temp labels we draw at STOP
        self.robot_dot = None

        # >>> ADDED (trail): remember last position + store trail segments
        self.prev_x = self.x
        self.prev_y = self.y
        self.trail_lines = []
        # <<< ADDED (trail)

        self.draw_axes()
        self.root.after(30, self.tick)

    def draw_axes(self):
        self.canvas.create_line(0, ORIGIN_Y, CANVAS_W, ORIGIN_Y)
        self.canvas.create_line(ORIGIN_X, 0, ORIGIN_X, CANVAS_H)

    def world_to_canvas(self, x_cm, y_cm):
        cx = ORIGIN_X + x_cm * SCALE
        cy = ORIGIN_Y - y_cm * SCALE
        return cx, cy

    def draw_robot(self):
        cx, cy = self.world_to_canvas(self.x, self.y)
        r = 4
        if self.robot_dot:
            self.canvas.delete(self.robot_dot)
        self.robot_dot = self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r)

    # >>> ADDED (trail): draw a segment from last pos to new pos
    def draw_trail_segment(self, x0, y0, x1, y1):
        cx0, cy0 = self.world_to_canvas(x0, y0)
        cx1, cy1 = self.world_to_canvas(x1, y1)
        seg = self.canvas.create_line(cx0, cy0, cx1, cy1)
        self.trail_lines.append(seg)

        # keep bounded (so it doesn't grow forever)
        if len(self.trail_lines) > MAX_POINTS:
            old = self.trail_lines.pop(0)
            self.canvas.delete(old)
    # <<< ADDED (trail)

    def plot_obstacle_from_range(self):
        if self.last_range_cm is None:
            return
        dist = self.last_range_cm
        if dist <= 0:
            return

        ox = self.x + dist * math.cos(self.heading)
        oy = self.y + dist * math.sin(self.heading)

        cx, cy = self.world_to_canvas(ox, oy)
        r = 2
        dot = self.canvas.create_oval(cx - r, cy - r, cx + r, cy + r)
        self.points.append(dot)

        if len(self.points) > MAX_POINTS:
            old = self.points.pop(0)
            self.canvas.delete(old)

    # NEW: label temperature at the robot STOP position (robot location, not obstacle point)
    def label_temp_at_robot(self):
        if self.last_temp is None:
            return

        cx, cy = self.world_to_canvas(self.x, self.y)

        # Offset so the text doesn't overlap the robot dot
        label = self.canvas.create_text(
            cx + 10, cy - 10,
            text=f"{self.last_temp:.1f}F",
            anchor="nw"
        )
        self.temp_labels.append(label)

        # keep bounded
        if len(self.temp_labels) > MAX_POINTS:
            old = self.temp_labels.pop(0)
            self.canvas.delete(old)

    def handle_line(self, text: str):
        if text.startswith("RANGE,"):
            try:
                self.last_range_cm = float(text.split(",")[1])
            except:
                pass
            return

        # NEW: parse temperature lines like "TEMP,72.3"
        if text.startswith("TEMP,"):
            try:
                self.last_temp = float(text.split(",")[1])
            except:
                pass
            return

        if text.startswith("EV,"):
            parts = text.split(",")
            ev = parts[1] if len(parts) > 1 else ""

            if ev == "TURN_R" and len(parts) >= 3:
                deg = float(parts[2])
                self.heading -= math.radians(deg)
                return

            if ev == "TURN_L" and len(parts) >= 3:
                deg = float(parts[2])
                self.heading += math.radians(deg)
                return

            if ev == "TURN_180" and len(parts) >= 3:
                deg = float(parts[2])
                self.heading += math.radians(deg)
                return

            if ev == "DRIVE_START":
                self.moving = True
                self.last_move_time = time.time()
                return

            if ev == "STOP":
                self.moving = False
                self.plot_obstacle_from_range()   # existing behavior (obstacle dot)
                self.label_temp_at_robot()        # NEW behavior (temp at robot stop)
                return

            return

    def tick(self):
        now = time.time()
        dt = now - self.last_move_time
        self.last_move_time = now

        if self.moving:
            # >>> ADDED (trail): save old position
            old_x, old_y = self.x, self.y
            # <<< ADDED (trail)

            d_cm = V_CM_S * dt
            self.x += d_cm * math.cos(self.heading)
            self.y += d_cm * math.sin(self.heading)

            # >>> ADDED (trail): draw segment + update prev
            self.draw_trail_segment(old_x, old_y, self.x, self.y)
            self.prev_x, self.prev_y = self.x, self.y
            # <<< ADDED (trail)

        while True:
            try:
                kind, payload = q.get_nowait()
            except queue.Empty:
                break

            if kind == "ERR":
                self.status.config(text=f"Serial error: {payload}")
                break

            self.handle_line(payload)

        self.draw_robot()

        hdg_deg = (math.degrees(self.heading) % 360.0)
        rng = self.last_range_cm if self.last_range_cm is not None else float("nan")
        tmp = self.last_temp if self.last_temp is not None else float("nan")

        self.status.config(
            text=(
                f"x={self.x:.1f}cm  y={self.y:.1f}cm  heading={hdg_deg:.1f}°  "
                f"range={rng:.1f}cm  temp={tmp:.1f}F  points={len(self.points)}  "
                f"moving={self.moving}"
            )
        )

        self.root.after(30, self.tick)

    def send_command(self, event=None):
        msg = self.cmd_entry.get().strip()
        self.cmd_entry.delete(0, tk.END)

        if not msg:
            return
        if msg.lower() == "quit":
            self.root.destroy()
            return

        try:
            self.ser.write((msg + "\n").encode())
        except Exception as e:
            self.status.config(text=f"Send error: {e}")


def main():
    megaBoard = serial.Serial(PORT, BAUD, timeout=0.1)

    t = threading.Thread(target=reader, args=(megaBoard,), daemon=True)
    t.start()

    root = tk.Tk()
    app = MapApp(root, megaBoard)
    root.mainloop()

    megaBoard.close()
    print("Serial port closed.")


if __name__ == "__main__":
    main()