# Battery Module (`battery_module`)

> **Source:** `components/battery_module/` — `battery.c`  
> **Public API:** `include/battery.h`  
> **Target:** ESP32-S3 (ESP-IDF 6.0.2) & Host Test Suite  

The **Battery Module** is responsible for sampling battery cell voltage, estimating state-of-charge percentage (0–100%), filtering radio transmission current transients, enforcing monotonicity to eliminate percentage bounce, and disseminating battery telemetry across all communication planes (BLE Battery Service, ESP-NOW Split Heartbeat, and the internal Event Bus).

---

## 1. Hardware Interface & Pin Assignment

Per the architectural pin budget decision ([`PIN_BUDGET_HARDWARE_DECISION.md`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/implementations/PIN_BUDGET_HARDWARE_DECISION.md)):
- On ESP32-S3, **ADC2 (GPIO11–20) conflicts with Wi-Fi/ESP-NOW hardware arbitration** and cannot be used reliably in wireless operation.
- In split keyboard configurations, each half utilizes only 6 to 8 physical columns, releasing **GPIO 7 (ADC1_CH6)** as the dedicated analog battery voltage sensing line.
- An external high-impedance voltage divider (e.g. 1 MΩ / 1 MΩ with 100 nF reservoir capacitor, divider ratio 2.0× / 2000‰) scales the single-cell LiPo voltage (3.3 V – 4.2 V) to the ADC input range (~1.65 V – 2.1 V).

### Kconfig Configuration:
- `CONFIG_TEF_BATTERY_ENABLE` (default `y`): Enable/disable the battery monitoring driver.
- `CONFIG_TEF_BATTERY_ADC_GPIO` (default `7`): Pin mapped to ADC1. Set to `-1` to disable ADC reading.
- `CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE` (default `2000`): Permille multiplier (2000 = 2.0×).
- `CONFIG_TEF_BATTERY_MIN_MV` (default `3300`): 0% voltage threshold.
- `CONFIG_TEF_BATTERY_MAX_MV` (default `4200`): 100% voltage threshold.
- `CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC` (default `60`): Periodic reporting interval.
- `CONFIG_TEF_BATTERY_DELTA_NOTIFY_PCT` (default `2`): Delta change threshold triggering immediate push.

---

## 2. Driver Architecture & Signal Processing

```
                       +-----------------------------+
                       |  GPIO 7 (ADC1_CH6 Divider)  |
                       +--------------+--------------+
                                      |
                                      v
                       +-----------------------------+
                       |  5-Sample Median Filter     | (Rejects RF TX dips)
                       +--------------+--------------+
                                      |
                                      v
                       +-----------------------------+
                       |  Curve Fitting Calibration  |
                       |  (adc_cali_raw_to_voltage)  |
                       +--------------+--------------+
                                      |
                                      v
                       +-----------------------------+
                       | LiPo Non-Linear LUT & Interp| (3.30V->0%, 3.85V->50%, 4.20V->100%)
                       +--------------+--------------+
                                      |
                                      v
                       +-----------------------------+
                       | Monotonic & Hysteresis Unit | (Prevents jitter during discharge)
                       +--------------+--------------+
                                      |
        +-----------------------------+-----------------------------+
        |                             |                             |
        v                             v                             v
+---------------+             +---------------+             +---------------+
| BLE BAS Push  |             | Event Bus     |             | Split Link    |
| (All profiles)|             | (BATTERY_EVT) |             | (Heartbeat)   |
+---------------+             +---------------+             +---------------+
```

### 2.1. Median Filtering
Bluetooth LE and Wi-Fi transmissions generate instantaneous pulse currents of 60–100 mA, causing internal battery resistance and track drops of 50–150 mV. An insertion-sorted 5-sample median filter discards these extreme outliers and provides an accurate resting cell voltage.

### 2.2. Non-Linear State-of-Charge (SoC) LUT
A simple linear map from 3.3 V to 4.2 V distorts battery percentage because standard lithium batteries have a flat discharge plateau between 3.7 V and 3.9 V. The driver employs a piecewise linear interpolation look-up table:
- $\ge 4.20\,\text{V} \implies 100\,\%$
- $4.05\,\text{V} \implies 90\,\%$
- $3.95\,\text{V} \implies 75\,\%$
- $3.85\,\text{V} \implies 50\,\%$
- $3.75\,\text{V} \implies 30\,\%$
- $3.65\,\text{V} \implies 15\,\%$
- $3.50\,\text{V} \implies 5\,\%$
- $\le 3.30\,\text{V} \implies 0\,\%$

### 2.3. Monotonicity and Charge Detection
Under varying workload, resting voltage can rise temporarily by a few millivolts when keys are released. The driver strictly prevents upward jitter during discharge:
- Percentage is updated downward monotonically as charge depletes.
- If a positive jump of $\ge +5\,\%$ is observed, the driver recognizes external power insertion (USB charging/VBUS) and allows the state-of-charge to rise, setting `battery_is_charging() = true`.

---

## 3. Telemetry and System Integration

1. **BLE Battery Service (BAS) & Periodic Timer:**  
   `battery_init()` registers an `esp_timer` periodic callback ("batt_poll") at `CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC` (default 60 s). Whenever `battery_poll_and_notify()` runs, if $\Delta \ge 2\,\%$ or interval elapsed, it invokes `ble_hid_notify_battery_all()`, dispatching GATTS notifications to all subscribed host devices.
2. **ESP-NOW Split Heartbeat:**  
   In split setups, the Master echoes its live battery percentage via `resp.battery_pct = battery_get_level_pct()` in `split_dispatch.c`, and the Slave includes its own reading via `split_task.c`.
3. **Event Bus & Critical Cutoff:**  
   Posts `BATTERY_EVENTS` / `BATTERY_EVENT_LEVEL_CHANGED` payload (`uint8_t`) so status, display, RGB LED, and `power_mgr` can react (e.g. entering deep sleep cutoff if level hits 0%).
