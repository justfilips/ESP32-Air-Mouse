import serial  # For communicating with Arduino over a serial port
import time  # For delays, timestamps, and measuring intervals
import math  # For mathematical operations, e.g., atan2 and degrees
import subprocess  # For running system commands like shutdown or launching apps
import psutil  # For checking running processes and system info
import webbrowser  # For opening URLs in web browsers
from datetime import datetime  # For working with current date and time
import pytz  # For timezone handling
import os  # import the os module for working with file paths and directories
from comtypes import CLSCTX_ALL  # COM context constant for audio interface

# Mouse and keyboard control
from pynput.mouse import Controller as MouseController, Button  # move cursor & click buttons
from pynput.keyboard import Controller as KeyboardController, Key  # send keys & special keys

# Audio control
from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume  # get/set system volume
from comtypes import cast, POINTER  # required for working with COM interfaces
# --------------------------- Setup ---------------------------
mouse = MouseController()
keyboard = KeyboardController()
SERIAL_PORT = 'COM5'  # change this if needed
ser = serial.Serial(SERIAL_PORT, 38400)
time.sleep(2)  # wait for serial connection to stabilize

# Sensitivity & smoothing
sensitivity = 0.002
alpha = 0.15
smoothed_dx = 0
smoothed_dy = 0

# State tracking
prev_left = prev_right = prev_sw = prev_keyboardButton = 0
swcount = 0
press_start_time_double_Click = None
press_start_time_hold = None

# Joystick & zoom
last_zoom_time = time.time()
zoom_cooldown = 0.5
joy_threshold = 2

prev_joy_x = prev_joy_y = 0

# Tilt detection
upside_down_threshold = -8000
upside_down_detected = False
forward_tilt_threshold = 1000
forward_tilt_detected = False

# Shutdown timer
shutdown_mode = False
shutdown_timer_seconds = 0
shutdown_timer_running = False
joyx_shutdown_threshold = 5
sw_press_time = 0
joy_right_active = False
joy_left_active = False

# Mode 2 flags
bootupInCurrentModeTwo = True
setVolumeOnce = True

# Keyboard app path
keyboard_app_path = os.path.join(os.path.dirname(__file__), "..", "tools", "FreeVK.exe")

# --------------------------- Volume Control ---------------------------

devices = AudioUtilities.GetSpeakers()
volume_interface = devices.EndpointVolume  # directly use EndpointVolume

def get_volume():
    # Returns 0‑100%
    return int(volume_interface.GetMasterVolumeLevelScalar() * 100)

def set_volume(level):
    level = max(0, min(level, 100))
    volume_interface.SetMasterVolumeLevelScalar(level / 100, None)

volume_level = get_volume()
# --------------------------- Browser Helpers ---------------------------
def is_brave_running():
    for proc in psutil.process_iter(['name']):
        if proc.info['name'] and 'brave' in proc.info['name'].lower():
            return True
    return False

def open_in_brave(url):
    brave_path = r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"
    if not is_brave_running():
        subprocess.Popen([brave_path])
        time.sleep(2)
    webbrowser.register('brave', None, webbrowser.BackgroundBrowser(brave_path))
    webbrowser.get('brave').open_new_tab(url)

# --------------------------- Serial Command ---------------------------
def send_command(cmd):
    ser.write((cmd + '\n').encode())
    print(f"Sent: {cmd}")

# --------------------------- Initialize Time ---------------------------
latvia = pytz.timezone("Europe/Riga")
now = datetime.now(latvia)
current_time = now.strftime("%H:%M")
send_command(f"time:{current_time}")
last_sent_time = current_time

# --------------------------- Main Loop ---------------------------
while True:
    try:
        # Update time every minute
        now = datetime.now(latvia)
        current_time = now.strftime("%H:%M")
        if current_time != last_sent_time:
            send_command(f"time:{current_time}")
            last_sent_time = current_time

        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue

        parts = line.split(',')
        if len(parts) != 14:
            continue

        # Parse sensor/button values
        ax, ay, az, gx, gy, gz, joy_x, joy_y, left_click, right_click, sw, multiFunctionButton, redButton, potentiometerMode = map(int, parts)

        # --------------------------- Mode 1: Mouse/Browser Control ---------------------------
        if potentiometerMode == 1:
            bootupInCurrentModeTwo = False
            angle = math.degrees(math.atan2(joy_y, joy_x))

            # Scroll handling
            if joy_y > 2 and 60 <= angle <= 120:
                mouse.scroll(0, -1)
                send_command("scroll_up")
            elif joy_y < -2 and -120 <= angle <= -60:
                mouse.scroll(0, 1)
                send_command("scroll_down")

            # Zoom handling
            current_time_sec = time.time()
            if joy_x > joy_threshold and current_time_sec - last_zoom_time > zoom_cooldown and -30 <= angle <= 30:
                send_command("zoom_out")
                keyboard.press(Key.ctrl_l)
                keyboard.press('-')
                keyboard.release('-')
                keyboard.release(Key.ctrl_l)
                last_zoom_time = current_time_sec
            elif joy_x < -joy_threshold and current_time_sec - last_zoom_time > zoom_cooldown and (angle >= 150 or angle <= -150):
                send_command("zoom_in")
                keyboard.press(Key.ctrl_l)
                keyboard.press(Key.shift_l)
                keyboard.press('=')
                keyboard.release('=')
                keyboard.release(Key.shift_l)
                keyboard.release(Key.ctrl_l)
                last_zoom_time = current_time_sec

            # SW button handling: double click, hold
            if sw and not prev_sw:
                if swcount == 0:
                    swcount = 1
                    press_start_time_double_Click = current_time_sec
                    press_start_time_hold = current_time_sec
                elif swcount == 1:
                    if current_time_sec - press_start_time_double_Click <= 0.5:
                        send_command("open_tab")
                        keyboard.press(Key.ctrl)
                        keyboard.press('t')
                        keyboard.release('t')
                        keyboard.release(Key.ctrl)
                    swcount = 0
                    press_start_time_double_Click = None
                    press_start_time_hold = None
            elif sw and prev_sw:
                if press_start_time_hold and current_time_sec - press_start_time_hold > 1:
                    send_command("close_tab")
                    keyboard.press(Key.ctrl)
                    keyboard.press('w')
                    keyboard.release('w')
                    keyboard.release(Key.ctrl)
                    press_start_time_hold = None
            elif swcount == 1 and current_time_sec - press_start_time_double_Click > 0.5:
                swcount = 0
                press_start_time_double_Click = None
                press_start_time_hold = None
            prev_sw = sw

            # MultiFunctionButton: toggle FreeVK
            if multiFunctionButton == 1 and prev_keyboardButton == 0:
                keyboard_running = any(proc.info['name'] == 'FreeVK.exe' for proc in psutil.process_iter(['name']))
                if keyboard_running:
                    for proc in psutil.process_iter(['name']):
                        if proc.info['name'] == 'FreeVK.exe':
                            proc.kill()
                else:
                    send_command("keyboard_on")
                    subprocess.Popen(keyboard_app_path)
                time.sleep(0.5)
            prev_keyboardButton = multiFunctionButton

        # --------------------------- Mode 2: Media Control ---------------------------
        elif potentiometerMode == 2:
            if bootupInCurrentModeTwo or setVolumeOnce:
                initial_volume = get_volume()
                send_command(f"VOL:{initial_volume}")
                bootupInCurrentModeTwo = False
                setVolumeOnce = False

            joy_threshold = 0.2
            if abs(joy_x) > abs(joy_y):
                if joy_x > joy_threshold and get_volume() < 100:
                    keyboard.press(Key.media_volume_up)
                    keyboard.release(Key.media_volume_up)
                    time.sleep(0.1)
                    send_command(f"VOL:{get_volume()}")
                elif joy_x < -joy_threshold and get_volume() > 0:
                    keyboard.press(Key.media_volume_down)
                    keyboard.release(Key.media_volume_down)
                    time.sleep(0.1)
                    send_command(f"VOL:{get_volume()}")
            elif abs(joy_y) > abs(joy_x):
                if joy_y > joy_threshold:
                    keyboard.press(Key.media_previous)
                    keyboard.release(Key.media_previous)
                elif joy_y < -joy_threshold:
                    keyboard.press(Key.media_next)
                    keyboard.release(Key.media_next)
            if sw and not prev_sw and joy_x == 0 and joy_y == 0:
                keyboard.press(Key.media_play_pause)
                keyboard.release(Key.media_play_pause)
                prev_sw = 1
            elif not sw:
                prev_sw = 0
            prev_joy_x = joy_x

        # --------------------------- Mode 3: Shutdown / Browser ---------------------------
        elif potentiometerMode == 3:
            bootupInCurrentModeTwo = False

            # --- Enter or Exit Shutdown Timer Mode ---
            if multiFunctionButton == 1 and not shutdown_mode and not shutdown_timer_running:
                shutdown_mode = True
                shutdown_timer_seconds = 600  # Default 10 minutes
                send_command(f"ENTER_TIMER_MODE:{shutdown_timer_seconds}")
                print(f"[Info] Entered Shutdown Timer Mode: {shutdown_timer_seconds // 60} min")
                time.sleep(0.3)  # Debounce
            elif multiFunctionButton == 1 and shutdown_mode and not shutdown_timer_running:
                shutdown_mode = False
                shutdown_timer_seconds = 0
                send_command("EXIT_TIMER_MODE")
                print("[Info] Exited Shutdown Timer Mode")
                time.sleep(0.3)  # Debounce

            if shutdown_mode:
                # --- Adjust timer using joystick ---
                if joy_x > joyx_shutdown_threshold and not joy_right_active:
                    shutdown_timer_seconds += 600  # Increase 10 min
                    send_command(f"UPDATE_TIMER:{shutdown_timer_seconds}")
                    joy_right_active = True
                    joy_left_active = False
                    print(f"[Timer] Increased to {shutdown_timer_seconds // 60} min")
                elif joy_x < -joyx_shutdown_threshold and not joy_left_active:
                    shutdown_timer_seconds = max(0, shutdown_timer_seconds - 600)
                    send_command(f"UPDATE_TIMER:{shutdown_timer_seconds}")
                    joy_left_active = True
                    joy_right_active = False
                    print(f"[Timer] Decreased to {shutdown_timer_seconds // 60} min")
                else:
                    joy_right_active = joy_left_active = False

                # --- SW button short/long press handling ---
                if sw and not prev_sw:
                    sw_press_time = time.time()
                elif not sw and prev_sw:
                    held_time = time.time() - sw_press_time
                    if held_time < 1 and shutdown_timer_seconds > 0 and not shutdown_timer_running:
                        shutdown_timer_running = True
                        subprocess.run(f'shutdown -s -f -t {shutdown_timer_seconds}', shell=True)
                        send_command(f"START_TIMER:{shutdown_timer_seconds}")
                        print(f"[Timer] Shutdown scheduled in {shutdown_timer_seconds // 60} minutes")
                    elif held_time >= 1:
                        shutdown_timer_running = False
                        shutdown_mode = False
                        subprocess.run('shutdown -a', shell=True)
                        send_command("CANCEL_TIMER")
                        send_command("EXIT_TIMER_MODE")
                        print("[Timer] Shutdown aborted")
                prev_sw = sw

            # --- Browser shortcuts ---
            if redButton:
                open_in_brave("https://www.youtube.com")
                print("[Browser] Opened YouTube")
            if left_click:
                open_in_brave("https://www.netflix.com")
                print("[Browser] Opened Netflix")
            if right_click:
                open_in_brave("https://chat.openai.com")
                print("[Browser] Opened ChatGPT")

    except Exception as e:
        print(f"[Error] {e}")
        time.sleep(0.05)

    # Small delay to prevent CPU overload
    time.sleep(0.01)