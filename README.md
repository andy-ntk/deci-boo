# Deci-Boo! 👾🔊

**Monsters, Inc. / Monsters University Scare Games–inspired Smart Sound & Noise Arcade** built with `STM32F411RE` (`NUCLEO-F411RE`) and `ESP32-S3`.

## Overview

- **`KY-037` (Microphone Sensor):** Samples ambient sound / scream level via Analog (`PA0` / `ADC1_IN0` peak-to-peak) and Digital threshold (`PA1`).
- **`KY-040` (Rotary Encoder + Switch):** Adjusts noise/scare threshold in real time (`TIM3` Encoder Mode on `PA6`/`PA7`) and resets/switches mode via push button (`PA8`).
- **`KY-009` (RGB SMD LED):** Displays 3-tier Green → Yellow → Blinking Red indicator via `TIM4` Hardware PWM (`PB6`, `PB8`, `PB9` with 220–330 Ω resistors).
- **`ESP32-S3` Wi-Fi Bridge:** Bidirectional UART (`USART1` ↔ `Serial1`) hosting a real-time Web Dashboard & threshold control over Wi-Fi.

## Pin Mapping (`NUCLEO-F411RE` ↔ Modules)

| Module | Pin | STM32F411RE / ESP32-S3 | Function |
|---|---|---|---|
| `KY-037` | `A0` / `D0` | `PA0` (`ADC1_IN0`) / `PA1` (`GPIO_Input`) | Sound amplitude & digital comparator |
| `KY-040` | `CLK` / `DT` / `SW` | `PA6` (`TIM3_CH1`) / `PA7` (`TIM3_CH2`) / `PA8` (`Pull-up`) | Threshold dial & button |
| `KY-009` | `R` / `G` / `B` | `PB6` (`TIM4_CH1`) / `PB8` (`TIM4_CH3`) / `PB9` (`TIM4_CH4`) | PWM RGB indicator (via 220–330 Ω R) |
| UART Bridge | `TX` / `RX` | `PA9` (`USART1_TX`) → `GPIO 18`, `PA10` (`USART1_RX`) ← `GPIO 17` | Bidirectional STM32 ↔ ESP32-S3 link |

## Team (Group `:)` — Embedded System Lab)

- `6731315721` Natthakorn Yiengyongphan
- `6731332321` Poonnawit Supawasuwat
- `6731362121` Sukrit Kankrasang
- `6731365021` Aut Suttitanamongkol

## Docs

- [Week 1 Progress & Hardware Verification](docs/progress_update_2026-11-12.md)
