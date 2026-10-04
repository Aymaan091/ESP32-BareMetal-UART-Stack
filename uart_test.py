import serial
import time

def calculate_crc8(data_bytes):
    crc = 0
    for byte in data_bytes:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80: crc = (crc << 1) ^ 0x07
            else: crc <<= 1
    return crc & 0xFF

def send_command(ser, cmd, data):
    crc = calculate_crc8([cmd, data])
    packet = bytearray([0xAA, cmd, data, crc, 0x55])
    ser.write(packet)
    time.sleep(0.1)
    # Print the ESP32's debug response
    while ser.in_waiting > 0:
        print(ser.readline().decode('utf-8', errors='replace').strip())

# Connect to the board
ser = serial.Serial('COM7', 115200, timeout=1)
ser.setDTR(False)
ser.setRTS(False)
time.sleep(2)
ser.reset_input_buffer()

print("\nSending Blue Command...")
send_command(ser, 0x01, 0x01)
time.sleep(2)

print("\nSending Green Command...")
send_command(ser, 0x01, 0x02)
time.sleep(2)

print("\nSending Off Command...")
send_command(ser, 0x01, 0x00)

ser.close()