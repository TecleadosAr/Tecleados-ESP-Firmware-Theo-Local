# Presupuesto de Pines y Decisión de Arquitectura de Hardware (Phase 0.4)

> **Documento:** `implementations/PIN_BUDGET_HARDWARE_DECISION.md`  
> **Fecha:** Octubre 2026  
> **Target:** ESP32-S3 (Xtensa Dual-Core, ESP32-S3-WROOM-1 / ESP32-S3-MINI)  
> **Estado:** Aprobado — Bloqueante para Fases 3 y 4

---

## 1. Resumen Ejecutivo

El ESP32-S3 dispone de 49 GPIOs físicos (GPIO0 a GPIO48). En la configuración actual del firmware TEF:
- **28 pines están en uso activo** (24 para la matriz de 6×18, 2 para USB D±, 1 para botón Boot y 1 para LED RGB).
- **12 pines están reservados por hardware interno** (GPIO26–37 por Flash SPI y memoria **Octal PSRAM**, verificado en `CONFIG_SPIRAM_MODE_OCT=y`).
- **5 pines permanecen libres o reutilizables** en el chip: **GPIO43, GPIO44, GPIO45, GPIO46, GPIO47**.

Este documento establece la asignación de pines, la resolución de conflictos críticos (como la ocupación total de ADC1 por la matriz) y las decisiones de hardware para las Fases 3 (Batería y Energía) y 4 (Hardware Extendido: Encoders, Dongle, Display, Trackball).

---

## 2. Mapa Completo de Pines del ESP32-S3

| GPIO | Función TEF Actual | Restricciones / Características de Hardware | Estado / Asignación Futura |
|---|---|---|---|
| **GPIO0** | Botón BOOT (`BOOT_BUTTON`) | Strapping (pull-up interno, 0 = download mode) | **Asignado:** Botón físico / Pairing switch |
| **GPIO1** | Matriz Fila 0 (`k_rows[0]`) | ADC1_CH0, RTC_GPIO1, EXT1 Wakeup capable | **Asignado:** Matriz Fila 0 / Deep Sleep Wake |
| **GPIO2** | Matriz Fila 1 (`k_rows[1]`) | ADC1_CH1, RTC_GPIO2, EXT1 Wakeup capable | **Asignado:** Matriz Fila 1 / Deep Sleep Wake |
| **GPIO3** | Matriz Fila 2 (`k_rows[2]`) | ADC1_CH2, RTC_GPIO3, EXT1 Wakeup capable | **Asignado:** Matriz Fila 2 / Deep Sleep Wake |
| **GPIO4** | Matriz Fila 3 (`k_rows[3]`) | ADC1_CH3, RTC_GPIO4, EXT1 Wakeup capable | **Asignado:** Matriz Fila 3 / Deep Sleep Wake |
| **GPIO5** | Matriz Fila 4 (`k_rows[4]`) | ADC1_CH4, RTC_GPIO5, EXT1 Wakeup capable | **Asignado:** Matriz Fila 4 / Deep Sleep Wake |
| **GPIO6** | Matriz Fila 5 (`k_rows[5]`) | ADC1_CH5, RTC_GPIO6, EXT1 Wakeup capable | **Asignado:** Matriz Fila 5 / Deep Sleep Wake |
| **GPIO7** | Matriz Columna 0 (`k_cols[0]`) | ADC1_CH6, RTC_GPIO7 | **Asignado:** Matriz Col 0 (Unificado) / ADC Batería en Split |
| **GPIO8** | Matriz Columna 1 (`k_cols[1]`) | ADC1_CH7, RTC_GPIO8 | **Asignado:** Matriz Col 1 |
| **GPIO9** | Matriz Columna 2 (`k_cols[2]`) | ADC1_CH8, RTC_GPIO9 | **Asignado:** Matriz Col 2 |
| **GPIO10**| Matriz Columna 3 (`k_cols[3]`) | ADC1_CH9, RTC_GPIO10 | **Asignado:** Matriz Col 3 |
| **GPIO11**| Matriz Columna 4 (`k_cols[4]`) | ADC2_CH0, RTC_GPIO11 | **Asignado:** Matriz Col 4 |
| **GPIO12**| Matriz Columna 5 (`k_cols[5]`) | ADC2_CH1, RTC_GPIO12 | **Asignado:** Matriz Col 5 |
| **GPIO13**| Matriz Columna 6 (`k_cols[6]`) | ADC2_CH2, RTC_GPIO13 | **Asignado:** Matriz Col 6 |
| **GPIO14**| Matriz Columna 7 (`k_cols[7]`) | ADC2_CH3, RTC_GPIO14 | **Asignado:** Matriz Col 7 |
| **GPIO15**| Matriz Columna 8 (`k_cols[8]`) | ADC2_CH4, RTC_GPIO15 | **Asignado:** Matriz Col 8 |
| **GPIO16**| Matriz Columna 9 (`k_cols[9]`) | ADC2_CH5, RTC_GPIO16 | **Asignado:** Matriz Col 9 |
| **GPIO17**| Matriz Columna 10 (`k_cols[10]`) | ADC2_CH6, RTC_GPIO17 | **Asignado:** Matriz Col 10 |
| **GPIO18**| Matriz Columna 11 (`k_cols[11]`) | ADC2_CH7, RTC_GPIO18 | **Asignado:** Matriz Col 11 |
| **GPIO19**| USB D- (Native OTG) | TinyUSB Hardware PHY | **Asignado:** USB D- |
| **GPIO20**| USB D+ (Native OTG) | TinyUSB Hardware PHY | **Asignado:** USB D+ |
| **GPIO21**| Matriz Columna 12 (`k_cols[12]`) | RTC_GPIO21 | **Asignado:** Matriz Col 12 |
| **GPIO26–32** | Bus SPI Flash / PSRAM | Conectados internamente al SiP / Flash | **BLOQUEADOS:** Sistema |
| **GPIO33–37** | Octal PSRAM (`CONFIG_SPIRAM_MODE_OCT=y`) | Líneas DQS y datos Octal PSRAM | **BLOQUEADOS:** Sistema (Octal PSRAM) |
| **GPIO38**| Matriz Columna 13 (`k_cols[13]`) | Banco GPIO Alto | **Asignado:** Matriz Col 13 |
| **GPIO39**| Matriz Columna 14 (`k_cols[14]`) | Banco GPIO Alto | **Asignado:** Matriz Col 14 |
| **GPIO40**| Matriz Columna 15 (`k_cols[15]`) | Banco GPIO Alto | **Asignado:** Matriz Col 15 |
| **GPIO41**| Matriz Columna 16 (`k_cols[16]`) | Banco GPIO Alto | **Asignado:** Matriz Col 16 |
| **GPIO42**| Matriz Columna 17 (`k_cols[17]`) | Banco GPIO Alto | **Asignado:** Matriz Col 17 |
| **GPIO43**| UART0 TX / Debug Console | Libre si consola va por USB CDC | **Propuesto:** I2C SDA (Display / Fuel Gauge) |
| **GPIO44**| UART0 RX / Debug Console | Libre si consola va por USB CDC | **Propuesto:** I2C SCL (Display / Fuel Gauge) |
| **GPIO45**| Strapping (VDD_SPI voltage) | **Crítico:** Debe estar en LOW en boot (3.3V flash) | **Propuesto:** Pin de depuración de latencia (salida) o CS Display |
| **GPIO46**| Strapping (ROM Boot log) | Input-only en reset, pull-down interno | **Propuesto:** Entrada auxiliar / Interrupt / MOT |
| **GPIO47**| Libre | GPIO de propósito general sin restricciones | **Propuesto:** Encoder A / Pin de interrupción |
| **GPIO48**| WS2812 LED Data | RMT Tx | **Asignado:** LED RGB / Indicador de estado |

---

## 3. Resoluciones Arquitectónicas Críticas

### 3.1. Medición de Batería (Fase 3.1)
- **Problema:** En el ESP32-S3, **ADC2 (GPIO11–20) no puede utilizarse simultáneamente con Wi-Fi/ESP-NOW** debido al arbitraje de radio en hardware. **ADC1 (GPIO1–10) está 100% ocupado por la matriz** en el layout unificado de 6×18.
- **Resolución — Estrategia Híbrida según Hardware:**
  1. **En Teclados Split (Mitad Izquierda / Derecha):** Cada mitad solo tiene entre 6 y 8 columnas físicas. Por lo tanto, las columnas lógicas 0..3 (GPIO7–10) o superiores no existen físicamente en la PCB de la mitad, liberando **GPIO7 (ADC1_CH6)** como entrada analógica de batería con calibración por curva (`adc_cali_create_scheme_curve_fitting`).
  2. **En Teclados Unificados de 18 Columnas:** Se adopta un **Fuel-Gauge I2C (MAX17048)** conectado en el bus I2C (GPIO43/44).
     - Ventaja: Consume 0 pines analógicos, comparte bus con el display OLED, y mide porcentaje real con consumo en reposo de solo 3 µA (evitando los 21 µA de pérdida del divisor resistivo de 100 kΩ).
  3. **Configuración de Firmware:** Se implementa soporte Kconfig:
     - `CONFIG_TEF_BATTERY_ADC_GPIO` (default: 7 en split, -1 para deshabilitar).
     - Si está deshabilitado o no detectado, el firmware lee el fuel gauge I2C si está presente, o reporta `0xFF` (desconocido).

### 3.2. Pantalla de Estado (Fase 4.3)
- **Resolución:** Bus I2C de 2 hilos:
  - **SDA:** GPIO 43
  - **SCL:** GPIO 44
- **Periféricos soportados en este bus:** Display OLED SSD1306 (0x3C) y opcionalmente Fuel Gauge MAX17048 (0x36).
- **Consola de depuración:** La consola de depuración se redirecciona a USB Serial/JTAG (nativo de ROM) o se desactiva en producción, liberando UART0 (GPIO43/44) para I2C.

### 3.3. Encoders Rotatorios (Fase 4.1)
- **Resolución A (Pines dedicados):** 1 encoder rotatorio requiere 2 pines:
  - Pin A: GPIO 47 (con decodificación por hardware PCNT).
  - Pin B: GPIO 46 (o GPIO 45 asegurando pull-down de strapping en boot).
- **Resolución B (Encoders en Matriz — Cero Pines Extra):** Las líneas A y B de cada encoder se conectan a dos posiciones vacías de la matriz de teclas (ej. intersecciones de Fila 5, Columnas 16 y 17) con diodos 1N4148 estándar. El motor de matriz a 1000 Hz detecta los detents como pulsaciones rápidas sin requerir ningún GPIO adicional del ESP32-S3.

### 3.4. Sensor Señalador / Trackball PMW3360 (Fase 4.4)
- **Resolución:** El sensor óptico PMW3360 requiere bus SPI (MOSI, MISO, SCK, CS = 4 pines) más la línea de interrupción de movimiento `MOT` (1 pin), sumando un total de 5 pines.
- Dado que en la configuración unificada de 18 columnas solo restan 5 pines, el Trackball **no puede coexistir simultáneamente con el display I2C y encoders físicos directos** a menos que:
  - Se utilice un expansor de matriz (shift register 74HC165 / 74HC595 para las columnas), liberando 15 GPIOs, o
  - El trackball se monte exclusivamente en la variante split esclava donde existen 10 pines libres de columnas no cableadas.

### 3.5. Despertar de Deep Sleep (Fase 3.2)
- **Resolución:** Filas de la matriz **GPIO 1 a 6** son pines RTC (`RTC_GPIO1..6`).
- No se requiere ningún pin adicional: el modo deep sleep configura pull-ups internos RTC en las filas, mantiene las columnas en nivel bajo mediante `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`, y activa `esp_sleep_enable_ext1_wakeup_io((1ULL<<1)|...|(1ULL<<6), ESP_EXT1_WAKEUP_ANY_LOW)`.
- Cualquier tecla presionada despierta instantáneamente el SoC.

---

## 4. Matriz de Asignación Final de Pines

| Función de Hardware | Pines Asignados | Bus / Modo |
|---|---|---|
| **Matriz Filas (0–5)** | GPIO 1, 2, 3, 4, 5, 6 | RTC GPIOs, Entrada con Pull-up |
| **Matriz Columnas (0–11)** | GPIO 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 | Salidas de drenaje abierto / Push-Pull |
| **Matriz Columnas (12–17)** | GPIO 21, 38, 39, 40, 41, 42 | Salidas Push-Pull |
| **USB D- / D+** | GPIO 19, 20 | USB OTG PHY |
| **LED RGB WS2812** | GPIO 48 | RMT Canal 0 |
| **Botón de Usuario / Boot** | GPIO 0 | Entrada con Pull-up |
| **Bus I2C (Display + Fuel Gauge)** | GPIO 43 (SDA), GPIO 44 (SCL) | I2C Master (400 kHz) |
| **Pin Depuración de Latencia** | GPIO 45 | Salida digital rápida |
| **Encoder / Interrupción Auxiliar** | GPIO 47 (Canal A), GPIO 46 (Canal B) | PCNT / GPIO Interrupt |
| **ADC Batería (Variante Split)** | GPIO 7 (desconectando Col 0 en PCB split) | ADC1_CH6 con curve fitting |
