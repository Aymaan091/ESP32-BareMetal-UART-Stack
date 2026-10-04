# ESP32 Bare-Metal UART Communication Stack

A fault-tolerant, interrupt-driven serial communication protocol built for the **ESP32-S3** using **C (ESP-IDF)** and **FreeRTOS**. 

This project demonstrates how to guarantee reliable, lossless data exchange between a PC application and an embedded hardware peripheral in potentially noisy environments without relying on blocking polling loops.

## 🚀 System Architecture

Unlike standard `uart_read_bytes` blocking loops, this stack guarantees zero data loss and safely rejects corrupted packets using a four-layer architecture:

1. **Hardware Interrupts (FreeRTOS):** The UART driver utilizes FreeRTOS event queues (`uart_event_task`) to wake the processor only when data physically hits the RX pin, freeing up CPU cycles for other tasks.
2. **Circular Ring Buffer:** Incoming bytes are instantly pushed into a custom, thread-safe ring buffer. This completely decouples the fast hardware interrupt layer from the slower application processing layer.
3. **State Machine Packet Framing:** A strict state machine parses the buffer byte-by-byte looking for `0xAA` (Start) and `0x55` (End) frames. It silently drops garbage data and resets on misaligned packets without crashing the system.
4. **CRC-8 Error Detection:** Before executing any hardware command, the ESP32 runs a mathematical CRC-8 checksum on the payload. If the computed hash does not match the transmitted hash (e.g., due to a flipped bit over a noisy wire), the packet is rejected safely.

## 🛠️ Hardware Integration (RGB LED)

The state machine is currently mapped to the ESP32-S3's onboard WS2812 RGB LED (via the `led_strip` RMT driver). A Python controller constructs raw Hex packets, computes the CRC, and pushes them over the COM port to remotely toggle the hardware states.

### Binary Packet Structure
All commands are sent as raw Hex bytes, not ASCII strings:
```text
[ 0xAA (Start) ]  [ COMMAND ]  [ DATA ]  [ CRC-8 ]  [ 0x55 (End) ]
