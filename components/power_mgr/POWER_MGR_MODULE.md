# Unified Power Manager Module (`power_mgr`)

> **Source:** `components/power_mgr/` — `power_mgr.c`  
> **Public API:** `include/power_mgr.h`  
> **Target:** ESP32-S3 (ESP-IDF 6.0.2) & Host Test Suite  

The **Unified Power Manager** (`power_mgr`) is the single authoritative coordinator for dynamic frequency scaling (DFS), CPU power locks, peripheral activity timeouts, and sleep modes (Light Sleep and Deep Sleep) across the keyboard firmware.

---

## 1. Architectural Motivation & Problem Statement

Prior to Phase 3.2:
- Power management calls were duplicated between `split_task.c` and `split_bridge.c` (`pm_apply_active`), hardcoding a static 240 MHz clock with no dynamic scaling.
- The scan loop and Wi-Fi/BLE subsystems drew continuous maximum active current (~65–80 mA) even when no keys were being pressed.
- Sleep modes could not be activated safely because entering light sleep while USB is mounted causes USB OTG PHY clock failure.

The **Unified Power Manager** introduces:
1. **Single Ownership of `esp_pm_configure()`:** Configured once at boot with DFS frequency bounds (240 MHz max, 80 MHz min).
2. **Dynamic CPU Frequency Locks:** Governed via `ESP_PM_CPU_FREQ_MAX`. The lock is held during active typing and released when entering IDLE.
3. **USB PHY Protection:** Light sleep and deep sleep are strictly prohibited whenever USB VBUS is connected and TinyUSB is active.
4. **Hardware-Safe Matrix Wakeup (R13):** Disables level-sensitive wakeups immediately upon waking from light sleep to avoid interrupt storms while keys are held down.

---

## 2. Power State Machine

```
   +-----------------------------------------------------------+
   |                        BOOT / INIT                        |
   +-----------------------------+-----------------------------+
                                 |
                                 v
   +-----------------------------------------------------------+
   |                       POWER_STATE_ACTIVE                  |
   |   - CPU: 240 MHz (ESP_PM_CPU_FREQ_MAX Lock Held)          |
   |   - Scan Loop: 1000 Hz or debounced matrix polling        |
   |   - Radios: Active                                        |
   |   - Typical Draw: ~65 - 80 mA                             |
   +--------------+-----------------------------^--------------+
                  |                             |
     Inactivity   |                             | Activity notify
     >= 5.0 s     |                             | (keypress / traffic)
                  v                             |
   +--------------------------------------------+--------------+
   |                       POWER_STATE_IDLE                    |
   |   - CPU: 80 MHz (DFS Base / Tickless FreeRTOS)            |
   |   - Max Frequency Lock: Released                          |
   |   - Scan Loop: ulTaskNotifyTake (ISR wait)                |
   |   - Typical Draw: ~15 - 20 mA                             |
   +--------------+--------------------------------------------+
                  |
                  | If Wireless (!USB Active) && Inactivity >= 30 s
                  v
   +-----------------------------------------------------------+
   |                    POWER_STATE_LIGHT_SLEEP                |
   |   - CPU: Clock-gated (WFI / Low-power sleep clock)        |
   |   - Wake Source: Matrix rows GPIO 1..6 (LOW_LEVEL)        |
   |   - Columns: Pulled LOW                                   |
   |   - Wake Latency: ~1.5 ms                                 |
   |   - Safety: Disables level wake immediately on resume     |
   |   - Typical Draw: ~1.5 - 2.5 mA                           |
   +--------------+--------------------------------------------+
                  |
                  | If Inactivity >= 15 min (900 s)
                  v
   +-----------------------------------------------------------+
   |                    POWER_STATE_DEEP_SLEEP                 |
   |   - CPU / SRAM / Radios: Powered OFF                      |
   |   - Wake Source: RTC GPIO 1..6 (EXT1 Wakeup ANY_LOW)      |
   |   - Column Pins: Held LOW via gpio_deep_sleep_hold_en()   |
   |   - Row Pins: RTC Pull-up enabled                         |
   |   - Resume: Full cold boot (~150 ms + BLE reconnect)      |
   |   - Typical Draw: < 20 µA                                 |
   +-----------------------------------------------------------+
```

---

## 3. Subsystem Coordination

- **`kb_manager`:**
  - Invokes `power_mgr_activity_notify()` whenever a matrix state change or keypress edge occurs.
  - In its idle wait phase (no active keys, no pending reports, no tap-hold trackers), evaluates `power_mgr_can_enter_light_sleep()`. If qualified, delegates sleep entry to `power_mgr_enter_light_sleep()`.
- **`usbmod`:**
  - On `TINYUSB_EVENT_ATTACHED`, notifies `power_mgr_set_usb_active(true)` to keep the OTG PHY operational.
  - On `TINYUSB_EVENT_DETACHED`, notifies `power_mgr_set_usb_active(false)` to permit wireless sleep transitions.
- **`split_task` & `split_bridge`:**
  - Replaces legacy hardcoded `pm_apply_active()` with `power_mgr_activity_notify()`, seamlessly ensuring full CPU frequency during split role negotiation or slave matrix packet forwarding.

---

## 4. Kconfig Configuration & Rollback

- `CONFIG_TEF_POWER_MGR` (default `y`): Master switch. If set to `n`, power management drops back to static 240 MHz execution without power locks or dynamic sleep.
- `CONFIG_TEF_PM_IDLE_TIMEOUT_MS` (default `5000`): Time without events before shifting from ACTIVE (240 MHz) to IDLE (80 MHz).
- `CONFIG_TEF_PM_ENABLE_LIGHT_SLEEP` (default `y`): Allows light sleep in wireless operation.
- `CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS` (default `30000`): Inactivity required before entering light sleep (30 seconds).
- `CONFIG_TEF_PM_ENABLE_DEEP_SLEEP` (default `y`): Allows deep sleep.
- `CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS` (default `900000`): Inactivity required before entering deep sleep (15 minutes).
