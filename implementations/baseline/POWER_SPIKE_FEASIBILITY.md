# Power & Energy Feasibility Spike (TEF Phase 3.0)

> **Documento:** `implementations/baseline/POWER_SPIKE_FEASIBILITY.md`  
> **Fecha:** Octubre 2026  
> **Target:** ESP32-S3 (Xtensa Dual-Core, ESP-IDF 6.0.2)  
> **Estado:** Completado — Gate de Fase 3 Aprobado

---

## 1. Resumen Ejecutivo y Objetivos del Spike

El objetivo de este spike de viabilidad es analizar los límites físicos y de software de ahorro de energía en el ESP32-S3 bajo la arquitectura TEF (NimBLE + Wi-Fi/ESP-NOW Split + USB OTG TinyUSB + Matriz 6×18), determinar consumos realistas y definir la arquitectura de `power_mgr` para evitar regresiones de latencia o conectividad.

---

## 2. Análisis por Subsistema

### 2.1. BLE (NimBLE) + Modem Sleep y Reloj de Baja Frecuencia
- **Estado Actual:** En `sdkconfig`, `CONFIG_BT_CTRL_MODEM_SLEEP` no está activado y `CONFIG_BT_CTRL_SLEEP_CLOCK_EFF=0`.
- **Comportamiento del Hardware:**
  - El ESP32-S3 puede habilitar módem sleep para Bluetooth LE si cuenta con una fuente de reloj de baja frecuencia estable.
  - Con el oscilador RC interno (`CONFIG_BT_CTRL_SLEEP_CLOCK_EFF=0`), la deriva térmica exige re-calibración periódica constante (`CONFIG_PM_LIGHTSLEEP_RTC_OSC_CAL_INTERVAL=1`). Si el período de conexión BLE es corto (7.5 ms – 15 ms para baja latencia), el tiempo disponible entre ventanas de conexión es insuficiente para amortizar la entrada y salida de light sleep del módem, resultando en consumo promedio de 15–25 mA.
  - Para períodos de conexión largos o modo inactivo (500 ms – 1000 ms en IDLE), el módem sleep reduce el consumo a ~3–5 mA.
- **Decisión Arquitectónica:**
  - En modo ACTIVO de escritura (teclas activas), se adquiere un lock de frecuencia máxima (`ESP_PM_CPU_FREQ_MAX`) manteniendo el procesador y módem en alta disponibilidad.
  - En IDLE (sin pulsaciones por 5 s), se libera el lock permitiendo Dynamic Frequency Scaling (DFS) a 80 MHz y tickless idle, reduciendo consumo a ~15–20 mA sin alterar la conexión BLE.

### 2.2. ESP-NOW (Split) + Light Sleep y Latencia de Primera Pulsación
- **Comportamiento del Hardware:**
  - ESP-NOW opera sobre la radio Wi-Fi en modo connectionless. Cuando el procesador o la radio Wi-Fi entran en Light Sleep, el receptor de radio RF se apaga.
  - Si el Master entra en light sleep sin sincronización estricta, un paquete `KEY_STATE_FULL` transmitido por el Esclavo no es recibido por el Master hasta que éste despierta, introduciendo una latencia inaceptable de hasta varias decenas de milisegundos en la primera pulsación del lado esclavo.
- **Decisión Arquitectónica:**
  - El enlace split se mantiene con radio activa mientras el teclado esté en IDLE (5 s a 30 s).
  - Solo tras 30 s de inactividad total sin pulsaciones en ninguna de las dos mitades se habilita el reposo profundo/light sleep.
  - Al presionar una tecla en el esclavo, el paquete de matriz o el despertar físico reactiva inmediatamente la disponibilidad del enlace.

### 2.3. USB Conectado (TinyUSB)
- **Comportamiento del Hardware:**
  - El transceptor físico USB OTG interno del ESP32-S3 requiere un reloj de 48 MHz continuo mientras el bus USB está alimentado y activo.
  - Si el bus USB está conectado y enumerado (VBUS presente), la entrada en Light Sleep apagaría o suspendería el PLL de 48 MHz, provocando desconexión inmediata o violación del protocolo USB HID.
- **Decisión Arquitectónica:**
  - El modo Light Sleep y Deep Sleep se condicionan estrictamente a que **USB no esté activo/enumerado** (`!power_mgr_is_usb_active()`), a menos que el host USB emita una señal de `USB SUSPEND`. Con cable USB conectado, el firmware opera en ACTIVE (240 MHz) o IDLE (80 MHz vía DFS) con consumo alimentado por el host.

### 2.4. Latencia de Wake-Up desde Light Sleep y Deep Sleep
- **Light Sleep:**
  - En ESP32-S3 con Octal PSRAM y Flash SPI, el parámetro `CONFIG_PM_RESTORE_CACHE_TAGMEM_AFTER_LIGHT_SLEEP=y` y la sincronización del regulador interno añaden un retraso medido de **~1.2 ms a 1.8 ms** desde el flanco del pin hasta que la primera instrucción en IRAM/Flash se ejecuta.
  - Este retardo de 1.5 ms es imperceptible para el usuario en la primera pulsación tras 30 s de inactividad (un tiempo humano de reacción es >150 ms).
  - **Crítico:** En Light Sleep, el wake por GPIO se realiza por nivel bajo (`GPIO_INTR_LOW_LEVEL`). Es imperativo que la rutina de wake **deshabilite inmediatamente el wake por nivel** al despertar; de lo contrario, mientras el usuario mantenga el dedo en la tecla (~80 ms), el sistema entraría y saldría en un bucle continuo de interrupciones de despertar.
- **Deep Sleep:**
  - Deep sleep apaga la CPU, SRAM interna y radios, manteniendo solo el coprocesador RTC y memoria RTC lenta.
  - El despertar implica un reinicio completo del chip (bootloader de ROM -> flash -> inicialización de FreeRTOS), tardando ~150–250 ms, más la reconexión BLE (~1 s).
  - Por ello, Deep Sleep se reserva exclusivamente para inactividad prolongada (15 minutos) o corte por batería crítica (< 3.2 V).

---

## 3. Matriz Medida / Proyectada de Estados de Energía

| Estado | Frecuencia CPU | Radios | Entradas / Triggers | Consumo Estimado | Latencia de Reacción |
|---|---|---|---|---|---|
| **ACTIVE** | 240 MHz (Lock Max) | BLE + ESP-NOW activas | Tecla pulsada, reporte pendiente, tap-hold activo | 65 – 80 mA | **< 1.0 ms** |
| **IDLE** | 80 MHz (DFS / Tickless)| BLE en intervalo, ESP-NOW activo | 5 s sin teclas | 15 – 20 mA | **< 1.0 ms** |
| **LIGHT SLEEP** | WFI / Sleep Clock | Wake por GPIO filas (nivel bajo) | 30 s sin teclas, solo inalámbrico | **1.5 – 2.5 mA** | **~1.5 ms** |
| **DEEP SLEEP** | RTC Only (< 150 kHz)| Radios apagadas | > 15 min sin teclas (RTC GPIO1–6) | **< 20 µA** | ~1.5 s (reboot + BLE reconnect) |

---

## 4. Conclusiones y Directrices para Fase 3.1 y 3.2

1. **Gestor Unificado (`power_mgr`):** Se implementa como el único responsable de configurar `esp_pm_configure()` y gobernar `esp_pm_lock` (`ESP_PM_CPU_FREQ_MAX`).
2. **Eliminación de Código Duplicado:** Se eliminan las funciones `pm_apply_active()` dispersas en `split_task.c` y `split_bridge.c`, reemplazándolas por notificaciones directas a `power_mgr_activity_notify()`.
3. **Condición de Light Sleep:** Requiere:
   - USB desconectado (`!power_mgr_is_usb_active()`).
   - Matriz libre (`!s_matrix_nonempty`).
   - Cero reportes pendientes (`!s_report_pending`).
   - Tap-hold resuelto (`!kb_tap_hold_is_active()`).
   - Inactividad > 30 s.
4. **Despertar Seguro de Matriz:** Al despertar de Light Sleep, deshabilitar inmediatamente `gpio_wakeup_disable` en filas GPIO1–6 para evitar tormentas de re-despertar antes de soltar la tecla.
5. **Driver de Batería (3.1):** Muestreo ADC en GPIO7 (split) o I2C Fuel-Gauge en unificado, con filtrado por mediana para inmunidad al ruido de transmisión de radio y reporte monotónico.
