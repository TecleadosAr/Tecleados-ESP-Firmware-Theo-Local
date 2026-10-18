# Plan Integral de Implementación: Evolución Arquitectónica del Firmware (TEF)

> **Documento:** `FIRMWARE_EVOLUTION_IMPLEMENTATION_PLAN.md`  
> **Estado:** Revisado v3 — Fases 0–5 completadas (plan de evolución arquitectónica finalizado y verificado con 262 tests unitarios en ASan/TSan)  
> **Rama de Trabajo:** `firmware-evolution`  
> **Plataforma Objetivo:** ESP32-S3 (Xtensa dual-core, **ESP-IDF 6.0.2** — ver `sdkconfig` y `main/idf_component.yml` que exige `idf >= 6.0`) & Configurator (React 19 / Vite / TS)  
> **Fecha:** Octubre 2026

---

## 0. Registro de Revisión (qué cambió respecto a v1/v2 y por qué)

Cada afirmación del plan original fue contrastada contra el código. Resumen de hallazgos que **cambian el diseño**:

| # | Afirmación en v1/v2 | Realidad verificada en el repo | Consecuencia en v2/v3 |
|---|---|---|---|
| R1 | Toolchain ESP-IDF v5.5, CI con `espressif/idf:v5.5` | `sdkconfig` = **IDF 6.0.2**; `main/idf_component.yml` pide `idf >= 6.0` | CI y APIs (ADC, GPIO) se planifican para IDF 6.x |
| R2 | Flashear app en `0x10000` | [`partitions.csv`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/partitions.csv): app `factory` en **`0x90000`**, NVS `0x9000` (512 KB), **sin particiones OTA** | Descartado en v3 junto con el flasher web (R22) |
| R3 | "Liberar >9 KB de DRAM interna" al unificar tap-workers | Las 4 tareas `kb_tap_*` se crean con `MALLOC_CAP_SPIRAM` ([`kb_macro.c#L611-L613`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L611-L613)); solo `comm_timeout_task` (4 KB) está en DRAM interna | `comm_timeout_task` ya eliminada (1.4); `kb_tap_0..3` y `kb_sys_action` pasan a consolidación de tareas en §4.3 |
| R4 | Mailbox `xQueueOverwrite` de `kb_report_payload_t` | El reporte se arma **desde el estado compartido** `s_v_nkro` en `kb_macro_send_report()`; lo llaman `kb_manager`, `macro_task`, `tap_worker` y `kb_custom_key` | Mailbox de payload es innecesario y puede perder un *press* corto; se usa notificación + snapshot (§1.2) |
| R5 | El split sync de custom keys solo requiere subir cota a 120 y agregar `vTaskDelay` cada 8 paquetes | El push ya es **stop-and-wait con ACK** (`on_ack`, sem de TX); la cota `64` es real (macros `64` y combos `32` también están hardcodeados) | Fix de 3 líneas + constantes; **no** agregar delays |
| R6 | "Estado actual: 36 % de CPU" por settling | Cierto **solo mientras hay teclas activas**; en reposo se usa ISR + `ulTaskNotifyTake` | Objetivos de CPU recalculados (§2.1) |
| R7 | Reducir settling a 5–8 µs "porque es seguro" | El propio comentario de [`kb_matrix.c#L77`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_matrix.c#L77) calcula ~13 µs de constante RC | Se hace **parametrizable y medido con osciloscopio**, no fijado a ciegas |
| R8 | `GPIO.in1.data` + dos máscaras (low/high) | Filas = GPIO1–6, todas en el banco bajo (<32) | Basta una lectura de `GPIO.in`/`GPIO_IN_REG`; no hace falta `in1` |
| R9 | `esp_adc_cal_characterize` | API legacy eliminada; en S3 se usa `adc_cali_create_scheme_curve_fitting` | Corregido (§3.1) |
| R10 | Pin ADC "ADC1" sin especificar | **GPIO1–10 (todo ADC1) están ocupados por la matriz**; ADC2 (GPIO11–20) también (11–18 matriz, 19/20 USB) | Mitigado en 3.1 con `CONFIG_TEF_BATTERY_ADC_GPIO = -1` (deshabilitado por defecto sin hardware dedicado) |
| R11 | Divisor 100 kΩ/100 kΩ | Drena 4.2 V/200 kΩ ≈ **21 µA** ≈ todo el presupuesto de Deep Sleep (<20 µA) | Divisor de alta impedancia o gateado (§3.1) |
| R12 | "BAS y telemetría split por implementar" | BAS **ya existe** ([`ble_hid_service.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/ble_module/ble_hid_service.c) read + `ble_hid_notify_battery_level`), y `split_protocol.h` ya tiene `battery_pct` (heartbeat en `split_task.c#L249`) | El driver ADC reemplaza el símbolo `weak`; el resto es cableado |
| R13 | Wake de light-sleep por GPIO "negedge", "<10 µs" | En light sleep el wake por GPIO es **por nivel** (`gpio_wakeup_enable(..., LOW_LEVEL)`); la latencia es ~cientos de µs–ms (hay `ESP_SLEEP_WAIT_FLASH_READY_EXTRA_DELAY=2000`) | Rediseño del wake y de las métricas (§3.2) |
| R14 | ESP-NOW con "DTIM" en light sleep | ESP-NOW no tiene AP/DTIM; requiere `esp_now_set_wake_window` + intervalo de wake sincronizado. Además `CONFIG_BT_CTRL_MODEM_SLEEP` está **deshabilitado** hoy | Spike de viabilidad previo (§3.0) |
| R15–R17, R20 | Nuevas features en Fases 4–5 (Dongle, Encoders, Display, Trackball, Flasher web, extensión de batería) | La intencionalidad del plan es **mejorar el estado general y la calidad del firmware actual**, no agregar nuevas features ni periféricos de hardware | **Reemplazadas en v3 (R22):** Fases 4 y 5 pasan a enfocarse en concurrencia, memoria interna (DRAM), higiene arquitectónica y cobertura integral de tests |
| R18 | "Home-row mods" desde cero con *Permissive Hold* **y** *Hold-on-other-key-press* | Ya existen engine tap/hold/double-tap ([`kb_system_action.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_system_action.c)) y `CKEY_MODE_MULTI_ACTION`; ambas reglas son **flavors excluyentes**, no complementarios | Se audita lo existente y se agrega un resolvedor *interrupt-aware* (§2.4) |
| R19 | Tests y CI al final (Fases 5.2/5.3) | `test/run_tests.sh` falla hoy en el primer `#include` (`event_bus.h` no encontrado; faltan `-I../components/...`) | **Se adelantan a Fase 0**: sin red de seguridad no se debe tocar el núcleo |
| R21 | `atomic_uint_least16_t` | Xtensa solo tiene `S32C1I` nativo de 32 bits; atomics de 16 bits caen en emulación con spinlock; además `kb_macro_get_layer_mask()` **escribe** (limpia capas borradas) siendo un getter | `_Atomic uint32_t` + CAS (§1.3); tests host pasan a `-std=c11` |
| R22 | **Pivote v3 (Fases 4 y 5):** Agregar periféricos nuevos (batería extendida, encoders, dongle, display, trackball, flasher) | Auditoría del código actual revela deuda técnica real: condiciones de carrera en `comm_tx`, `kb_system_action` y hot-reload de NVS (`kb_combo`, `kb_custom_key`, `kb_macro`, `cfg_layouts`, `statusmod`), 30 KB de `.bss` en DRAM interna (`s_tx_buf`, `s_rx_buf`, `s_cfg_resp_buf`), acceso no serializado a TinyUSB, y módulos críticos sin tests unitarios | **Fase 4** sanea concurrencia, thread-safety, tareas redundantes y DRAM interna; **Fase 5** cierra brechas de tests unitarios, elimina código muerto/magic numbers y endurece el análisis estático |

---

## 1. Visión y Objetivos

TEF ya cuenta con USB NKRO, BLE multi-perfil (NimBLE), enlace split cifrado sobre ESP-NOW (X25519 + AES-128-CCM), protocolo *Blast+Reconcile* y un configurador web. El objetivo de este plan **no es agregar nuevas features de hardware**, sino consolidar la **calidad, estabilidad, seguridad de concurrencia, uso eficiente de memoria y mantenibilidad del firmware existente** en **6 fases secuenciales, cada una entregable y reversible**:

```mermaid
flowchart TD
    F0["Fase 0: Fundaciones (tests, CI, métricas, versión de protocolo)"] --> F1["Fase 1: Core y concurrencia inicial"]
    F1 --> F2["Fase 2: Latencia + Tap-Hold avanzado"]
    F2 --> F3["Fase 3: Gestión de energía (power_mgr)"]
    F3 --> F4["Fase 4: Saneamiento de concurrencia, DRAM interna y tareas"]
    F4 --> F5["Fase 5: Cobertura integral de tests, higiene de código y calidad"]
```

### Principios rectores
1. **Cero nuevas features de producto en Fases 4–5**: todo esfuerzo se dedica a eliminar deuda técnica, condiciones de carrera, consumo innecesario de DRAM interna y brechas de testing.
2. **Medir antes de optimizar**: cada optimización tiene una métrica base capturada en Fase 0.
3. **Un cambio = un commit atómico = una métrica o test que lo justifica.**
4. **Feature flags (Kconfig) para todo cambio de comportamiento observable** (`CONFIG_TEF_EAGER_DEBOUNCE`, `CONFIG_TEF_MATRIX_SETTLE_US`, `CONFIG_TEF_POWER_MGR`…), permitiendo rollback sin revertir.
5. **Definition of Done por fase** (§6): tests host verdes (`--asan` y `--tsan`), `idf.py build` sin warnings nuevos, documentación del módulo actualizada (`components/*/*_MODULE.md` y, si cambia la arquitectura, `universe/modules/`), medición adjunta.

---

## 2. Mapa de Fases y Módulos Afectados

| Fase | Componentes | Objetivo | Estado / Riesgo |
|---|---|---|---|
| **0** | `test/`, `.github/workflows/`, `split_protocol`, docs de hardware | Red de seguridad, línea base medida, versión de protocolo split | **Completada** (Bajo) |
| **1** | `keyboard` (`kb_manager`, `kb_macro`, `kb_matrix`), `split_config_sync`, `comm_module` | Cero bloqueos en el scan loop; sync completo de custom keys; concurrencia inicial | **Completada** (Bajo-Medio) |
| **2** | `keyboard` (`kb_matrix`, `kb_manager`, `kb_debounce`, `kb_tap_hold`), `config_module`, `configurator` | Latencia interna < 1.2 ms (p99); resolvedor Tap-Hold con flavors | **Completada** (Medio) |
| **3** | `power_mgr`, `battery_module` (opcional/deshabilitado sin pin), `split`, `ble_module` | Gestión unificada de frecuencia (`power_mgr`) y estados de reposo | **Completada** (Medio) |
| **4** | `comm_module`, `usb_module`, `keyboard` (`kb_combo`, `kb_custom_key`, `kb_macro`, `kb_system_action`), `config_module`, `status_module`, `rgb_module` | Eliminar data races multi-hilo, serializar TinyUSB, liberar ≥30 KB de DRAM interna (`.bss` → PSRAM) y consolidar tareas redundantes | **Completada** (Medio) |
| **5** | `test/` (`keyboard`, `comm`, `split`, `config`), `main/`, `cfgmod`, `ble_module`, `button_module`, CI | Cobertura unitaria de motores críticos (`kb_combo`, `kb_macro`, `kb_custom_key`, `Blast+Reconcile`, `split_role`), higiene de código y endurecimiento de compilación | **Completada** (Bajo-Medio) |

---

## 3. Desglose por Fases

---

### FASE 0: Fundaciones

#### Checklist de Ejecución de Fase 0:
- [x] **0.1. Sanear la suite de tests host y mocks con ESP-IDF 6.x**
  - [x] Includes dinámicos de componentes y flag `-std=gnu11` en `test/run_tests.sh`.
  - [x] Soporte para AddressSanitizer y UndefinedBehaviorSanitizer (`--asan`).
  - [x] Mock PSA Crypto (`psa/crypto.h`) para MbedTLS 3.x (ECDH, SHA256-KDF, AES-128-CCM).
  - [x] Actualización de mocks `mock_comm`, `mock_esp`, `mock_tinyusb`, `cfgmod`.
  - [x] Resolución de bitrot y 134/134 tests pasando con 0 fallos.
- [x] **0.2. Pipeline de CI en GitHub Actions**
  - [x] Crear `.github/workflows/ci.yml`.
  - [x] Job `host-tests`: ejecución con `--asan` y subida de reporte JUnit (`test_results.xml`).
  - [x] Job `firmware-build`: compilación en contenedor `espressif/idf:v6.0.2` con `idf.py build` y `size-components`.
  - [x] Job `configurator`: `npm ci`, `npm run lint` y `npm run build`.
- [x] **0.3. Línea base de métricas e instrumentación**
  - [x] Kconfig `CONFIG_TEF_DEBUG_LATENCY_PIN` y conmutación de nivel en `kb_macro_virtual_press` al entrar nueva pulsación al NKRO.
  - [x] Kconfig `CONFIG_TEF_MATRIX_SETTLE_US` (default 20 µs) en `kb_matrix.c`.
  - [x] Módulo `components/keyboard/kb_telemetry.c` con histograma y tiempo bloqueado de `kb_macro_send_report`.
  - [x] Documento de caracterización `implementations/baseline/BASELINE_METRICS.md` con inventario de tareas, presupuesto de CPU y consumos PPK2.
- [x] **0.4. Presupuesto de pines y decisión de hardware**
  - [x] Documento `implementations/PIN_BUDGET_HARDWARE_DECISION.md`.
  - [x] Validación de pines bloqueados por Octal PSRAM (GPIO26–37).
  - [x] Resolución de ADC1 ocupado vs I2C Fuel-Gauge / Split column remap.
  - [x] Asignación de I2C Display (GPIO43/44), Encoders (GPIO46/47 o Matriz) y wake RTC (GPIO1–6).
- [x] **0.5. Versionado del protocolo split**
  - [x] Definición de `SPLIT_PROTO_VERSION` (0x02) en `split_protocol.h`.
  - [x] Campo `proto_version` en `split_discovery_payload_t`, `split_role_negotiate_payload_t` y `split_heartbeat_payload_t`.
  - [x] Verificación y rechazo de versiones incompatibles con transición a `SPLIT_STATE_VERSION_MISMATCH` (0x06).
  - [x] Soporte de `SPLIT_STATE_VERSION_MISMATCH` en el configurador web (`protocol.ts`, `SplitDashboard.tsx`, `DeviceDashboard.tsx`).
  - [x] Tests unitarios dedicados de versionado en `test/split/test_split_protocol.c`.

#### 0.1. Sanear la suite de tests host (antes: 5.2)
* **Estado verificado:** `cd test && bash run_tests.sh` → `fatal error: event_bus.h: No such file or directory`. `run_tests.sh` solo pasa `-Iinclude`.
* **Acciones:**
  1. Agregar a `INCLUDES` todos los `-I../components/<mod>/include` (event_bus, keyboard, config_module, ble_module, usb_module, status_module, split, comm_module…). Idealmente generarlos con un `find ../components -maxdepth 3 -name include`.
  2. Cambiar `-std=c99` → **`-std=gnu11`** (necesario para `<stdatomic.h>` en 1.3 y habitual en el código de IDF).
  3. Corregir bitrot conocido: inclusiones de `usb_module` en [`test/main.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/test/main.c), `process_rx_request` → `comm_process_rx_request` en [`test_usb_rx.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/test/usb/test_usb_rx.c), constantes `KB_LAYER_FN1`/`SYS_ACTION_LAYER_FN1` en [`test_kb_layout.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/test/keyboard/test_kb_layout.c) (migrar a la API de máscara de capas).
  4. Alinear mocks (`test/include/`) con **IDF 6.x**; no "5.5".
  5. Agregar `-fsanitize=address,undefined` en un modo `run_tests.sh --asan`.
* **Aceptación:** `bash run_tests.sh` = 0 fallos en Linux; el resultado JUnit (`test_results.xml`) se sube como artefacto de CI.

#### 0.2. CI mínimo desde el día uno (antes: 5.3)
* Crear `.github/workflows/ci.yml` (hoy **no existe** `.github/`), ejecutado en cada PR:
  1. **host-tests** (`ubuntu-latest`, gcc).
  2. **firmware-build**: contenedor `espressif/idf:v6.0` (o la versión exacta fijada en `dependencies.lock`), `idf.py build`, `-Werror` sobre warnings **nuevos** (baseline guardado), y reporte de `idf.py size-components` comparado contra el `main` (falla si DRAM interna libre cae > X KB; el firmware ya vive al límite de SRAM con NimBLE + Wi-Fi).
  3. **configurator**: `npm ci && npm run lint && npm run build` (+ `tsc --noEmit`).
* Cache de `managed_components/` y `~/.espressif`/ccache para tiempos razonables.

#### 0.3. Línea base de métricas (instrumentación)
* El scan loop ya contabiliza `min/max scan interval` y reportes/s; extender con:
  - **Pin de depuración** (GPIO de test, `CONFIG_TEF_DEBUG_LATENCY_PIN`) que se alterna cuando una pulsación nueva entra al NKRO → medir con analizador lógico *interruptor ↔ pin* y *interruptor ↔ SOF USB*.
  - Contador de **tiempo máximo bloqueado** en `kb_macro_send_report` (histograma).
  - % de CPU del core del `kb_manager` mientras hay teclas activas (`uxTaskGetSystemState`/`vTaskGetRunTimeStats`).
  - Inventario real de tareas (`vTaskList`): el "hasta 16 tareas" de v1 **no estaba verificado**; adjuntar la tabla con stack/high-water-mark y ubicación (DRAM/PSRAM).
  - Consumo base con PPK2 en USB, BLE conectado idle y BLE + split.
* Guardar resultados en `implementations/baseline/` (CSV + capturas) para la matriz de §7.

#### 0.4. Presupuesto de pines y decisión de hardware (**bloqueante para Fases 3 y 4**)
* **Mapa actual:** filas GPIO1–6; columnas GPIO7–18, 21, 38–42; RGB GPIO48; BOOT GPIO0; USB D±/GPIO19–20.
* **Libres (a validar contra el modo de PSRAM Quad/Octal y strapping):** GPIO43/44 (UART0), 45/46 (strapping, usar con cuidado), 47, y 33–37 solo si la PSRAM es Quad.
* **Entregable:** tabla de asignación para: ADC batería (1 pin, **debe ser ADC1/GPIO1–10 → hoy ocupados**), I²C/SPI de display (2–4 pines), encoders (2 pines c/u), trackball PMW3360 (SPI + MOT), wake de deep sleep. Opciones si no alcanzan: fuel-gauge I²C (MAX17048, comparte bus con OLED), expansor I/O (el encoder puede ir en la propia matriz), o revisión de PCB. **Sin esta tabla no se arranca 3.1 ni 4.x.**

#### 0.5. Versionado del protocolo split
* Antes de agregar paquetes (telemetría, encoders, mouse), añadir un campo `proto_version` al handshake/heartbeat de `split_protocol.h` y rechazar mitades incompatibles con un estado visible en el configurador. Evita que una mitad actualizada y otra no queden en silencio inconsistentes.

---

### FASE 1: Core del Firmware, Concurrencia y Sincronización

#### Checklist de Ejecución de Fase 1:
- [x] **1.1. Bug de sincronización de Custom Keys**
  - [x] Reemplazar cotas literales (64, 32) por constantes de dominio `CFG_CKEYS_MAX_COUNT` (120), `CFG_MACROS_MAX_COUNT` (64) y `CFG_COMBOS_MAX_COUNT` (32) en `components/split/split_config_sync.c`.
  - [x] Instrumentación y medición temporal de `split_config_sync_push_all` con `esp_timer_get_time()`.
  - [x] Tests unitarios en `test/split/test_split_config_sync.c` verificando sync completo de custom keys > 64 (`ck_64`, `ck_65`, `ck_119`), macros y combos.
- [x] **1.2. Envío de reportes sin bloqueo y reintento acotado**
  - [x] Implementación de `kb_macro_send_report_nowait()` (un único intento no bloqueante con snapshot de `s_v_nkro`) y `kb_macro_send_report_blocking(timeout_ms)` en `components/keyboard/kb_macro.c`.
  - [x] Desacople de scan loop en `components/keyboard/kb_manager.c`: actualización inmediata de matriz física en `s_last_matrix` para evitar regeneración redundante de eventos, y manejo de fallo de transporte vía `s_report_pending` con backoff exponencial acotado (1 ms -> 2 ms -> ... -> 16 ms).
  - [x] Tests unitarios en `test/keyboard/test_kb_report.c` validando entrega inmediata cuando está listo, fallo inmediato sin bloqueo, y reintentos acotados.
- [x] **1.3. Estado de capas dinámicas thread-safe y atómico**
  - [x] Conversión de `s_momentary_layers` y `s_toggled_layers` a `_Atomic uint32_t` con `stdatomic.h` en `components/keyboard/kb_macro.c`.
  - [x] `kb_macro_get_layer_mask(void)` convertido en getter puro sin efectos secundarios (`memory_order_relaxed`).
  - [x] Purga de capas eliminadas trasladada a `kb_macro_purge_deleted_layers(void)` suscrito al evento `CONFIG_EVENT_KIND_UPDATED` de layouts en el `event_bus`.
  - [x] Tests multihilo con `pthread` y verificación bajo ThreadSanitizer (`--tsan`) en `test/keyboard/test_kb_layout.c`.
- [x] **1.4. FreeRTOS: Reemplazo de polling en comm_timeouts_task por esp_timer**
  - [x] Eliminación de `comm_timeouts_task` (liberando 4 KB de DRAM interna crítica) y del bucle de sondeo cada 5 ms.
  - [x] Temporizador one-shot `s_comm_timeout_timer` con `esp_timer` programado al plazo mínimo (`min(rx_deadline, tx_deadline)`).
  - [x] Encolado de mensaje de control (1 byte `0xFF`) a `s_comm_message_buffer` preservando un único consumidor seguro en `comm_proc_task`.
- [x] **1.5. Instalación autónoma del servicio de ISR**
  - [x] Invocación defensiva de `gpio_install_isr_service(0)` en `kb_matrix_init_isr()` en `components/keyboard/kb_matrix.c`, tolerando `ESP_ERR_INVALID_STATE`.
  - [x] Tolerancia a `ESP_ERR_INVALID_STATE` en `components/button_module/button.c` para inicialización independiente y agnóstica al orden de arranque.

#### 1.1. Bug de sincronización de Custom Keys (quick win, **primer commit de la fase**)
* **Diagnóstico confirmado:** [`split_config_sync.c#L264`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/split/split_config_sync.c#L264) itera `j < 64` pero `CFG_CKEYS_MAX_COUNT = 120`. Las ckeys 64–119 nunca llegan al esclavo.
* **Acciones:**
  1. Reemplazar los literales por constantes: ckeys → `CFG_CKEYS_MAX_COUNT`; macros (`64`) → `CFG_MACROS_MAX_COUNT`; combos (`32`) → `CFG_COMBOS_MAX_COUNT`. Hoy coinciden, pero son latentes.
  2. **No** agregar `vTaskDelay` cada 8 paquetes: `split_config_sync_push` ya es stop-and-wait por ACK (con `tx_sem`). Lo que sí hay que **medir** es el tiempo total de `push_all` con 120 ckeys + 64 macros (con `esp_timer`), y fijar un presupuesto (p. ej. < 10 s) para no degradar el *pairing UX*.
  3. Test host: poblar `ck_idx` con bits > 64 y verificar que se generan los `ck_N` correspondientes.
* **Aceptación:** esclavo con las 120 ckeys idénticas al master (checksum de NVS).

#### 1.2. Envío de reportes sin bloqueo (replanteado)
* **Diagnóstico confirmado:** [`kb_macro_send_report()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L78-L98) reintenta 100×`vTaskDelay(1)`. `kb_send_report` retorna `ESP_ERR_INVALID_STATE` (BLE ruteado pero desconectado) o `ESP_FAIL` (USB no montado / no `ready`) → **cada reporte con cambio bloquea hasta 100 ms** el scan loop (llamado desde [`kb_manager.c#L447`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_manager.c#L447)).
* **Bug latente adicional:** tras el envío, `kb_manager` hace `memcpy(s_last_matrix, s_matrix)` **aunque `result != ESP_OK`**. Si falla el reporte de *release* y no hay más cambios, el host queda con tecla pegada hasta el *keepalive* (`MIN_REPORT_RATE_HZ`).
* **Por qué no el mailbox de v1:** el payload se reconstruye desde `s_v_nkro` (estado compartido protegido por mutex) y lo disparan 4 contextos distintos; una cola de longitud 1 con `xQueueOverwrite` puede **perder una pulsación breve** (press+release entre dos despachos) y duplica el estado.
* **Diseño (en dos pasos, el 2º condicionado a medición):**
  1. **Paso A (≈20 líneas):** `kb_macro_send_report_nowait()` = un único intento, sin reintentos. En `kb_manager`: si falla, **no** actualizar `s_last_matrix` y marcar `s_report_pending`; reintentar en el siguiente scan con backoff acotado (1→2→4→…→16 ms). Los contextos `macro_task`/`tap_worker`/`kb_custom_key` (que ya corren en tareas propias) mantienen una variante con reintento acotado `kb_macro_send_report_blocking(timeout_ms)` porque ahí **no se puede descartar** un press/release de macro.
  2. **Paso B (solo si la medición 0.3 muestra bloqueo residual en `ble_hid_send_*`/TinyUSB):** `kb_transport_task` despertada por **`xTaskNotify` con *dirty flag*** (no con cola de payload); al despertar toma un snapshot del `s_v_nkro` y envía. Efecto colateral deseado: **serializa el acceso a TinyUSB**, que hoy se invoca desde varias tareas (TinyUSB no es thread-safe para `tud_hid_*`). Para no perder un *press* breve, el transporte nunca coalesce si el snapshot pendiente contiene una tecla que el nuevo no tiene: en ese caso envía ambos en orden.
* **Aceptación:** con USB desconectado y BLE routing activo, `kb_manager` mantiene el período de scan (p99 < 2×) mientras se teclea; sin teclas pegadas tras reconectar.

#### 1.3. Estado de capas dinámicas thread-safe
* **Diagnóstico confirmado:** [`s_momentary_layers`/`s_toggled_layers`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L28-L29) se modifican con `|=`, `&=`, `^=` desde varios contextos/núcleos. Además **`kb_macro_get_layer_mask()` escribe** (líneas ~111-116: limpia capas borradas), es decir, un *getter* con efectos secundarios invocado desde cualquier tarea.
* **Acciones:**
  1. Usar `_Atomic uint32_t` (acceso nativo `S32C1I` en Xtensa; los de 16 bits caen en `libatomic` con spinlock) con `atomic_fetch_or/and/xor_explicit`.
  2. El "toggle" es read-modify-write → `atomic_fetch_xor`. La purga de capas borradas se mueve fuera del getter (se ejecuta una sola vez en el handler del evento de actualización de layouts, único escritor).
  3. `memory_order_relaxed` es correcto para la máscara aislada; si se agrega correlación con otros datos (p. ej. índice de keymap cacheado), usar `acquire/release`.
  4. Test host con dos hilos pthread hammereando set/clear (`-fsanitize=thread`).

#### 1.4. Tareas FreeRTOS: reemplazar polling por plazos (prioridad media-baja)
* **`comm_timeout_task`** ([`comm_dispatch.c#L143-L160`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_dispatch.c#L143-L160)): chequea **RX y TX** cada 5 ms, 4 KB de DRAM interna. Sustituir por **un `esp_timer` one-shot** re-armado al *próximo plazo* (min(rx_deadline, tx_deadline)), iniciado en el primer fragmento de cada ráfaga y cancelado al completar.
  - ⚠️ El callback corre en la tarea de `esp_timer`; hoy `comm_erase_rx_buffer()`/`comm_session_unlock()` se ejecutan desde otro contexto que `comm_proc_task`. Para mantener un **único consumidor**, el callback solo debe **encolar un mensaje de timeout** al `s_comm_message_buffer` y que `comm_proc_task` ejecute el reset.
* **`kb_tap_0..3`:** no es un *pool* ocioso: 4 workers permiten taps concurrentes con `vTaskDelay(dur)` dentro. **Un solo worker serializaría los taps** (regresión de latencia con taps simultáneos). La alternativa correcta es **eliminar el delay bloqueante**: press inmediato + liberación programada con una **lista de plazos** (heap mínimo) atendida por una única tarea o `esp_timer`. Ahorro real: ~12 KB de **PSRAM** (no DRAM). Hacerlo solo si la medición 0.3 justifica el esfuerzo.

#### 1.5. Instalación autónoma del servicio de ISR
* [`kb_matrix_init_isr()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_matrix.c#L166) asume que `button.c` ya llamó `gpio_install_isr_service(0)`. Invocarlo en `kb_matrix_init_isr()` ignorando `ESP_ERR_INVALID_STATE`. Verificar el flag IRAM de la ISR (`IRAM_ATTR` ya presente) con `CONFIG_GPIO_CTRL_FUNC_IN_IRAM` si se activa light sleep (§3.2).

---

### FASE 2: Latencia Competitiva + Tap-Hold Avanzado

#### Checklist de Ejecución de Fase 2:
- [x] **2.1. Tiempo de asentamiento parametrizable y medido**
  - [x] Kconfig `CONFIG_TEF_MATRIX_SETTLE_US` (default 20 µs) en `components/keyboard/Kconfig`.
  - [x] Aplicación de `CONFIG_TEF_MATRIX_SETTLE_US` en `components/keyboard/kb_matrix.c`.
  - [x] Mantener compatibilidad host test mediante mock/default fallback.
- [x] **2.2. Eager Debounce (activación inmediata en el primer scan)**
  - [x] Kconfig `CONFIG_TEF_EAGER_DEBOUNCE`, `CONFIG_TEF_PRESS_CONFIRM_SCANS` (default 1) y `CONFIG_TEF_RELEASE_DEBOUNCE_SCANS` (default 5).
  - [x] Implementación en `components/keyboard/kb_debounce.c` y `components/keyboard/include/kb_debounce.h`.
  - [x] Integración de `kb_debounce_update` y `kb_debounce_init` en `components/keyboard/kb_manager.c`.
  - [x] Tests unitarios dedicados en `test/keyboard/test_kb_debounce.c` cubriendo press inmediato, 5 scans de release consecutivo, rebotes durante release y reseteo.
- [x] **2.3. Lectura directa del banco de GPIO con HAL optimizado**
  - [x] Implementación de `components/keyboard/include/kb_matrix_hal.h` y `components/keyboard/kb_matrix_hal.c`.
  - [x] Lectura directa de registros de entrada en ESP-IDF (`REG_READ(GPIO_IN_REG)` decodificando `(~in >> 1) & 0x3F`) para GPIO1-6.
  - [x] Escritura directa por registros a bancos bajo (`W1TC/W1TS` GPIO0-31) y alto (`W1TC/W1TS` GPIO32-48) para selección rápida de columnas.
  - [x] Integración en `kb_matrix.c` y tests unitarios en `test/keyboard/test_kb_matrix.c`.
- [x] **2.4. Resolvedor Tap-Hold con flavors, búfer diferido y sincronización cross-stack**
  - [x] Búfer de eventos diferidos (16 entradas) y máquina de estados interrupt-aware en `components/keyboard/kb_tap_hold.c`.
  - [x] Soporte para los 4 flavors: `TIMER` (0), `HOLD_ON_OTHER_KEY_PRESS` (1), `PERMISSIVE_HOLD` (2), `TAP_PREFERRED` (3).
  - [x] Soporte para `quick_tap_ms` (repetición inmediata/auto-repeat sin retardo) y `require_prior_idle_ms` (prevención de falsos holds en tipeo veloz).
  - [x] Integración en el scan loop de `components/keyboard/kb_manager.c` con guardas para light sleep (`kb_tap_hold_is_active()`).
  - [x] Actualización de `cfg_ckey_ma_t` (40 bytes) y `cfg_custom_key_t` (76 bytes) en `components/config_module/` con migración retrocompatible de NVS (72 bytes).
  - [x] Actualización del esquema binario y UI del configurador (`customKeys.ts`, `BinarySchema.ts`, `DeviceController.ts`, `CustomKeysDashboard.tsx`).
  - [x] Documentación actualizada en `COMM_PROTOCOL.md`.
  - [x] Tests unitarios deterministas completos en `test/keyboard/test_kb_tap_hold.c` (11 tests cubriendo todos los modos y secuencias complejas).

> **Ojo con la expectativa:** el objetivo realista es la **latencia interna** (matriz → bytes de reporte entregados al stack USB/BLE). La latencia extremo a extremo la domina el transporte (USB: ≤1 ms de polling; BLE: intervalo de conexión 7.5–15 ms).

#### 2.1. Tiempo de asentamiento parametrizable y medido (**mayor ganancia, va primero**)
* **Diagnóstico:** [`esp_rom_delay_us(20)`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_matrix.c#L77) × 18 columnas = **360 µs por scan activo** (36 % del core) como *busy-wait*.
* **Acciones:**
  1. Kconfig `CONFIG_TEF_MATRIX_SETTLE_US` (default 20 = comportamiento actual).
  2. Con osciloscopio, medir el flanco de fila (pull-up interno ≈45 kΩ + capacidad parásita) en el peor caso (fila más larga, tecla con diodo) y elegir `settle = 1.5 × t_medido`. **No** fijar 5–8 µs sin medición: la nota actual del código estima ~13 µs.
  3. Prueba de estrés: barrer 10 000 pulsaciones de cada tecla con el generador de inyección (`SYS_CMD_INJECT_KEY` no sirve para el nivel físico; usar un banco con solenoides o un *matrix-shorting* con GPIOs de test) y exigir 0 *misses*/*ghosts*.
  4. Opcional: *scan pipelining* (leer filas de la columna *n* mientras ya se activa la *n+1* solo si el hardware lo permite con diodos).
* **Expectativa recalculada (R6):** con 8 µs ≈ 18×8 = 144 µs ≈ **14 % del core mientras hay teclas activas**; en reposo ya es ~0 % (ISR + notify). Objetivo: **< 15 %**, no "<8 %".

#### 2.2. Eager Debounce (simplificado)
* **Diagnóstico:** el integrador de [`debounce_update`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_manager.c#L143-L160) requiere `KB_DEBOUNCE_SCANS = 5` incrementos para marcar *press*: latencia de entrada ≈ 4–5 ms.
* **Diseño — cambia una línea, no hace falta una ventana de lockout aparte:** al pasar de 0 → activo, saltar el contador **directamente a `KB_DEBOUNCE_SCANS`** (la tecla queda estable en el primer scan). Como el *release* solo ocurre cuando el contador llega a 0, los rebotes posteriores **ya quedan filtrados** por el propio integrador (el contador solo baja 1 por scan en bajo y se recupera en alto). Una ventana fija de 5 ms como en v1 solo retrasaría *releases* legítimos rápidos.
* **Riesgo y mitigación:** un solo glitch de EMI ahora registra una tecla. Parámetro `CONFIG_TEF_PRESS_CONFIRM_SCANS` (default 1; subir a 2 si el banco de EMI lo exige) y `release_scans` independiente (default 5).
* **Interacciones a revisar:** `kb_combo` (ventana de combos) y los timers de `kb_system_action` pueden depender implícitamente del retardo de 5 ms; correr sus tests antes/después.
* **Objetivo medido (0.3):** matriz → pin de depuración **p99 < 1.2 ms** (≤ 1 período de scan + 1 scan activo), no "< 0.5 ms".

#### 2.3. Lectura directa del banco de GPIO
* Filas = GPIO1–6, todas < 32: **una** lectura de `GPIO_IN_REG` (o `GPIO.in`) por columna y `rows = (~in >> 1) & 0x3F`. **No usar `in1`** (R8; además el campo no se llama `data` en los headers S3 — verificar en `soc/gpio_struct.h` de IDF 6).
* Reducir también las escrituras de columnas: GPIO7–18/21 están en el banco bajo (`out_w1tc/w1ts`) y GPIO38–42 en el alto (`out1_w1tc/w1ts`).
* **Ganancia esperada ~10–15 µs por scan** (≪ 2.1). Se hace **después** de 2.1/2.2 y solo si el perfil lo justifica; encapsular en `kb_matrix_hal` para poder testear con mock.

#### 2.4. Resolvedor Tap-Hold con *flavors* (antes: 4.4)
* **Estado actual (auditado):** existe el engine tap/double-tap/hold por timeout ([`kb_system_action.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_system_action.c), defaults 300/500 ms) y `CKEY_MODE_MULTI_ACTION` con `hold_sustain`. **Lo que falta** es decidir el hold según **otras teclas** (interrupt-aware), imprescindible para home-row mods.
* **Diseño:**
  1. Un **buffer de resolución de eventos** en `kb_manager` *antes* de `kb_macro_process_action`: mientras una tecla tap-hold esté "indecisa", los eventos siguientes se encolan y se liberan en orden al decidirse.
  2. **Flavor por tecla** (campo nuevo en el esquema, enum excluyente — no reglas acumulables):
     - `TIMER` (hold solo por `tapping_term`, comportamiento actual),
     - `HOLD_ON_OTHER_KEY_PRESS` (hold apenas se presiona otra tecla),
     - `PERMISSIVE_HOLD` (hold si otra tecla se presiona **y suelta** dentro del *tapping term*),
     - `TAP_PREFERRED/BALANCED` (opcional).
  3. Parámetros por tecla: `tapping_term` (default 200 ms), `quick_tap_ms` (evita hold al repetir/arrastrar), `require_prior_idle_ms` (**clave anti-misfire en tipeo rápido**).
  4. Cambios cross-stack: esquema de `cfg_custom_keys` (**bump de versión de protocolo**), UI del configurador (flavor, términos), tests host con secuencias deterministas (roll A+S, tap solo, hold, solapamientos).
* **Nota:** el resolvedor añade latencia **por diseño** a esas teclas (se emite al soltar o al decidir); por eso depende de 2.2 y se mide aparte.

---

### FASE 3: Autonomía y Eficiencia Energética

#### Checklist de Ejecución de Fase 3:
- [x] **3.0. Spike de viabilidad (Autonomía y Eficiencia Energética)**
  - [x] Documento de caracterización `implementations/baseline/POWER_SPIKE_FEASIBILITY.md`.
  - [x] Análisis de viabilidad de BLE + Modem Sleep y fuente de reloj RC vs XTAL 32 kHz.
  - [x] Análisis de ESP-NOW + Light Sleep y latencia en esclavo.
  - [x] Aislamiento de sleep bajo conexión USB activa para proteger PHY de TinyUSB.
  - [x] Medición y caracterización de latencia de wake-up desde Light Sleep (~1.5 ms) y Deep Sleep.
- [x] **3.1. Driver real de batería**
  - [x] Kconfig `CONFIG_TEF_BATTERY_ENABLE`, `CONFIG_TEF_BATTERY_ADC_GPIO` (default -1 para prevenir conflicto con Col 0 GPIO 7), `CONFIG_TEF_BATTERY_DIVIDER_RATIO_PERMILLE` (default 2000), `CONFIG_TEF_BATTERY_POLL_INTERVAL_SEC` (default 60), `CONFIG_TEF_BATTERY_DELTA_NOTIFY_PCT` (default 2).
  - [x] Driver `components/battery_module/battery.c` con API moderna ESP-IDF 6.x `adc_oneshot` y calibración `adc_cali_create_scheme_curve_fitting`.
  - [x] Filtro de mediana de 5 muestras para supresión de picos de corriente de transmisión RF.
  - [x] Tabla LUT de descarga LiPo con interpolación lineal precisa (4.20 V -> 100%, 3.85 V -> 50%, 3.30 V -> 0%).
  - [x] Filtro de monotonía e histéresis para eliminar fluctuaciones/rebotes de porcentaje durante descarga y detección de carga/VBUS.
  - [x] Publicación periódica por `esp_timer` (60 s) o umbral ($\Delta \ge 2\,\%$) a perfiles BLE (`ble_hid_notify_battery_all()`) y evento `BATTERY_EVENTS` / `BATTERY_EVENT_LEVEL_CHANGED` en el `event_bus`.
  - [x] Reemplazo de `.battery_pct = 0xFF` hardcodeado en `split_dispatch.c` por `battery_get_level_pct()`.
  - [x] Documentación de arquitectura en `components/battery_module/BATTERY_MODULE.md`.
  - [x] Tests unitarios dedicados en `test/battery/test_battery.c` (7 tests).
- [x] **3.2. Gestor de energía unificado (`power_mgr`)**
  - [x] Kconfig `CONFIG_TEF_POWER_MGR`, `CONFIG_TEF_PM_IDLE_TIMEOUT_MS` (5000 ms), `CONFIG_TEF_PM_LIGHT_SLEEP_TIMEOUT_MS` (30000 ms), `CONFIG_TEF_PM_DEEP_SLEEP_TIMEOUT_MS` (900000 ms = 15 min).
  - [x] Implementación de `components/power_mgr/power_mgr.c` y `include/power_mgr.h`.
  - [x] Eliminación de `pm_apply_active()` duplicada en `split_task.c` y `split_bridge.c`, reemplazada por `power_mgr_activity_notify()`.
  - [x] Dueño único de `esp_pm_configure()` y gestión dinámica de frecuencia mediante lock `ESP_PM_CPU_FREQ_MAX`.
  - [x] Máquina de estados: ACTIVE (240 MHz lock), IDLE (80 MHz DFS tickless tras 5s), LIGHT SLEEP (WFI tras 30s inalámbrico), DEEP SLEEP (>15 min o corte de batería crítica al 0%).
  - [x] Wake de Light Sleep con deshabilitación inmediata de wake por nivel en filas (GPIO 1..6) para prevenir bucles de interrupción (R13) y discriminación de causa de wake (`ESP_SLEEP_WAKEUP_GPIO` vs no-GPIO).
  - [x] Wake de Deep Sleep mediante RTC EXT1 en filas (GPIO 1..6) con pad hold activo en columnas (`gpio_hold_en` + `gpio_deep_sleep_hold_en`) y unhold al reiniciar.
  - [x] Integración con `kb_manager.c`, `usbmod.c`, y `event_bus`.
  - [x] Documentación de arquitectura en `components/power_mgr/POWER_MGR_MODULE.md`.
  - [x] Tests unitarios completos en `test/power/test_power_mgr.c` (10 tests).

#### 3.0. Spike de viabilidad (2–3 días, **gate de la fase**)
Los consumos de v1 eran hipótesis. Antes de diseñar, medir con PPK2 y validar:
1. **BLE + light sleep:** hoy `CONFIG_BT_CTRL_MODEM_SLEEP` está **sin definir** y el reloj de sleep BLE es el efectivo `0`; confirmar qué fuente de 32 kHz (XTAL externo vs. RC interno) tiene el hardware, porque determina si el chip *puede* dormir con BLE conectado y con qué precisión de conexión.
2. **ESP-NOW + light sleep:** viabilidad real de `esp_now_set_wake_window()` + `esp_wifi_connectionless_module_set_wake_interval()` y su efecto en la **latencia de la primera pulsación del esclavo** (si el master duerme su radio Wi-Fi, el evento espera al siguiente wake).
3. **USB conectado:** con USB (TinyUSB) activo no hay light sleep → la política aplica solo al modo inalámbrico.
4. Latencia de wake real desde light sleep (`ESP_SLEEP_WAIT_FLASH_READY_EXTRA_DELAY=2000` sugiere ms, no µs).
* **Salida:** tabla *medida* de consumo y latencia de wake por estado; si no alcanza, el diseño de 3.2 se reduce a DFS + tickless (sin light sleep).

#### 3.1. Driver real de batería
* **Hoy:** [`battery_get_level_pct()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/battery_module/battery.c) es un símbolo `weak` que devuelve `0xFF`. **BAS ya existe** (read + `ble_hid_notify_battery_level` al suscribirse) y el *heartbeat* split ya transporta `battery_pct` — solo falta el driver y la publicación periódica (R12).
* **Diseño:**
  1. **Pin (bloqueante, 0.4):** ADC1 = GPIO1–10 está 100 % ocupado por la matriz. Opciones: sacrificar un pin de columna, mover a fuel-gauge I²C, o revisión de PCB. ADC2 no es opción (conflicto con Wi-Fi/ESP-NOW).
  2. **API IDF 6:** `adc_oneshot` + calibración `adc_cali_create_scheme_curve_fitting` (S3). Eliminar toda referencia a `esp_adc_cal_*`.
  3. **Divisor de alta impedancia:** 100 k/100 k drena ≈21 µA (≈ el presupuesto completo de Deep Sleep). Usar ≥ 1 MΩ/1 MΩ **con capacitor ≥100 nF** como reservorio de carga del S/H, o un **MOSFET de gateo** habilitado solo al muestrear.
  4. **Estimación de %:** LUT con interpolación lineal (4.20→100, 3.85→50, 3.60→10, 3.30→0 como punto de partida, **a calibrar con una descarga real**); mediana de N muestras (no solo IIR, por los picos de TX de radio); muestrear en un instante de baja carga (tras TX); **histéresis y monotonía** (el % no sube mientras descarga); considerar detección de carga (VBUS) para no mostrar valores falsos enchufado.
  5. **Publicación:** timer de 60 s o Δ ≥ 2 % → `ble_hid_notify_battery_level`; el esclavo ya reporta `battery_pct` en el heartbeat (reemplazar el `0xFF` hardcodeado en `split_dispatch.c#L260`).
  6. Reemplazar `weak` por una implementación con **Kconfig de pin/divisor** (`CONFIG_TEF_BATTERY_ADC_GPIO`, `…_DIVIDER_RATIO`); si no hay pin configurado, mantiene `0xFF`.

#### 3.2. Gestor de energía unificado (`power_mgr`)
* **Hoy:** `pm_apply_active()` está **duplicada** en [`split_task.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/split/split_task.c#L84) y [`split_bridge.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/split/split_bridge.c#L37) (`light_sleep_enable = false`, 240 MHz fijo), y `CONFIG_PM_DFS_INIT_AUTO` está desactivado.
* **Diseño idiomático:** un único dueño de `esp_pm_configure()` (configurado **una vez** con `light_sleep_enable = true` si 3.0 lo valida) y control de estados con **`esp_pm_lock`**: `ESP_PM_CPU_FREQ_MAX` adquirido mientras haya actividad (tecla, tráfico USB/BLE) y liberado tras un *grace period*. Evita reconfigurar PM repetidamente y es la forma soportada de forzar frecuencia.
* **Máquina de estados (valores marcados *hipótesis* hasta 3.0):**

| Estado | CPU | Radios | Entrada | Consumo (hipótesis) |
|---|---|---|---|---|
| **ACTIVE** | 240/160 MHz | BLE + ESP-NOW | Tecla o tráfico | 65–80 mA |
| **IDLE** | 80 MHz + tickless | BLE en intervalo, ESP-NOW activo | 5 s sin teclas | 15–20 mA |
| **LIGHT SLEEP** | WFI/PM | Wake-interval acordado (§3.0) | 30 s sin teclas, **solo modo inalámbrico** | 1.5–2.5 mA |
| **DEEP SLEEP** | RTC | Apagadas | > 15 min | < 20 µA (con divisor de 3.1) |

* **Detalles de implementación corregidos (R13):**
  - **Light sleep:** `gpio_wakeup_enable(row_gpio, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()`; columnas mantenidas en bajo (el modo ISR actual de `kb_matrix_set_interrupts_enabled(true)` ya lo hace). Tras el wake, **deshabilitar el wake por nivel** hasta soltar la tecla para no re-despertar en bucle.
  - **Deep sleep:** filas GPIO1–6 son pines RTC → `esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW)`; mantener columnas en bajo con `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`; pull-ups de filas vía `rtc_gpio_pullup_en`.
  - **UX:** la primera tecla tras Deep Sleep se pierde (boot + reconexión BLE ≈ 1–2 s). Debe documentarse y reservarse al umbral largo.
  - **Latencia de primera pulsación desde Light Sleep:** métrica explícita (§7), no "<10 µs".
  - ISR y rutas de wake en IRAM (`CONFIG_PM_SLP_IRAM_OPT`, `PM_SLEEP_FUNC_IN_IRAM` ya activos).
* **Rollback:** `CONFIG_TEF_POWER_MGR=n` mantiene el comportamiento actual (240 MHz fijo).

---

### FASE 4: Saneamiento de Concurrencia, Memoria Interna (DRAM) y Arquitectura

> **Propósito:** Eliminar condiciones de carrera reales entre tareas, serializar accesos no thread-safe, liberar ≥ 30 KB de DRAM interna estática (`.bss` → PSRAM) y consolidar tareas de temporización redundantes sin añadir nuevas features de producto.

#### Checklist de Ejecución de Fase 4:
- [x] **4.1. Sincronización y Thread-Safety en el motor de transporte (`comm_module` y `usb_module`)**
  - [x] Proteger el estado compartido `s_tx_*` y `comm_erase_tx_buffer()` en `components/comm_module/comm_tx.c` frente al acceso concurrente entre `comm_tx_task` y `comm_proc_task`.
  - [x] Evitar `xSemaphoreGive(s_tx_done_sem)` espurio cuando `comm_erase_tx_buffer()` es invocado por el propio timeout de `comm_tx_task`.
  - [x] Serializar las invocaciones a `tud_hid_n_report` / `tud_hid_n_keyboard_report` en `components/usb_module/usbmod.c` mediante mutex ligero.
  - [x] Eliminar el VLA `uint8_t buf[len + 1]` en `usb_send_keyboard_nkro()`, validar `tud_mounted() && tud_hid_n_ready()` antes de transmitir, quitar `taskYIELD()` redundante en `usb_task()` y remover funciones de test con espera activa infinita (`block_until_kb_ready`, `usb_send_char`, `usb_send_keystroke`).
- [x] **4.2. Protección de recarga en caliente (Hot-Reload) de configuración y cachés**
  - [x] Sincronizar la recarga de NVS frente al bucle de escaneo en `components/keyboard/kb_combo.c` (`kb_combo_reload` vs `kb_combo_process_key`/`kb_combo_tick`).
  - [x] Sincronizar `kb_custom_key_reload()` frente a `find_ckey()` en `components/keyboard/kb_custom_key.c`.
  - [x] Sincronizar `on_macros_updated()` frente a `execute_macro_cancellable()` y `kb_macro_process_action()` en `components/keyboard/kb_macro.c`.
  - [x] Proteger `s_dram_base`, `s_dram_swap` y `s_swap_layer_idx` en `components/config_module/cfg_layouts.c` y corregir la desreferencia sin guarda de `s_psram_cache` en `cfg_layout_create()`.
  - [x] Proteger `s_cache` en `components/status_module/statusmod.c` con spinlock (`portMUX_TYPE`) entre `sys_evt` y `comm_proc_task`.
- [x] **4.3. Consolidación de tareas de temporización (`kb_sys_action` y `kb_tap_0..3`)**
  - [x] Eliminar la tarea dedicada `kb_sys_action` (4 KB stack) y la condición de carrera sobre `s_trackers[]` en `components/keyboard/kb_system_action.c`, integrando `kb_system_action_poll()` al bucle determinista de `kb_manager_task` con guarda `kb_system_action_is_active()` para sleep.
  - [x] Reemplazar el pool de 4 tareas `kb_tap_0..3` (12 KB stack) en `components/keyboard/kb_macro.c` por un planificador no bloqueante de plazos (`press_deadline_us`, `release_deadline_us`) despachado por timer o tick de `kb_manager`.
- [x] **4.4. Optimización de memoria interna (DRAM `.bss` y Heap) y `rgb_worker`**
  - [x] Migrar los 3 buffers estáticos de 10 KB (`s_tx_buf` en `comm_tx.c`, `s_rx_buf` en `comm_rx.c` y `s_cfg_resp_buf` en `cfgmod.c` = **30 KB de `.bss` en DRAM interna**) a asignación en PSRAM (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`) con fallback a DRAM.
  - [x] Cambiar `malloc(payload_len)` en `comm_send_payload()` (`comm_tx.c`) por `heap_caps_malloc` priorizando PSRAM.
  - [x] Convertir `rgb_worker_task` (`components/rgb_module/rgb.c`) en 100 % orientado a eventos (`portMAX_DELAY` en `xQueueReceive` en lugar de despertar cada 2 s en reposo).

#### 4.1. Sincronización y Thread-Safety en el motor de transporte (`comm_module` y `usb_module`)
* **Diagnóstico verificado en el código:**
  1. **Carrera en `comm_tx.c`:** El estado global de transmisión ([`s_tx_buf`, `s_tx_buf_len`, `s_tx_buf_idx`, `s_tx_awaiting_response`, `s_tx_blast_mode_flag`, `s_tx_current_target`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L39-L56)) es leído y escrito sin sincronización desde dos tareas distintas: `comm_tx_task` ([`comm_tx.c#L319-L371`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L319-L371)) y `comm_proc_task` (vía [`comm_process_tx_response()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L177) y el handler de timeout en [`comm_dispatch.c#L203`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_dispatch.c#L203)). Si `xSemaphoreTake(s_tx_done_sem, ...)` expira en `comm_tx_task` al mismo tiempo que llega un ACK/BITMAP en `comm_proc_task`, ambas tareas ejecutan `comm_erase_tx_buffer()` en paralelo. Además, `comm_erase_tx_buffer()` ejecuta `xSemaphoreGive(s_tx_done_sem)` incluso cuando fue llamado por el propio timeout de `comm_tx_task`.
  2. **TinyUSB sin serialización y código peligroso en `usbmod.c`:** `tud_hid_n_report()` no es thread-safe, pero se invoca desde `kb_mgr`, `kb_macro`, `kb_tap_*`, `comm_tx_task` y `comm_proc_task`. En [`usb_send_keyboard_nkro()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/usb_module/usbmod.c#L73-L79) se usa un VLA en stack (`uint8_t buf[len + 1]`) y no se comprueba `tud_mounted() && tud_hid_n_ready(ITF_NUM_HID_KBD)` antes de llamar a TinyUSB. Asimismo, [`block_until_kb_ready()`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/usb_module/usbmod.c#L90-L94) contiene un bucle infinito de espera activa (`while (!tud_hid_n_ready(...)) continue;`) que dispara el Task Watchdog si el host desconecta a mitad de llamada.
* **Acciones:**
  1. Proteger la máquina de estados `s_tx_*` con un mutex interno en `comm_tx.c` (o delegar las transiciones a un único hilo consumidor) y distinguir el reset por finalización externa (`comm_complete_tx_buffer` → da el semáforo) del reset por timeout local en `comm_tx_task`.
  2. Añadir un mutex de transporte USB en `usbmod.c` (adquisición sin espera o ≤ 1 tick en la ruta `nowait` de teclado, y acotada en COMM), reemplazar el VLA por `uint8_t buf[NKRO_BYTES + 1]`, y eliminar las funciones muertas `block_until_kb_ready`, `usb_send_char` y `usb_send_keystroke`.
* **Aceptación:** 0 errores bajo ThreadSanitizer (`--tsan`) en pruebas concurrentes de TX/RX + timeout; `idf.py build` con `-Wvla` limpio.

#### 4.2. Protección de recarga en caliente (Hot-Reload) de configuración y cachés
* **Diagnóstico verificado en el código:**
  - Cuando llega un comando `SET`/`DELETE` desde el configurador o desde `split_config_sync`, los callbacks de actualización mutan tablas en RAM mientras `kb_mgr` o `kb_macro` las recorren en otro núcleo:
    - [`kb_combo.c#L109-L127`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_combo.c#L109-L127) (`kb_combo_reload`): modifica `s_combo_count`, `s_combos` (incluyendo `qsort()`), `s_combo_rt` y `s_suppressed_count` desde la tarea de eventos mientras `kb_mgr` ejecuta `kb_combo_process_key()` y `kb_combo_tick()`.
    - [`kb_custom_key.c#L151-L163`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_custom_key.c#L151-L163) (`kb_custom_key_reload`): reescribe `s_ckeys` y `s_ckey_count` sin exclusión mutua mientras `kb_mgr` invoca `kb_custom_key_is_tap_hold()`.
    - [`kb_macro.c#L621-L626`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L621-L626) (`on_macros_updated`): recarga `s_macros` y hace `memset(s_rt_state, 0, ...)` desde `comm_proc_task` mientras `kb_macro` ejecuta `execute_macro_cancellable()`.
    - [`cfg_layouts.c#L131-L160`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfg_layouts.c#L131-L160) y [`L235`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfg_layouts.c#L235): `cfg_layout_get_action_code()` muta `s_dram_swap` y `s_swap_layer_idx` sin lock frente a `layout_update_cb()`; y `cfg_layout_create()` pasa `&s_psram_cache[id]` a `cfgmod_write_storage()` sin comprobar si `s_psram_cache` es `NULL`.
    - [`statusmod.c#L16-L30`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/status_module/statusmod.c#L16-L30): `s_cache` se actualiza tanto en `sys_evt` como en `status_module_callback()` (`comm_proc_task`) sin spinlock.
* **Acciones:**
  1. Diferir la recarga de `kb_combo` y `kb_custom_key` mediante flags atómicos (`_Atomic bool s_reload_pending`) consumidos de forma síncrona al inicio del ciclo de `kb_manager_task` (antes de escanear/procesar), eliminando bloqueos y carreras en la ruta crítica.
  2. En `kb_macro.c`, encolar un comando de control de recarga en `s_macro_queue` (o proteger el acceso a `s_macros`/`s_rt_state` con mutex) para que nunca se sobrescriba una macro a mitad de ejecución.
  3. En `cfg_layouts.c`, sincronizar el acceso al caché de capas y corregir la guarda de `s_psram_cache` en `cfg_layout_create()`.
  4. En `statusmod.c`, proteger las lecturas y escrituras de `s_cache` con un `portMUX_TYPE` al tomar el snapshot de `send_status_push()`.
* **Aceptación:** test multihilo (`pthread` + `--tsan`) simulando ráfagas de actualización de NVS mientras `kb_manager` / `kb_combo` / `kb_custom_key` procesan pulsaciones a 1000 Hz sin data races.

#### 4.3. Consolidación de tareas de temporización (`kb_sys_action` y `kb_tap_0..3`)
* **Diagnóstico verificado en el código:**
  - [`kb_system_action.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_system_action.c) crea una tarea `kb_sys_action` (4 KB de stack) que sondea cada 10 ms (`vTaskDelay(pdMS_TO_TICKS(10))`). Además de introducir hasta 10 ms de jitter en la detección de *hold* y *single tap*, `process_action()` (llamada desde `kb_mgr`) y `kb_system_action_poll()` (llamada desde `kb_sys_action`) modifican `s_trackers[]` y `s_active_tracker_count` **sin ninguna sincronización**.
  - [`kb_macro.c#L605-L615`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c#L605-L615) mantiene 4 tareas FreeRTOS permanentes (`kb_tap_0..3`, 12 KB de stack en total) únicamente para dormir con `vTaskDelay` entre press y release de un tap asíncrono.
* **Acciones:**
  1. Integrar `kb_system_action_poll()` directamente en el bucle principal de `kb_manager_task` (junto a `kb_combo_tick` y `kb_tap_hold_tick`), exponer `kb_system_action_is_active()` para mantener despierto el bucle mientras haya trackers activos (`s_active_tracker_count > 0`) y eliminar la tarea `kb_sys_action`. Resultado: 0 carreras de datos, resolución de 1 ms (en vez de 10 ms) y 1 tarea menos.
  2. Sustituir las 4 tareas `kb_tap_0..3` por una tabla de slots de taps activos con timestamps de activación/liberación (`press_at_us`, `release_at_us`) evaluada por un único timer/worker o en el propio tick del subsistema de teclado.
* **Aceptación:** reducción de 5 tareas FreeRTOS en el inventario del sistema; todos los tests de `test_kb_system_action` y `test_kb_report` verdes.

#### 4.4. Optimización de memoria interna (DRAM `.bss` y Heap) y `rgb_worker`
* **Diagnóstico verificado en el código:**
  - Tres buffers estáticos de 10 240 bytes residen en la `.bss` de DRAM interna:
    1. `s_tx_buf[MAX_TX_BUF_SIZE]` en [`comm_tx.c#L39`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L39) (10 KB).
    2. `s_rx_buf[MAX_RX_BUF_SIZE]` en [`comm_rx.c#L33`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c#L33) (10 KB).
    3. `s_cfg_resp_buf[CFG_USB_RESP_BUF_SIZE]` en [`cfgmod.c#L369`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfgmod.c#L369) (10 KB).
  - En [`comm_tx.c#L388`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c#L388), `comm_send_payload()` reserva copias dinámicas con `malloc()` en DRAM interna.
  - En [`rgb.c#L104`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/rgb_module/rgb.c#L104), `rgb_worker_task` tiene un timeout de 2000 ms en `xQueueReceive` que fuerza un re-cálculo y refresco RMT cada 2 segundos incluso cuando el teclado está completamente inactivo, pese a que `split_task` ya publica eventos explícitos `SPLIT_EVENT_STALE` y `SPLIT_EVENT_STALE_RECOVERED`.
* **Acciones:**
  1. Convertir `s_tx_buf`, `s_rx_buf` y `s_cfg_resp_buf` en punteros inicializados en `comm_tx_init()`, `comm_rx_init()` y `cfg_init()` mediante `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (con fallback a `MALLOC_CAP_DEFAULT`).
  2. Usar `heap_caps_malloc` con preferencia PSRAM en `comm_send_payload()`.
  3. Cambiar `rgb_worker_task` a espera bloqueante `portMAX_DELAY` en `xQueueReceive`, eliminando el despertar periódico de 2 s.
* **Aceptación:** `idf.py size-components` reporta una reducción de **≥ 30 KB en `.bss` de DRAM interna** (`diram_bss`).

---

### FASE 5: Cobertura Integral de Tests, Higiene de Código y Endurecimiento de Calidad

> **Propósito:** Cerrar las brechas de pruebas unitarias en los motores más complejos del firmware, eliminar números mágicos y código muerto, modularizar componentes monolíticos y blindar el repositorio con análisis estático y presupuestos en CI.

#### Checklist de Ejecución de Fase 5:
- [x] **5.1. Tests unitarios para motores de teclado no cubiertos (`kb_combo`, `kb_custom_key`, `kb_macro`)**
  - [x] Crear `test/keyboard/test_kb_combo.c` validando combos simples, *subsumption* (subconjuntos/superconjuntos), `strict_order`, `delayed_press`, `cancel_keys`, `release_on_first_key` y expiración por `kb_combo_tick()`.
  - [x] Crear `test/keyboard/test_kb_custom_key.c` validando `CKEY_MODE_PRESS_RELEASE` (`press_sustain`, `wait_for_finish`) y `CKEY_MODE_MULTI_ACTION` (`hold_sustain`, `SINGLE_TAP`, `DOUBLE_TAP`, `HOLD`).
  - [x] Crear `test/keyboard/test_kb_macro.c` validando los 8 modos de ejecución (`ONCE_STACK_ONCE`, `ONCE_NO_STACK`, `ONCE_STACK_N`, `HOLD_REPEAT`, `HOLD_REPEAT_CANCEL`, `TOGGLE_REPEAT`, `TOGGLE_REPEAT_CANCEL`, `BURST_N`), recursión acotada (`depth <= 5`) y limpieza al cancelar (`kb_macro_force_clear`).
- [x] **5.2. Tests unitarios para `Blast+Reconcile` (`comm_module`), `split_role`/`split_sync` y `cfgmod`**
  - [x] Crear `test/comm/test_comm_blast.c` validando transacciones multipaquete `Blast+Reconcile`, reconstrucción de bitmap con pérdida simulada de paquetes `MID`, límite de rondas de reconciliación (`TX_BLAST_MAX_RECONCILE_ROUNDS`), reintentos por `NAK` y lock de sesión entre transportes.
  - [x] Crear `test/split/test_split_role_and_sync.c` validando todas las reglas de desempate de `split_role_on_negotiate()`, el intercambio `ROLE_SWAP_REQ`/`ACK` y la compresión/descompresión delta de matriz en `split_sync.c`.
  - [x] Crear `test/config/test_cfgmod.c` validando `cfgmod_handle_usb_comm()` (`GET`, `SET`, borrado con payload vacío, actualización de máscaras `mac_idx`/`cmb_idx`/`ck_idx`, validación de tamaños y `CFG_KEY_*_LIMITS`).
- [x] **5.3. Higiene arquitectónica, constantes de dominio y desacoplamiento (`main`, `cfgmod`, `ble_module`)**
  - [x] Limpiar `main/main.c` eliminando variables e includes muertos (`enum ColorSet`, headers de TinyUSB) y desacoplar los handlers de prueba del botón físico.
  - [x] Reemplazar literales mágicos (`64`, `32`, `120`, `15`, `16`, `256`, `8`) en `components/config_module/cfgmod.c` por constantes de dominio con `_Static_assert` de consistencia, y eliminar declaraciones `extern` duplicadas.
  - [x] Modularizar `components/ble_module/blemod.c` (1428 líneas) extrayendo la máquina de estados de reconexión (`RECONN_PHASE_*`) y advertising a una unidad cohesiva y testeable en host.
- [x] **5.4. Endurecimiento de compilación, sanitizers y guardia de memoria en CI**
  - [x] Habilitar flags estrictos de compilación (`-Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized`) en los `CMakeLists.txt` de los componentes propios.
  - [x] Integrar ejecución de tests de concurrencia con `--tsan` (ThreadSanitizer) además de `--asan` en `test/run_tests.sh` y `.github/workflows/ci.yml`.
  - [x] Configurar guardia automática de tamaño de DRAM interna en `.github/workflows/ci.yml` verificando el reporte de `idf.py size-components`.

#### 5.1. Suite de tests unitarios para motores de teclado no cubiertos (`kb_combo`, `kb_custom_key`, `kb_macro`)
* **Diagnóstico verificado:** Aunque el subsistema `keyboard` cuenta con tests para `debounce`, `layout`, `matrix`, `report`, `state`, `system_action` y `tap_hold`, tres motores con alta densidad de estados carecen por completo de pruebas unitarias:
  1. [`kb_combo.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_combo.c) (344 líneas): manejo de combos solapados donde un combo de 3 teclas subsume y cancela a uno de 2 teclas (`is_subset`), supresión temporal de teclas en `s_suppressed[]` para `delayed_press`, liberación retroactiva en `cancel_keys`, orden estricto (`strict_order`) y vaciado por timeout en `kb_combo_tick()`.
  2. [`kb_custom_key.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_custom_key.c) (248 líneas): lógica de `wait_for_finish` basada en ticks restantes (`s_press_end_tick`), `press_sustain` y `hold_sustain`.
  3. [`kb_macro.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/keyboard/kb_macro.c) (673 líneas): máquina de estados de los 8 modos de ejecución, colas de apilamiento (`pending_count`), ráfagas (`burst_remaining`), macros anidadas y cancelación sin dejar teclas virtuales pegadas en `s_v_nkro`.
* **Aceptación:** 100 % de los modos y ramas críticas de `kb_combo`, `kb_custom_key` y `kb_macro` cubiertos en `test/keyboard/` y ejecutándose en `< 2 s` bajo `--asan`.

#### 5.2. Suite de tests para `Blast+Reconcile` (`comm_module`), `split_role`/`split_sync` y `cfgmod`
* **Diagnóstico verificado:**
  - En `comm_module`, el motor [`comm_tx.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_tx.c) / [`comm_rx.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/comm_module/comm_rx.c) implementa el protocolo multipaquete *Blast+Reconcile*, pero `test_usb_rx.c` solo ejercita transferencias simples.
  - En `split`, [`split_role.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/split/split_role.c) y [`split_sync.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/split/split_sync.c) carecen de tests que verifiquen la jerarquía de decisión Master/Slave y la compresión delta de matriz.
  - En `config_module`, [`cfgmod.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfgmod.c) no tiene tests sobre su despachador binario `cfgmod_handle_usb_comm()`.
* **Aceptación:** Nuevas suites `test_comm_blast.c`, `test_split_role_and_sync.c` y `test_cfgmod.c` integradas en `test/main.c` con 0 fallos.

#### 5.3. Higiene arquitectónica, constantes de dominio y desacoplamiento (`main`, `cfgmod`, `ble_module`)
* **Diagnóstico verificado:**
  1. [`main/main.c#L34-L64`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/main/main.c#L34-L64): incluye cabeceras de bajo nivel de TinyUSB que pertenecen a `usb_module`, define un `enum ColorSet current_color` sin usar y mantiene callbacks `single_press_test`/`double_press_test`/`hold_press_test` con nombres de prototipo.
  2. [`cfgmod.c#L33-L34`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfgmod.c#L33-L34) y [`L127-L243`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/config_module/cfgmod.c#L127-L243): declara dos veces `extern void cfg_ble_init(void);` y repite literales mágicos (`64`, `32`, `120`, `15`, `16`, `256`, `8`) que pueden desincronizarse si cambian las cotas en las cabeceras.
  3. [`blemod.c`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/components/ble_module/blemod.c): concentra 1428 líneas mezclando GAP, GATT, bonding y la máquina de estados de reconexión (`RECONN_PHASE_SELECTED`, `RECONN_PHASE_FOREVER`, `RECONN_PHASE_FINITE`).
* **Acciones:**
  1. Limpiar `main/main.c` y formalizar los handlers del botón de sistema.
  2. Sustituir todos los literales mágicos en `cfgmod.c` por `CFG_MACROS_MAX_COUNT`, `CFG_COMBOS_MAX_COUNT`, `CFG_CKEYS_MAX_COUNT`, `CFG_LAYOUT_MAX_COUNT`, etc., añadiendo `_Static_assert` sobre los tamaños de las máscaras de bits.
  3. Extraer la lógica de fases de reconexión de `blemod.c` a un submódulo interno con interfaz limpia y test unitario en host.

#### 5.4. Endurecimiento de compilación, sanitizers y guardia de memoria en CI
* **Acciones:**
  1. Añadir `-Wall -Wextra -Wvla -Werror=implicit-fallthrough -Werror=uninitialized` en los componentes del firmware para prevenir regresiones de calidad.
  2. Añadir un modo `bash run_tests.sh --tsan` ejecutado en `.github/workflows/ci.yml` sobre las pruebas de concurrencia.
  3. Registrar en `implementations/baseline/BASELINE_METRICS.md` el nuevo consumo de DRAM/PSRAM tras la Fase 4 y fijar el umbral en CI.

---

## 4. Cronograma y Dependencias

```mermaid
flowchart LR
    subgraph Completado ["Fases 0 a 3 (Completadas)"]
        F0["Fase 0: Fundaciones y CI"] --> F1["Fase 1: Core y Reportes"]
        F1 --> F2["Fase 2: Latencia y Tap-Hold"]
        F2 --> F3["Fase 3: Gestión de Energía"]
    end

    subgraph Calidad ["Fase 4: Concurrencia y DRAM"]
        F4_1["4.1 Thread-safety COMM y USB"] --> F4_2["4.2 Hot-Reload seguro"]
        F4_2 --> F4_3["4.3 Consolidar tareas (sys_action / tap)"]
        F4_3 --> F4_4["4.4 Liberar 30 KB DRAM (.bss a PSRAM)"]
    end

    subgraph Robustez ["Fase 5: Tests e Higiene"]
        F5_1["5.1 Tests kb_combo / ckey / macro"] --> F5_2["5.2 Tests Blast+Reconcile / split / cfgmod"]
        F5_2 --> F5_3["5.3 Higiene main / cfgmod / blemod"]
        F5_3 --> F5_4["5.4 Flags estrictos + TSan en CI"]
    end

    F3 --> F4_1
    F4_4 --> F5_1
```

* Las subfases **5.1 y 5.2** (ampliación de cobertura de tests unitarios) validan directamente los invariantes de las estructuras saneadas en **4.1–4.3**.

---

## 5. Riesgos y Mitigaciones

| Riesgo | Prob. | Impacto | Mitigación |
|---|---|---|---|
| Light sleep incompatible con BLE+ESP-NOW de baja latencia | Alta | Alto | Spike 3.0 como gate; fallback a DFS/tickless (`CONFIG_TEF_POWER_MGR`) |
| Settling reducido causa *ghosting*/*misses* | Media | Alto | Kconfig + banco de estrés + rollback inmediato |
| Eager debounce expone EMI | Media | Medio | `press_confirm_scans` configurable |
| Deadlock al introducir mutexes en `comm_tx` o `usbmod` | Baja | Alto | Orden estricto de adquisición de locks; no llamar callbacks externos con el lock tomado; validación con `--tsan` |
| Accesos a PSRAM con caché SPI deshabilitada (durante escritura NVS) | Baja | Alto | Solo mover buffers de aplicación (`s_tx_buf`, `s_rx_buf`, `s_cfg_resp_buf`) a PSRAM; mantener stacks de tareas que escriben en NVS (`split`, `comm_proc_task`) en DRAM interna |
| Regresión en tiempos de respuesta de `kb_system_action` al eliminar su tarea | Baja | Medio | Ejecutar `kb_system_action_poll()` en cada ciclo activo de `kb_manager` (1 ms vs 10 ms previos) e incluir `kb_system_action_is_active()` en la condición de espera |

---

## 6. Definition of Done (por fase)

1. `bash test/run_tests.sh --asan` y `bash test/run_tests.sh --tsan` → 0 fallos; nuevos tests unitarios para cada módulo intervenido.
2. `idf.py build` sin warnings nuevos (con `-Wall -Wextra -Wvla`); `size-components` confirmando la reducción de DRAM interna.
3. Configurador: `npm run lint && npm run build` verdes.
4. Métrica asociada **medida** y actualizada en [`implementations/baseline/BASELINE_METRICS.md`](file:///home/srleg/Projects/Tecleados-ESP-Firmware/implementations/baseline/BASELINE_METRICS.md).
5. Documentación actualizada: `*_MODULE.md` del componente correspondiente en `components/` (y `universe/modules/` solo si cambia la relación arquitectónica entre módulos).
6. Commits atómicos y reversibles en `firmware-evolution`.

---

## 7. Matriz de Validación y Criterios de Aceptación

| Métrica | Línea base (Fase 0.3) | Objetivo | Método |
|---|---|---|---|
| **Latencia interna de pulsación** (matriz → pin debug) | ~4–5 ms (debounce 5 scans) | **p99 < 1.2 ms** | Analizador lógico: interruptor ↔ pin debug |
| **CPU del core de scan con teclas activas** | ~36 % (20 µs × 18) | **< 15 %** | `vTaskGetRunTimeStats` |
| **Bloqueo del scan loop por transporte caído** | hasta 100 ms | **< 1 período de scan** (p99 < 2×) | Histograma en `kb_macro_send_report` |
| **Teclas pegadas tras fallo de reporte** | posible hasta keepalive | **0** (reintento por `s_report_pending`) | Test host + HW con USB desconectado |
| **Sync custom keys split** | 64 / 120 | **120 / 120** + `push_all` < 10 s | Test unitario + Checksum NVS en esclavo |
| **Memoria DRAM interna libre (`.bss` liberada)** | `s_tx_buf` + `s_rx_buf` + `s_cfg_resp_buf` = 30 KB en DRAM | **≥ 30 KB liberados** de `.bss` interna hacia PSRAM | `idf.py size-components` |
| **Tareas FreeRTOS de temporización redundantes** | `comm_timeout_task` + `kb_sys_action` + 4×`kb_tap` (6 tareas) | **Eliminadas / consolidadas** en timers o bucle de `kb_mgr` | Inventario en `BASELINE_METRICS.md` |
| **Condiciones de carrera (Data Races)** | Carreras en `comm_tx`, `kb_sys_action`, hot-reload NVS y `statusmod` | **0 data races** reportados | Suite multihilo con `ThreadSanitizer (--tsan)` |
| **Cobertura de tests unitarios en host** | Sin tests para `kb_combo`, `kb_custom_key`, `kb_macro`, `Blast+Reconcile`, `split_role`, `cfgmod` | **100 % de los motores core cubiertos y pasando** en CI | `run_tests.sh --asan` + GitHub Actions |

---

## 8. Siguientes Pasos Inmediatos

1. **Plan de evolución completado:** Todas las Fases (0 a 5) se encuentran completamente implementadas, saneadas y testeadas en host con ASan y TSan (262 assertions/suites verdes) y CI endurecido.
2. **Validación en hardware físico:** Flasheo y verificación con instrumentación en target ESP32-S3 (perfil de consumo PPK2, latencia con analizador lógico y pruebas de enlace BLE multi-host en vivo).
3. **Mantenimiento continuo:** Preservar guardias de memoria (`diram_bss <= 4096`) y flags estrictos (`-Wall -Wextra -Wvla -Werror=...`) en todos los nuevos PRs hacia `main`.
