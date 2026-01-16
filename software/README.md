# Nixie Power Meter (ESP32-S3)

This project implements a vintage-style **Power Meter** using logical Nixie Tubes (or similar displays), driven by an **ESP32-S3 Zero**. It samples current via a high-speed ADC sensor, calculates the RMS values in real-time, and displays the amperage on a 2-digit display.

## Features

*   **Real-time RMS Calculation**: Continuous sampling at **80 kHz** using ESP32-S3 DMA for precise AC current measurement.
*   **Nixie Tube Display**: Drives a dual-digit display via cascaded 74HC595 shift registers.
*   **System Monitoring**: Checks sampling consistency and CPU performance, alerting via status LED if data is lost.
*   **WS2812B Status LED**: Custom "bit-banged" driver for system health indication (Green/Yellow/Red).

## Hardware Configuration

### Controller
*   **Board**: Waveshare ESP32-S3 Zero
*   **Framework**: ESP-IDF (via PlatformIO)

### Pinout

| Function | PIN (GPIO) | Description |
| :--- | :--- | :--- |
| **ADC Input** | `GPIO 1` | Sensor Analog Out (0.5V - 4.5V) |
| **Status LED** | `GPIO 21` | WS2812B Data In |
| **SR Data** | `GPIO 10` | (DS) Serial Data |
| **SR Clock** | `GPIO 7` | (SHCP) Shift Clock |
| **SR Latch** | `GPIO 8` | (STCP) Storage Latch |
| **SR Enable** | `GPIO 9` | (OE) Output Enable |

### Sensor Calibration
*   **Zero Point**: ~2.5V (Center)
*   **Sensitivity**: ~0.333 V/A (Range +/- 6A)

## Software Architecture

The software is built on FreeRTOS with a multi-tasking approach:

1.  **ADC Processing Task (Core 1)**:
    *   Handles Direct Memory Access (DMA) from the ADC.
    *   Performs raw-to-ampere conversion and partial Sum-of-Squares calculation.
2.  **Main Logic Task**:
    *   Aggregates RMS chunks.
    *   Updates the Shift Registers updates (00-99 display).
    *   Monitors sampling frequency to detect lag.
3.  **LED Task**:
    *   Controls the onboard status LED via a custom assembly-optimized driver.

## LED Status Codes

The WS2812B LED indicates the health of the system:

| Color | Pattern | Meaning |
| :--- | :--- | :--- |
| 🟢 **Green** | Slow Blink | **System Healthy**. Sampling at ~80kHz. |
| 🟡 **Yellow** | Slow Blink | **Performance Warning**. Sampling rate dropped < 90%. |
| 🟡 **Yellow** | Fast Blink | **Queue Overflow**. Processing logic cannot keep up. |
| 🔴 **Red** | Fast Blink | **Critical Error**. ADC Timeout or Hardware failure. |

## Build & Upload

This project uses **PlatformIO**.

1.  Clone the repository.
2.  Open in VS Code with PlatformIO extension.
3.  Build and Upload:
    ```bash
    pio run --target upload
    ```

## License

[MIT](LICENSE)
