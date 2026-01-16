# Project Status Report

**Date:** January 16, 2026  
**Platform:** ESP32-S3 Zero (ESP-IDF)

## Overview
This project implements a Nixie Tube Power Meter using an ESP32-S3. It samples a current sensor via high-speed ADC, calculates the RMS value, and displays it on a 2-digit Nixie display driven by shift registers. A WS2812B LED provides system status.

## Hardware Configuration

### ADC / Sensor
- **Pin:** GPIO 1 (ADC1 Channel 0)
- **Sensor Specs:** 
  - Center Voltage: 2.5V (0A)
  - Range: +/- 6A (0.5V to 4.5V)
  - Sensitivity: ~0.333 V/A
- **Configuration:** 80kHz Sampling, 12-bit Resolution, Continuous DMA.

### Nixie Display (Shift Registers)
- **Driver:** 3x 74HC595 (Cascaded, 24-bit total)
- **Pins:**
  - `DATA` (DS): GPIO 10
  - `CLOCK` (SHCP): GPIO 7
  - `LATCH` (STCP): GPIO 8
  - `OE` (Output Enable): GPIO 9 (Active Low)
- **Logic:** Custom mapping for Tens and Ones digits across the 3 registers.

### Status LED
- **Type:** WS2812B (NeoPixel)
- **Pin:** GPIO 21
- **Driver:** Custom bit-banging implementation using CPU cycle counting (Direct Register Access).

### Inputs
- **Button:** GPIO 0 (Boot Button) - Used to cycle display modes.

## Software Architecture

1.  **ADC Task (`adc_processing_task`)**:
    - Reads raw data from DMA buffer.
    - Converts raw values to Amperes.
    - Calculates Sum-of-Squares (Partial RMS).
    - Sends chunks to the queue.

2.  **Main Logic (`main_logic_task`)**:
    - Aggregates data from queue.
    - polling Button (GPIO 0) for Mode switching.
    - Calculates final RMS every 500ms.
    - Updates Shift Registers (00-99 scaled display) based on current Multiplier.
    - **Monitoring:** Checks actual sampling frequency every 2 seconds to detect sample loss.

3.  **LED Task (`led_task`)**:
    - Receives status commands via Queue.
    - Directly drives GPIO using optimized assembly timings.

## System Status Indicators (LED)

| Color | Mode | Meaning |
| :--- | :--- | :--- |
| **Green** | Blink (Slow) | **System Healthy**. Sampling frequency is within targets. |
| **Yellow** | Blink (Slow) | **Performance Warning**. Sampling frequency dropped below 90% target (Lag). |
| **Yellow** | Blink (Fast) | **Queue Overflow**. Main loop cannot keep up with ADC data rate. |
| **Red** | Blink (Fast) | **Hardware Error**. ADC Read Timeout / DMA failure. |

### Display Modes (Selection via Button)
When the Boot Button is pressed, the LED indicates the new mode for 2 seconds.

| Mode | Factor | LED Color (Static) | Result Mapping |
| :--- | :--- | :--- | :--- |
| **0** (Default) | **4x** | **Pink** (Mag) | 1A -> "04" |
| **1** | **8x** | **Orange** | 1A -> "08" |
| **2** | **16x** | **Blue** | 1A -> "16" |

## Recent Changes & Fixes
- **Display Modes**: Added 3 modes switching via GPIO 0 (Boot Button). Multiplies current by 4, 8, or 16.
- **WS2812B Driver**: Replaced external library with custom "bit-banging" driver to fix timing issues (LED was showing white). used assembly cycle counting.
- **Pin Mapping**: Updated Shift Register pins to match hardware (10, 9, 8, 7).
- **Monitoring**: Added logic to measure actual input frequency and warn via LED if the system lags.
- **Display Logic**: Implemented 2-digit Nixie mapping logic using bitwise operations on 3 registers.

## Open Items / TODO
- [ ] **Calibration**: Verify the `0.333 V/A` sensitivity and `2.5V` offset with a multimeter/oscilloscope.
- [ ] **Digit Mapping**: Test if the specific bit-to-digit mapping for the Nixie tubes is correct on physical hardware.
- [ ] **LED Features**: Re-enable or merge "Current Load Color" (Green/Yellow/Red based on Amps) with "System Health" indication.
