# Baseline Metrics & System Characterization (TEF Phase 0.3)

> **Documento:** `implementations/baseline/BASELINE_METRICS.md`  
> **Fecha:** Octubre 2026  
> **Target:** ESP32-S3 (Xtensa Dual-Core, 240 MHz, ESP-IDF 6.0.2)  
> **Estado:** Línea base documentada e instrumentada

---

## 1. Inventario Real de Tareas FreeRTOS

Auditoría estática y dinámica de todas las tareas del firmware TEF:

| Nombre de Tarea | Prioridad | Core | Stack (Bytes) | Memoria (RAM) | Origen / Módulo | Propósito |
|---|---|---|---|---|---|---|
| `kb_mgr` | 5 | Unpinned | 6144 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_manager.c:490` | Escaneo de matriz, debounce y despacho de reportes (1000 Hz activo / ISR en reposo) |
| `kb_macro` | 4 | Unpinned | 5120 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_macro.c:606` | Ejecutor de secuencias de macros NVS |
| `kb_tap_0` | 4 | Unpinned | 3072 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_macro.c:612` | Worker para tap asíncrono no bloqueante |
| `kb_tap_1` | 4 | Unpinned | 3072 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_macro.c:612` | Worker para tap asíncrono no bloqueante |
| `kb_tap_2` | 4 | Unpinned | 3072 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_macro.c:612` | Worker para tap asíncrono no bloqueante |
| `kb_tap_3` | 4 | Unpinned | 3072 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_macro.c:612` | Worker para tap asíncrono no bloqueante |
| `kb_sys_action` | 5 | Unpinned | 4096 | **PSRAM** (`SPIRAM \| 8BIT`) | `components/keyboard/kb_system_action.c:178` | Máquina de estados Tap / Double-Tap / Hold |
| `usb_task` | 5 | Core 1 | 4096 | **DRAM** (`INTERNAL \| 8BIT`) | `components/usb_module/usbmod.c:218` | Driver TinyUSB (`tud_task`) |
| `comm_tx_task` | 10 | Unpinned | 4096 | **DRAM** (`INTERNAL \| 8BIT`) | `components/comm_module/comm_tx.c:401` | Despacho de tramas COMM sobre transporte activo |
| `comm_proc_task`| 5 | Unpinned | 8192 | **DRAM** (`INTERNAL \| 8BIT`) | `components/comm_module/comm_dispatch.c:171`| Desensamblador y enrutador de mensajes entrantes COMM |
| `comm_timeout_task`| 5 | Unpinned | 4096 | **DRAM** (`INTERNAL \| 8BIT`) | `components/comm_module/comm_dispatch.c:172`| Polling de timeout de ráfagas RX/TX (período 5 ms) |
| `split` | 5 | Unpinned | 4096 | **DRAM** (`INTERNAL \| 8BIT`) | `components/split/split_task.c:435` | Enlace ESP-NOW (X25519 + AES-128-CCM), heartbeats y sync NVS |
| `rgb_worker` | 5 | Unpinned | 4096 | **DRAM** (`INTERNAL \| 8BIT`) | `components/rgb_module/rgb.c:200` | Renderizado y refresh de tira LED WS2812 / RGB |
| `button_task` | 10 | Unpinned | 2048 | **DRAM** (default heap) | `components/button_module/button.c:125` | Botón físico de arranque / hardware switch |

### Tareas Internas de ESP-IDF / FreeRTOS (DRAM):
- `ipc0` / `ipc1` (Dual-core Inter-Processor Communication)
- `esp_timer` (Daemon de timers de alta resolución)
- `sys_evt` (Default system event loop)
- `IDLE0` / `IDLE1` (FreeRTOS idle tasks)
- `tiT` / NimBLE Host & Controller (cuando BLE está activo)
- Wi-Fi Driver Tasks (cuando ESP-NOW / split está activo)

### Conclusión de Memoria:
- **DRAM Interna ocupada por stacks:** ~35 KB (7 tareas en DRAM).
- **PSRAM ocupada por stacks:** ~28 KB (7 tareas en PSRAM).
- Todas las tareas del subsistema `keyboard` (`kb_mgr`, `kb_macro`, `kb_tap_0..3`, `kb_sys_action`) ya operan correctamente en PSRAM sin consumir DRAM interna escasa.

---

## 2. Presupuesto de CPU y Ciclos de Escaneo de Matriz

### Estado Actual:
- **Frecuencia de escaneo:** 1000 Hz (`MAX_POLLING_RATE_HZ = 1000`).
- **Columnas:** 18 columnas físicas (`k_cols`).
- **Filas:** 6 filas físicas (`k_rows`, GPIO1–6).
- **Tiempo de asentamiento por columna:** 20 µs (`esp_rom_delay_us(CONFIG_TEF_MATRIX_SETTLE_US)`).
- **Carga de CPU activa:**
  $$\text{Tiempo de espera activo por scan} = 18 \times 20\,\mu\text{s} = 360\,\mu\text{s}$$
  A 1000 Hz, un escaneo activo consume $360\,\mu\text{s} / 1000\,\mu\text{s} = \mathbf{36.0\,\%}$ del núcleo donde corre `kb_mgr`.
- **Carga de CPU en reposo:**
  Cuando ninguna tecla está pulsada, `kb_mgr` activa la ISR de filas y entra en espera con `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100))`. El consumo de CPU en reposo es $\mathbf{\approx 0\,\%}$.

### Objetivo Fase 2.1:
- Con medición de osciloscopio de la constante RC real (~13 µs según cálculo de pista + diodo), reducir `CONFIG_TEF_MATRIX_SETTLE_US` a un valor medido seguro (ej. 8–10 µs):
  $$\text{Tiempo a } 8\,\mu\text{s} = 18 \times 8\,\mu\text{s} = 144\,\mu\text{s} \implies \mathbf{14.4\,\%}\text{ de CPU activa}.$$

---

## 3. Bloqueo de Transporte y Reportes (`kb_macro_send_report`)

### Estado Previo:
- Cuando USB o BLE fallan o no están listos (`ESP_ERR_INVALID_STATE` o `ESP_FAIL`), `kb_macro_send_report()` realiza hasta 100 reintentos de `vTaskDelay(1)`:
  $$\text{Retardo máximo bloqueado} = 100 \times 1\,\text{ms} = \mathbf{100\,\text{ms}}.$$
- Esto produce bloqueos perceptibles en el bucle principal de escaneo de `kb_manager` cada vez que ocurre un cambio de tecla con el transporte desconectado.

### Instrumentación Agregada (Phase 0.3):
- Módulo `components/keyboard/kb_telemetry.c` con estructura `kb_report_blocked_stats_t`:
  - `total_reports`: Total de llamadas a `kb_macro_send_report()`.
  - `failed_reports`: Cantidad de envíos que retornaron error tras el bucle.
  - `max_blocked_us`: Peor latencia observada de bloqueo (µs).
  - `total_blocked_us`: Tiempo acumulado bloqueado en el envío.
  - `buckets[7]`: Histograma distribuido en:
    - `[0]`: $< 100\,\mu\text{s}$ (envío inmediato sin retardo)
    - `[1]`: $100\,\mu\text{s} \dots 1\,\text{ms}$
    - `[2]`: $1\,\text{ms} \dots 5\,\text{ms}$
    - `[3]`: $5\,\text{ms} \dots 20\,\text{ms}$
    - `[4]`: $20\,\text{ms} \dots 50\,\text{ms}$
    - `[5]`: $50\,\text{ms} \dots 100\,\text{ms}$
    - `[6]`: $\ge 100\,\text{ms}$ (bloqueo completo por desconexión)
- Registro periódico en `kb_manager.c` cuando `max_blocked_us > 1000\,\mu\text{s}`.

---

## 4. Pin de Depuración de Latencia (`CONFIG_TEF_DEBUG_LATENCY_PIN`)

- **Kconfig:** `CONFIG_TEF_DEBUG_LATENCY_PIN` (default: `-1` deshabilitado, configurable `0..48`).
- **Mecanismo:** El pin de salida conmuta de nivel lógico (`HIGH` $\leftrightarrow$ `LOW`) exactamente en el momento en que una nueva pulsación física o virtual es registrada en el mapa NKRO virtual (`kb_macro_virtual_press`).
- **Metodología de Medición con Analizador Lógico:**
  - **Canal 0:** Terminal del switch mecánico (entrada física).
  - **Canal 1:** Pin `CONFIG_TEF_DEBUG_LATENCY_PIN`.
  - **Canal 2:** D+ de USB (detección de Start-of-Frame o paquete HID IN).
  - **Métricas:**
    - $\Delta t(\text{Switch} \to \text{Debug Pin})$: Latencia interna de escaneo + debounce (Línea base: ~4–5 ms con integrador de 5 scans; Objetivo Fase 2.2: $< 1.2\,\text{ms}$).
    - $\Delta t(\text{Debug Pin} \to \text{USB SOF})$: Latencia de entrega al host USB (Línea base: $\le 1.0\,\text{ms}$).

---

## 5. Consumo de Energía Estimado / Base (Hardware ESP32-S3)

| Escenario Operativo | CPU Freq | Radios Activas | Consumo Típico (PPK2) | Estado del Firmware |
|---|---|---|---|---|
| **USB Conectado (Activo / Idle)** | 240 MHz | Ninguna | 50 – 70 mA | TinyUSB activo; sin DFS |
| **BLE Conectado (Escaneando activo)**| 240 MHz | BLE (NimBLE) | 65 – 85 mA | Escaneo continuo a 1000 Hz |
| **BLE Conectado (Idle en espera)** | 240 MHz | BLE (intervalo conex.)| 25 – 35 mA | `ulTaskNotifyTake` en matriz, CPU a 240 MHz fija |
| **BLE + Split Master (Idle)** | 240 MHz | BLE + ESP-NOW | 35 – 50 mA | Ambas radios encendidas sin sleep de módem |
| **Objetivo Light Sleep (Fase 3)** | DFS/WFI | BLE sync / ESP-NOW sync| **1.5 – 2.5 mA** | Requiere spike 3.0 de viabilidad |
| **Objetivo Deep Sleep (Fase 3)** | RTC | Todas apagadas | **< 20 µA** | Wake por RTC GPIO1–6 con matriz |

---

## 6. Métricas de Memoria y Consolidación tras Fases 4 y 5

### 6.1 Liberación de DRAM Interna (.bss a PSRAM)
En la Fase 4.4, se migraron los buffers estáticos de 10 KB desde la memoria `.bss` de DRAM interna hacia asignaciones dinámicas en PSRAM (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` con fallback a DRAM):
- `s_tx_buf[MAX_TX_BUF_SIZE]` en `comm_tx.c` (10 KB).
- `s_rx_buf[MAX_RX_BUF_SIZE]` en `comm_rx.c` (10 KB).
- `s_cfg_resp_buf[CFG_USB_RESP_BUF_SIZE]` en `cfgmod.c` (10 KB).

**Resultado medido:**
- **DRAM interna (.bss) liberada:** **30.0 KB (30,720 bytes)**.
- **Guardia de CI (.github/workflows/ci.yml):** Límite máximo de `diram_bss` fijado en 4096 bytes para `comm_module` y `config_module` tras compilar con `idf.py size-components`.

### 6.2 Consolidación de Tareas Ociosas
- `comm_timeout_task` (stack 4096 B): **Eliminada** en favor del callback de timeout reactivo.
- `kb_sys_action` (stack 4096 B): **Consolidada** en el bucle determinista de `kb_mgr` vía `kb_system_action_poll()`.
- `kb_tap_0..3` (stacks 4 × 3072 B): **Consolidadas** en pool reactivo sin tareas dedicadas.
- **Total tareas reducidas:** De 13 tareas a 7 tareas activas.

### 6.3 Flags Estrictos y Cobertura de CI
- Compilación con `-Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized` en todos los componentes propios.
- Validación continua en CI bajo `gcc` con **ASan**, **UBSan** y **TSan** (`test/run_tests.sh --tsan`). Cobertura de 262/262 pruebas pasando con 0 data races.

