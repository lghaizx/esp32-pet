"""Read the boot log / runtime log of the pet.

    python tools/read_serial.py [port] [seconds]

The board is reset with the classic DTR/RTS pulse first, so the log always
starts at the boot banner (which is what makes this useful after a flash).
"""
import serial, time, sys
port = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
ser = serial.Serial(port, 115200, timeout=0.2)
# classic ESP32 auto-reset pulse (RTS->EN, DTR->GPIO0)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.12)
ser.setRTS(False)
time.sleep(0.1)
buf = b''
t0 = time.time()
while time.time() - t0 < secs:
    data = ser.read(4096)
    if data:
        buf += data
ser.close()
# Encode explicitly: the console defaults to a legacy code page on Windows, and
# a boot banner can contain a replacement character that it cannot represent.
sys.stdout.buffer.write(buf.decode('utf-8', 'replace').encode('utf-8', 'backslashreplace'))
