# 🌡️ HTCC-AB02A — Nodo Sensor de Temperatura LoRaWAN Low-Power

**Firmware v1.2 · Heltec CubeCell HTCC-AB02A · ASR6502 + SX1262 · DS18B20**

> Nodo sensor autónomo de **temperatura** por **LoRaWAN** (AU915 sub-banda 2, OTAA, Clase A)
> con **deep sleep real** y **power gating** del sensor, pensado para despliegue masivo de
> bajo consumo a batería.

[![Plataforma](https://img.shields.io/badge/Plataforma-Heltec%20CubeCell%20HTCC--AB02A-blue)](https://heltec.org)
[![MCU](https://img.shields.io/badge/MCU-ASR6502%20(ARM%20Cortex--M0%2B)-green)](#)
[![Radio](https://img.shields.io/badge/Radio-SX1262-orange)](#)
[![LoRaWAN](https://img.shields.io/badge/LoRaWAN-AU915%20SB2-9cf)](#)
[![Clase](https://img.shields.io/badge/Clase-A-brightgreen)](#)
[![Activation](https://img.shields.io/badge/Activaci%C3%B3n-OTAA-ff69b4)](#)
[![Versión](https://img.shields.io/badge/Firmware-v1.2-8a2be2)](#)
[![RadioLib](https://img.shields.io/badge/RadioLib-7.7.1-lightgrey)](#)

---

## 📑 Tabla de contenidos

1. [Descripción del proyecto](#-descripción-del-proyecto)
2. [Características principales](#-características-principales)
3. [El dispositivo: hardware](#-el-dispositivo-hardware)
4. [Principio físico de funcionamiento](#-principio-físico-de-funcionamiento)
   - [Sensor DS18B20 y protocolo OneWire](#sensor-ds18b20-y-protocolo-onewire)
   - [Power gating del sensor](#power-gating-del-sensor)
   - [Deep sleep del MCU](#deep-sleep-del-mcu)
5. [Pinout y conexiones](#-pinout-y-conexiones)
6. [Diagrama de conexión](#-diagrama-de-conexión)
7. [Forma de operar](#-forma-de-operar)
8. [Formato del payload](#-formato-del-payload)
9. [Comandos de downlink](#-comandos-de-downlink)
10. [Configuración del decoder (ChirpStack v4)](#-configuración-del-decoder-chirpstack-v4)
11. [Ejemplos de uso](#-ejemplos-de-uso)
12. [Calibración](#-calibración)
13. [Compilación y flasheo](#-compilación-y-flasheo)
14. [Advertencias y precauciones](#-advertencias-y-precauciones)
15. [Estructura del proyecto](#-estructura-del-proyecto)

---

## 📖 Descripción del proyecto

El proyecto implementa un **nodo sensor LoRaWAN de temperatura** basado en la placa
**Heltec CubeCell HTCC-AB02A**, que integra en un único encapsulado SiP el MCU **ASR6502**
(núcleo ARM Cortex-M0+) y el transceptor de radio **SX1262**.

El nodo:

1. **Lee** un sensor digital de temperatura **DS18B20** (cápsula sumergible de 1 m,
   solo temperatura) mediante el protocolo **OneWire**, implementado por **bit-banging
   manual** (sin librerías externas de bus).
2. **Alimenta** el sensor mediante **power gating**: el pin VDD del DS18B20 se enciende
   únicamente durante la medición y se apaga junto con el MCU durante el sueño, para que
   el consumo del sensor en reposo sea prácticamente nulo.
3. **Reporta** por LoRaWAN (puerto 1, no confirmado) un payload de **4 bytes** con la
   temperatura en décimas de °C y el voltaje de batería en mV.
4. **Duerme** 10 minutos en **deep sleep real** (RTC + `lowPowerHandler()`) entre ciclos.
5. **Re-une** automáticamente la sesión OTAA si ésta se pierde.
6. **Opcionalmente**, conmuta una salida digital (`GPIO1/P6_0`) comandada por downlink.

El firmware está basado en el proyecto `HTCC_AB02A_LoRaWAN` (v1.1) y usa la librería
**RadioLib 7.7.1** para la pila LoRaWAN, en lugar del stack nativo `LoRaWan_APP.h`.

---

## ✨ Características principales

| Característica | Detalle |
|---|---|
| **Placa** | Heltec CubeCell HTCC-AB02A |
| **SoC** | ASR6502 (ARM Cortex-M0+, 48 MHz, 128 KB Flash, 16 KB RAM) |
| **Radio** | SX1262 (LoRa, integrada en el SiP) |
| **Región LoRaWAN** | AU915, **sub-banda 2** |
| **Activación** | **OTAA** (Over-The-Air Activation) |
| **Clase** | Clase A (uplink no confirmado, 2 ventanas RX) |
| **Sensor** | DS18B20, resolución fija **12 bits** (0.0625 °C/LSB) |
| **Protocolo sensor** | OneWire por **bit-banging manual** (sin librería externa) |
| **Integridad de datos** | CRC-8 Dallas + validación de sanidad del scratchpad |
| **Alimentación del sensor** | Power gating por GPIO (`GPIO3/P6_2`) |
| **Ciclo de medición** | **10 minutos** (`SLEEP_INTERVAL_MS = 600000`) |
| **Deep sleep** | Nativo (`TimerEvent_t` RTC + `lowPowerHandler()` → `CySysPmDeepSleep`) |
| **Uplink** | Puerto 1, 4 bytes big-endian, no confirmado |
| **Downlink** | Puerto 1, comando de salida `GPIO1` (ON/OFF/TOGGLE) |
| **Credenciales** | `#define` editables por tarjeta (producción masiva) |
| **Decoder** | `decoder.js` — ChirpStack v4 (`Decode`/`Encode`) |
| **Consumo en sueño** | Radio ~0.6 µA + sensor ~0 mA (power gating) |

---

## 🖥️ El dispositivo: hardware

### Heltec CubeCell HTCC-AB02A

La **HTCC-AB02A** es una placa de desarrollo ultra-compacta de Heltec que combina:

- **ASR6502**: System-in-Package que integra un MCU ARM Cortex-M0+ (basado en STM32L0)
  y el transceptor LoRa **SX1262**, más un cristal y componentes de RF. Ofrece **128 KB
  de Flash** y **16 KB de RAM**.
- **Conectividad LoRa/LoRaWAN** lista para usar (antena, adaptación de impedancia y
  control de consumo del radio).
- **Gestión de batería**: ADC interno para leer el voltaje de la batería (pin `P2_0`),
  además de circuito de carga LiPo (según variante).
- **GPIOs de propósito general** accesibles, incluyendo los usados por este proyecto.

> La HTCC-AB02A es la variante **sin** pantalla OLED (a diferencia de otras de la familia
> CubeCell). Es ideal para nodos sensor de despliegue masivo por su tamaño y bajo consumo.

### Sensor DS18B20 (cápsula sumergible de 1 m)

El **DS18B20** (Maxim/Dallas) es un termómetro digital con salida por bus OneWire. En
este proyecto se usa la versión encapsulada en **tubo de acero inoxidable con cable de
1 m**, sellada e impermeable, apta para medir temperatura de líquidos o ambientes
húmedos.

- Rango de medición: **−55 °C a +125 °C**.
- Resolución programable 9–12 bits; aquí se fija en **12 bits** (máxima precisión).
- Error típico: **±0.5 °C** en el rango −10 °C a +85 °C.
- Alimentación: **3.0–5.5 V** (modo de 3 hilos, alimentación externa).
- Consumo en reposo: ~1.5 µA (en conversión: ~1.5–4 mA).

### Pines del DS18B20 (cápsula)

La cápsula sumergible expone tres hilos:

| Hilo | Color típico | Función |
|---|---|---|
| **VDD** | Rojo | Alimentación del sensor (3.0–5.5 V) |
| **GND** | Negro | Masa |
| **DQ** | Amarillo | Datos OneWire (open-drain, requiere pull-up) |

---

## 🔬 Principio físico de funcionamiento

### Sensor DS18B20 y protocolo OneWire

El DS18B20 mide la temperatura convirtiéndola internamente a un valor digital. En su
núcleo hay un ADC de alta resolución que traduce la temperatura a un número binario de
12 bits (modo fijo de este proyecto), donde cada LSB equivale a **0.0625 °C**. El valor
se almacena en un **scratchpad** (memoria temporal de 9 bytes) que incluye:

| Byte | Contenido |
|---|---|
| 0 | Temperatura LSB |
| 1 | Temperatura MSB (con bit de signo) |
| 2–3 | Registros de alarma TH/TL (no usados aquí) |
| 4 | Registro de configuración (resolución) |
| 5–7 | Bytes reservados (fijos `0xFF`, `0x0C`, `0x10`) |
| 8 | **CRC-8 Dallas** de los bytes 0–7 |

El bus **OneWire** usa **una sola línea de datos** (open-drain) con **pull-up externo**.
La comunicación es **maestro-esclavo** y **half-duplex**, basada en *time slots* de
duración fija:

- **Reset**: el maestro baja la línea 480 µs y luego la suelta; el sensor responde con un
  **pulso de presencia** (baja la línea) si está conectado.
- **Escritura de bit**: para escribir `1` el maestro baja la línea 1–15 µs y la suelta;
  para escribir `0` la baja 60–120 µs. Los bits se envían **LSB primero**.
- **Lectura de bit**: el maestro baja la línea brevemente y la suelta; el sensor mantiene
  la línea (bit `1`) o la baja (bit `0`). El maestro muestrea a ~15 µs.

La secuencia de lectura de temperatura implementada es:

```
Reset → Skip ROM (0xCC) → Convert T (0x44) → espera 750 ms
      → Reset → Skip ROM (0xCC) → Read Scratchpad (0xBE) → leer 9 bytes
      → validar CRC-8 → validar sanidad → calcular °C
```

En este proyecto el protocolo se implementa por **bit-banging manual** (control directo
del pin con `digitalWrite`/`digitalRead`), sin librería `DallasTemperature`. Esto ahorra
Flash y RAM en un MCU de recursos limitados y da control total del timing, necesario para
envolver las fases de bus en `noInterrupts()`.

### Power gating del sensor

Para que el nodo dure meses/años con batería, el sensor **no puede estar alimentado
durante el sueño**. El DS18B20 se alimenta desde el pin **`GPIO3/P6_2`** (VDD del sensor)
en lugar del rail fijo de 3.3 V:

- **Antes de medir**: `digitalWrite(VDD, HIGH)` → el sensor se alimenta; se esperan
  `DS18B20_POWERUP_SETTLE_MS = 10 ms` de estabilización.
- **Después de medir**: `digitalWrite(VDD, LOW)` → el sensor se apaga.
- **El pull-up externo de 4.7 kΩ se conecta a VDD del sensor** (no al 3.3 V fijo), de
  modo que al apagar VDD, el pull-up también queda sin corriente y no introduce fugas.
- **DQ en alta impedancia** (`INPUT`) durante el sueño, para no inyectar corriente por el
  pin con el sensor apagado.

De esta forma, durante los 10 minutos de sueño el sensor consume **~0 mA**.

### Deep sleep del MCU

El nodo no usa `delay()` para esperar 10 minutos (eso mantendría el MCU activo consumiendo
mA). En su lugar:

1. Se configura un **`TimerEvent_t`** (temporizador RTC) con `TimerSetValue(600000)`.
2. Se duerme el radio con `radio.sleep()` (~0.6 µA).
3. Se llama a `lowPowerHandler()` en bucle, que entra en **`CySysPmDeepSleep`** (deep
   sleep real del ASR6502) hasta que la interrupción del RTC despierta al MCU.
4. Al despertar, `radio.standby()` reactiva el radio.

Durante el deep sleep, **todas las GPIOs quedan en su último estado**, por lo que es
crítico dejar VDD del sensor en LOW y DQ en alta impedancia antes de dormir.

---

## 🔌 Pinout y conexiones

### Pines dedicados al sensor DS18B20

| Función | Pin Arduino | Pin físico | Tipo | Uso |
|---|---|---|---|---|
| **VDD (power gating)** | `GPIO3` | **P6_2** (PWM1) | Salida | Alimenta el DS18B20. HIGH al despertar (antes de medir), LOW antes de dormir. |
| **DQ (datos OneWire)** | `GPIO4` | **P6_4** (PWM2) | Entrada/Salida | Línea de datos. Requiere **pull-up externo 4.7 kΩ a VDD** del sensor. |

> **Justificación de pines**: `GPIO3`/`GPIO4` están libres, son GPIO de propósito general
> capaces de entregar ~20 mA (suficiente para los ~1.5–4 mA del DS18B20 en conversión) y
> no colisionan con pines reservados de la placa.

### Pines heredados (radio SX1262, batería, salida)

| Función | Pin | Nota |
|---|---|---|
| SX1262 NSS | 35 (**P4_3**) | Interno del SiP, no reasignable |
| SX1262 DIO1 | 38 (**P4_6**) | Interno |
| SX1262 RST | 47 (**P5_7**) | Interno |
| SX1262 BUSY | 39 (**P4_7**) | Interno |
| Salida de downlink | `GPIO1` (**P6_0**) | Comandada por `0x01`/`0x00`/`0x02` |
| Batería | **P2_0** (ADC interno) | `getBatteryVoltage()` |

### Tabla de conexiones del DS18B20 → HTCC-AB02A

| Hilo del sensor | Color típico | Conectar a |
|---|---|---|
| **VDD** | Rojo | `GPIO3` / **P6_2** |
| **GND** | Negro | GND de la placa |
| **DQ** | Amarillo | `GPIO4` / **P6_4** |
| — (pull-up) | — | Resistencia **4.7 kΩ** entre DQ y VDD del sensor |

---

## 🗺️ Diagrama de conexión

```
                    Heltec CubeCell HTCC-AB02A
                 ┌─────────────────────────────────┐
                 │                                 │
                 │  GPIO3 (P6_2)  ──────────────┐  │
                 │                               │  │
                 │  GPIO4 (P6_4)  ──────────┐    │  │
                 │                           │    │  │
                 │  GPIO1 (P6_0)  ───> [salida downlink opcional]   │
                 │                           │    │  │
                 │  GND          ────────┐   │    │  │
                 │                         │   │    │  │
                 └─────────────────────────┼───┼────┼──┘
                                           │   │    │
                 ┌─────────────────────────┼───┼────┼──────┐
                 │                         │   │    │      │
                 │   DS18B20 (cápsula)     │   │    │      │
                 │                         │   │    │      │
                 │   VDD (rojo)  ◄─────────┘   │    │      │
                 │   DQ  (amarillo) ◄──────────┘    │      │
                 │   GND (negro)  ◄─────────────────┘      │
                 │                                          │
                 │   4.7 kΩ  ── entre DQ y VDD (no al 3.3 V)│
                 └──────────────────────────────────────────┘
```

**Descripción textual del cableado:**

1. **VDD del sensor** (hilo rojo) → pin **`GPIO3` / P6_2** de la placa (alimentación
   conmutada por power gating).
2. **GND del sensor** (hilo negro) → pin **GND** de la placa.
3. **DQ del sensor** (hilo amarillo) → pin **`GPIO4` / P6_4** de la placa.
4. **Resistencia de pull-up 4.7 kΩ** conectada **entre DQ y VDD** del sensor (es decir,
   entre el pin `GPIO4/P6_4` y el pin `GPIO3/P6_2`), **no** al 3.3 V fijo. Así, al
   apagar VDD durante el sueño, el pull-up queda sin corriente y no genera fugas.
5. (Opcional) **Salida controlada por downlink** en `GPIO1 / P6_0` (relé, LED, actuador).

> ⚠️ **Importante**: el DS18B20 funciona en modo **3 hilos (alimentación externa)**. No se
> usa el modo *parasite power* (2 hilos). El pull-up externo es obligatorio porque el bus
> OneWire es open-drain.

---

## 🕹️ Forma de operar

### Puesta en marcha

1. **Editar credenciales OTAA** en `HTCC_AB02A_DS18B20.ino` (sección `#define` al inicio
   del archivo) con el `DevEUI`, `JoinEUI` y `AppKey`/`NwkKey` reales de la tarjeta.
   Registrar el `DevEUI` en el Network Server (gateway Milesight UG56/UG65).
2. **Compilar y flashear** el firmware (ver [Compilación y flasheo](#-compilación-y-flasheo)).
3. **Conectar el sensor** según el [diagrama de conexión](#-diagrama-de-conexión).
4. **Alimentar la placa** (batería LiPo o USB) y abrir el monitor serie a **115200 baud**.

### Ciclo de operación (automático)

Tras el arranque, el nodo ejecuta de forma autónoma cada **10 minutos**:

```
Despertar → standby radio → (re-join si hace falta) → VDD ON → estabilizar 10 ms
         → Reset OneWire → Skip ROM → Convert T → espera 750 ms
         → Reset → Skip ROM → Read Scratchpad → CRC-8 → sanidad → °C
         → VDD OFF → leer batería → construir payload 4 B → uplink puerto 1
         → (downlink opcional) → radio.sleep() → deep sleep 10 min
```

### Log de depuración (Serial 115200)

Al monitor serie (solo en desarrollo) se imprime un log que permite verificar el estado:

```
[INIT] HTCC-AB02A nodo DS18B20 v1.2 (RadioLib) — AU915 SB2
[INIT] DevEUI=70B3D56F27CEE98B JoinEUI=24E124C0002ADD01
[INIT] GPIOs: out=P6_0, ds18b20_vdd=P6_2, ds18b20_dq=P6_4
[INIT] Radio OK
[INIT] Node OK
[INIT] scanGuard=200 ms
[JOIN] +++ JOINED +++
[INIT] Ciclo de 600000 ms (10 min). Listo.
[TEMP] raw=234 -> 14.6 C -> 146 decimas (0x0092)
[BATT] 3660 mV
[UPLINK] port=1 payload=00920E4C (4 B)
[UPLINK] OK (sin downlink)
[SLEEP] Durmiendo 600000 ms...
```

> `raw=234` corresponde a `234 * 0.0625 = 14.625 °C`, redondeado a `146` décimas →
> `14.6 °C`.

---

## 📦 Formato del payload

### Uplink (nodo → red), puerto 1, **4 bytes big-endian**, no confirmado

| Byte(s) | Campo | Tipo | Descripción |
|---|---|---|---|
| **[0:1]** | Temperatura | `int16` big-endian | Décimas de °C en **complemento a dos**. `234` = 23.4 °C, `-55` = -5.5 °C. Rango válido DS18B20: **-550 a +1250**. |
| **[2:3]** | Batería | `uint16` big-endian | Voltaje de batería en **mV** (`getBatteryVoltage()`). |

**Codificación de la temperatura**: `int16_t tenths = (int16_t)roundf(tempC * 10.0f)`.

**Sentinel de error**: `DS18B20_INVALID = 0x7FFF` (32767), valor fuera del rango físico
del sensor. Se emite cuando hay **sensor desconectado, cortocircuito o CRC inválido**. El
decoder lo expone como `temperatura_C = null` + flag de error.

**Ejemplos de payload:**

| Bytes (hex) | Temperatura | Batería |
|---|---|---|
| `00 EA 0E 4C` | `0x00EA` = 234 → **23.4 °C** | `0x0E4C` = 3660 → **3660 mV** |
| `FF C9 0E 4C` | `0xFFC9` = -55 → **-5.5 °C** | 3660 mV |
| `7F FF 0E 4C` | Sentinel → **sensor inválido** | 3660 mV |
| `05 3C 0E 4C` | `0x053C` = 1340 → **134.0 °C** (fuera de rango, inválido) | 3660 mV |

### Downlink (red → nodo), puerto 1 (opcional)

El **primer byte** comanda la salida `GPIO1` (**P6_0**):

| Byte | Comando | Acción |
|---|---|---|
| `0x01` | ON | `digitalWrite(OUTPUT_PIN, HIGH)` |
| `0x00` | OFF | `digitalWrite(OUTPUT_PIN, LOW)` |
| `0x02` | TOGGLE | Invierte el estado actual de la salida |

Cualquier otro valor se ignora y se registra como "comando desconocido".

---

## 🧩 Configuración del decoder (ChirpStack v4)

El archivo `decoder.js` implementa el codec para el Network Server **ChirpStack v4**
(embebido en el gateway Milesight UG56/UG65). Para instalarlo:

1. En ChirpStack, ir a **Device Profile** del dispositivo.
2. En la pestaña **Codec**, pegar el contenido completo de `decoder.js`.
3. Guardar. El codec expone `Decode` (uplink) y `Encode` (downlink).

El codec devuelve, para un payload `00EA 0E4C`:

```json
{
  "temperatura_C": 23.4,
  "bateria_mV": 3660,
  "bateria_V": 3.66
}
```

Y para el sentinel `7FFF ...`:

```json
{
  "temperatura_C": null,
  "error": "SENSOR_DS18B20_INVALIDO",
  "bateria_mV": 3660,
  "bateria_V": 3.66
}
```

> La documentación detallada del codec (formato byte a byte y código completo) está en
> [`docs/datasheet.md`](docs/datasheet.md).

---

## 💡 Ejemplos de uso

### Ejemplo 1 — Monitoreo ambiental a batería

Desplegar el nodo en un ambiente húmedo (invernadero, depósito, exterior) con el sensor
sumergible dentro del medio a medir. Cada 10 minutos se reporta la temperatura y el
voltaje de batería; el power gating y el deep sleep garantizan meses de autonomía.

### Ejemplo 2 — Actuación por downlink

Conectar un relé o actuador a `GPIO1/P6_0`. Desde el NS (o una regla de ChirpStack) enviar
un downlink al puerto 1:
- `0x01` para activar (ON),
- `0x00` para desactivar (OFF),
- `0x02` para conmutar (TOGGLE).

### Ejemplo 3 — Detección de fallo de sensor

Si el sensor se desconecta o se corta, el nodo no se cuelga: reporta `temperatura_C = null`
con el flag `SENSOR_DS18B20_INVALIDO` y continúa reportando la batería. Esto permite
generar alarmas automáticas en el NS ante un sensor defectuoso.

### Ejemplo 4 — Producción masiva

Cada tarjeta se flashea con sus propias credenciales OTAA editando los `#define` al inicio
del `.ino`. Al registrar cada `DevEUI` en el NS, se mantiene la trazabilidad por tarjeta.

---

## 📏 Calibración

El **DS18B20 es un sensor digital calibrado de fábrica**; no requiere calibración manual
de su ADC (la conversión temperatura→bits es interna al sensor). Sin embargo, se
recomiendan las siguientes verificaciones:

### Verificación de offset

1. Colocar el sensor junto a un termómetro de referencia calibrado en un baño de
   temperatura estable (p. ej. agua con hielo a ~0 °C, o agua a temperatura ambiente).
2. Comparar el valor reportado (`temperatura_C`) con la referencia.
3. Si hay un **offset sistemático** (típicamente < ±0.5 °C, dentro de la especificación),
   corregirlo en la capa de aplicación (en el NS, en un panel o en el consumidor de datos),
   **no** en el firmware, ya que el firmware reporta la lectura cruda en décimas de °C.

### Puntos de referencia útiles

| Punto | Valor esperado (payload) |
|---|---|
| Hielo fundente (0 °C) | `0x0000` → 0.0 °C |
| Ambiente ~23.4 °C | `0x00EA` → 23.4 °C |
| Temperatura negativa -5.5 °C | `0xFFC9` → -5.5 °C |

### Verificación de la cadena completa

- Confirmar que el CRC-8 nunca falla de forma intermitente (≥10 lecturas consecutivas sin
  error). Si aparecen CRC inválidos intermitentes, revisar el **timing del bit-bang** (ver
  [developer.md](docs/developer.md#workarounds-de-timing-del-asr6502)).

---

## 🛠️ Compilación y flasheo

### Requisitos

- **Arduino IDE** (con soporte del core CubeCell de Heltec) **o** PlatformIO.
- **Librería RadioLib 7.7.1** (instalable desde el Gestor de Librerías de Arduino o como
  dependencia de PlatformIO).
- Placa **Heltec CubeCell HTCC-AB02A** seleccionada como destino.

### Pasos

1. **Instalar el core CubeCell** de Heltec (soporte de placas). Ver la documentación
   oficial de Heltec para añadir la URL del gestor de placas.
2. **Instalar RadioLib 7.7.1** desde el Gestor de Librerías (o añadirla a `platformio.ini`).
3. Abrir `HTCC_AB02A_DS18B20.ino`.
4. **Editar las credenciales OTAA** (`OTAA_DEVEUI`, `OTAA_APPEUI`, `OTAA_APPKEY`,
   `OTAA_NWKKEY`) al inicio del archivo.
5. Seleccionar la placa **CubeCell HTCC-AB02A** y el puerto serie.
6. **Compilar y subir**.
7. Abrir el monitor serie a **115200 baud** para verificar el join y el primer uplink.

> **Nota**: este proyecto no usa dependencias de Python (`requirements.txt` no existe).
> Toda la lógica es firmware Arduino + un codec JavaScript para el NS.

---

## ⚠️ Advertencias y precauciones

- **Credenciales**: los `DevEUI`/`AppKey` deben ser **únicos por tarjeta**. No reutilizar
  las mismas credenciales en producción masiva. Registrar cada `DevEUI` en el NS.
- **`NwkKey == AppKey`** asume **LoRaWAN 1.0.x**. Si el NS usa LoRaWAN 1.1 con `NwkKey`
  distinta, editar `OTAA_NWKKEY` de forma independiente.
- **Pull-up externo obligatorio** (4.7 kΩ) entre DQ y VDD. Sin él, el bus OneWire no
  funciona. Conectarlo a **VDD del sensor** (no al 3.3 V fijo) para no introducir fugas
  durante el sueño.
- **No alimentar el sensor con el 3.3 V fijo**: se perdería el beneficio del power gating
  y el sensor consumiría durante el sueño, agotando la batería.
- **No usar modo parasite power (2 hilos)**: el diseño asume alimentación externa de
  3 hilos.
- **DQ en alta impedancia durante el sueño**: el firmware ya lo gestiona; no añadir
  pull-up interno del MCU (ataría DQ al 3.3 V fijo e inyectaría corriente con el sensor
  apagado).
- **Duty-cycle LoRaWAN**: el ciclo de 10 minutos cumple ampliamente el límite de duty
  cycle de AU915. No reducir el intervalo de forma agresiva sin verificar la normativa
  local.
- **Rango físico**: el DS18B20 mide −55 °C a +125 °C. Valores fuera de rango (incluido el
  sentinel `0x7FFF`) indican fallo, no temperatura real.
- **Timing OneWire en ASR6502**: el bit-banging depende de la latencia de
  `digitalWrite`/`digitalRead` del core CubeCell. El firmware envuelve las fases de bus en
  `noInterrupts()`. Si aparecen CRC inválidos intermitentes, ver el fallback documentado en
  [`docs/developer.md`](docs/developer.md#workarounds-de-timing-del-asr6502).
- **Memoria limitada (16 KB RAM)**: evitar añadir buffers grandes o librerías pesadas;
  el proyecto usa buffers estáticos pequeños deliberadamente.

---

## 📁 Estructura del proyecto

```
HTCC_AB02A_DS18B20/
├── HTCC_AB02A_DS18B20.ino   # Firmware completo v1.2 (punto de entrada)
├── decoder.js               # Codec ChirpStack v4 (Decode/Encode)
├── docs/
│   ├── datasheet.md         # Documentación de usuario + especificaciones + payload/decoder
│   └── developer.md         # Documentación técnica del código fuente
├── PLAN.md                  # Plan de implementación (no versionado, ver .gitignore)
├── AUDIT_REPORT.md          # Informe de auditoría (no versionado)
└── .gitignore               # Exclusiones de control de versiones
```

---

## 📚 Documentación relacionada

- [`docs/datasheet.md`](docs/datasheet.md) — **Datasheet de usuario**: especificaciones
  técnicas, formato del payload byte a byte, decoder JS completo y guía de solución de
  problemas.
- [`docs/developer.md`](docs/developer.md) — **Documentación de desarrollo**: explicación
  del código fuente (bit-banging OneWire, cálculo de temperatura, power gating, deep
  sleep, workarounds de timing ASR6502) y guía de modificación.

---

*Proyecto basado en `HTCC_AB02A_LoRaWAN` (v1.1). Firmware v1.2 — verificado por auditoría
(APROBADO — SIN INCIDENCIAS).*
