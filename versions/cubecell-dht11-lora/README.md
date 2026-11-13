# CubeCell HTCC-AB02A — Sensor DHT11 LoRaWAN de Ultra-Bajo Consumo

[![Placa: Heltec CubeCell HTCC-AB02A](https://img.shields.io/badge/Placa-HTCC--AB02A-blue)](https://heltec.org/project/htcc-ab02a/)
[![MCU: ASR6501](https://img.shields.io/badge/MCU-ASR6501%20Cortex--M0%2B-green)](https://docs.heltec.org/en/node/asr650x/index.html)
[![LoRa: SX1262](https://img.shields.io/badge/LoRa-SX1262-red)](#)
[![Region: US915](https://img.shields.io/badge/Region-US915-orange)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow)](#)
[![Firmware: 92KB](https://img.shields.io/badge/Firmware-92.6%20KB-lightgrey)](#)
[![RAM: 5.5KB](https://img.shields.io/badge/RAM-34%25-brightgreen)](#)

Dispositivo IoT alimentado por bateria que despierta cada 10 minutos de deep sleep, lee un sensor de temperatura DHT11, y transmite los datos a traves de LoRaWAN (OTAA, US915, SF7). Construido sobre la placa Heltec CubeCell HTCC-AB02A, disenado para operar mas de 1 ano con una sola bateria ER14250 de 1200 mAh.

---

## Tabla de Contenidos

1. [Descripcion General](#descripcion-general)
2. [Placa Arduino Objetivo y Especificaciones](#placa-arduino-objetivo-y-especificaciones)
3. [Principio de Funcionamiento](#principio-de-funcionamiento)
4. [Forma de Operar](#forma-de-operar)
5. [Tabla de Pines (Pinout)](#tabla-de-pines-pinout)
6. [Diagrama de Conexion](#diagrama-de-conexion)
7. [Instrucciones de Compilacion y Carga](#instrucciones-de-compilacion-y-carga)
8. [Ejemplos de Uso](#ejemplos-de-uso)
9. [Formato del Payload LoRaWAN](#formato-del-payload-lorawan)
10. [Calibracion](#calibracion)
11. [Calculo de Consumo y Autonomia](#calculo-de-consumo-y-autonomia)
12. [Advertencias y Precauciones](#advertencias-y-precauciones)
13. [Documentacion Adicional](#documentacion-adicional)
14. [Referencias Tecnicas](#referencias-tecnicas)

---

## Descripcion General

Este proyecto implementa un **nodo sensor LoRaWAN de ultra-bajo consumo** que mide temperatura ambiente mediante un sensor DHT11 y transmite los datos cada 10 minutos a un gateway LoRaWAN (Milesight UG56). El dispositivo esta optimizado para funcionar con una bateria ER14250 Li-SOCl2 de 3.6 V durante mas de un ano sin recambio, alcanzando una autonomia teorica superior a 5 anos en condiciones nominales y ~1.5 anos en escenarios conservadores.

### Caracteristicas principales

- **Placa:** Heltec CubeCell HTCC-AB02A (ASR6501 Cortex-M0+ @ 48 MHz + SX1262 LoRa)
- **Bateria:** ER14250 1200 mAh Li-SOCl2 (formato 1/2 AA, holder integrado en placa)
- **Ciclo de operacion:** Deep sleep 10 minutos, despertar, medir, transmitir, dormir
- **Consumo medio:** ~26.5 uA (nominal), permitiendo anos de autonomia
- **Transmision LoRaWAN:** OTAA, US915, Clase A, SF7, unconfirmed uplink
- **Payload de 4 bytes:** numero aleatorio (2 bytes) + temperatura en decimas de C (2 bytes), little-endian
- **Power gating del sensor:** El DHT11 se alimenta desde un pin GPIO, cortando completamente su consumo (~1 mA) durante el 99.8% del tiempo
- **Protocolo DHT11 bit-bang:** Implementacion manual sin dependencias externas, lo que reduce tamano de firmware y elimina problemas de compatibilidad
- **Tamano de firmware:** ~92.6 KB (70.7% de los 128 KB de Flash)
- **RAM utilizada:** ~5.5 KB (34% de los 16 KB disponibles)

### Arquitectura del sistema (vista completa)

```
                                                         ┌──────────────────────┐
                                                         │    Network Server    │
                                                         │ ChirpStack / TTN /   │
 ┌────────────────────────────┐      LoRaWAN SF7         │ Helium               │
 │                            │      US915 125 kHz       │                      │
 │  ┌──────┐   ┌───────────┐  │  ╔══════════════════╗   │  ┌────────────────┐  │
 │  │ER14250│──▶│ Regulador │  │  ║                  ║   │  │ Payload Codec  │  │
 │  │3.6V   │   │ LDO 3.3V  │  │  ║   Gateway        ║   │  │ (JavaScript)   │  │
 │  │1200mAh│   └─────┬─────┘  │  ║   Milesight UG56  ║───┼─▶│                │  │
 │  └──────┘         │        │  ║                   ║   │  └───────┬────────┘  │
 │                   ▼        │  ╚═══════════════════╝   │          │           │
 │  ┌──────────────────────┐  │        IP/Backhaul       │          ▼           │
 │  │  ASR6501 Cortex-M0+  │  │                          │  ┌────────────────┐  │
 │  │  @ 48 MHz            │──┼──────────────────────────┼─▶│  Dashboard     │  │
 │  │                      │  │                          │  │  (Grafana,     │  │
 │  │  ┌──────┐ ┌───────┐  │  │                          │  │   Node-RED,    │  │
 │  │  │ RTC  │ │ Flash │  │  │                          │  │   InfluxDB)    │  │
 │  │  │Timer │ │128 KB │  │  │                          │  └────────────────┘  │
 │  │  └──────┘ └───────┘  │  │                          │                      │
 │  │                      │  │                          └──────────────────────┘
 │  │  ┌──────┐ ┌───────┐  │  │
 │  │  │SRAM  │ │SX1262 │  │  │
 │  │  │16 KB │ │Radio  ├──┼──┼────  Antena 915 MHz (resorte integrado)
 │  │  └──────┘ └───────┘  │  │
 │  └──────────────────────┘  │
 │           │                │
 │    ┌──────┼──────┐         │
 │    │      │      │         │
 │    ▼      ▼      ▼         │
 │  GPIO3  GPIO2   GND        │
 │  (VCC)  (DATA)             │
 │    │      │      │         │
 │  ┌─┴──────┴──────┴─┐       │
 │  │    DHT11        │       │
 │  │  Temp + Hum     │       │
 │  └─────────────────┘       │
 │                            │
 │     Heltec CubeCell        │
 │     HTCC-AB02A             │
 └────────────────────────────┘
```

---

## Placa Arduino Objetivo y Especificaciones

### Heltec CubeCell HTCC-AB02A

| Especificacion | Valor |
|---|---|
| **Microcontrolador** | ASR6501 (ARM Cortex-M0+ @ 48 MHz) |
| **Transceptor LoRa** | Semtech SX1262 integrado |
| **Memoria Flash** | 128 KB |
| **Memoria RAM** | 16 KB SRAM |
| **Conectividad** | LoRaWAN 1.0.3 Clase A/B/C, antena IPEX + resorte integrado |
| **Bateria** | Holder integrado para ER14250 (1/2 AA, Li-SOCl2) |
| **Rango de voltaje** | 2.5 V - 3.6 V (regulador LDO interno, 3.3 V nominal) |
| **RTC** | Interno, usado para wakeup desde deep sleep |
| **GPIOs disponibles** | GPIO0-GPIO5, mas pines auxiliares |
| **Interfaces** | UART, I2C, SPI, ADC, GPIO |
| **Perifericos onboard** | LED usuario, conector para OLED I2C (no usado en produccion) |
| **Deep sleep current** | ~4 uA tipico (ASR6501 STOP + SX1262 SLEEP) |
| **FQBN** | `heltec_cubecell:cubecell:HTCC-AB02A` |
| **Board package JSON** | `https://resource.heltec.cn/download/package_CubeCell_index.json` |

### Materiales necesarios (BOM - Bill of Materials)

| Cantidad | Componente | Especificacion | Coste aprox. (USD) | Notas |
|---|---|---|---|---|
| 1 | Heltec CubeCell HTCC-AB02A | Placa con ASR6501 + SX1262 | $10-15 | Incluye antena y holder de bateria |
| 1 | Sensor DHT11 | Modulo 3 pines (VCC, DATA, GND) | $1-2 | Incluye resistencia pull-up interna |
| 1 | Bateria ER14250 | Li-SOCl2 3.6V 1200 mAh 1/2AA | $2-5 | NO recargable. Desechar segun normativa |
| 3 | Cables Dupont hembra-hembra | 10-20 cm | $0.50 | Para conexion DHT11 |
| 1 | Gateway LoRaWAN | Milesight UG56 o compatible | $150-300 | US915. Ya debe estar operativo |
| 1 | Cable USB-C | Para programacion | $0-5 | Solo para carga inicial |
| **Total** | | | **~$14-$27** | (sin contar gateway) |

### Bateria ER14250

| Parametro | Valor |
|---|---|
| **Tipo** | Li-SOCl2 (Cloruro de Tionil-Litio) |
| **Voltaje nominal** | 3.60 V |
| **Capacidad** | 1200 mAh (@ 25C, 1 mA descarga continua) |
| **Voltaje de corte** | ~2.5 V |
| **Formato** | 1/2 AA (ER14250) |
| **Autodescarga** | < 1% anual |
| **Curva de descarga** | Muy plana: 3.6 V -> 3.4 V durante el 90% de la vida util |
| **Resistencia interna** | 10-30 Ohm (nueva, @ 25C) |
| **Corriente maxima continua** | 50 mA |
| **Corriente de pulso maxima** | 100 mA (< 1 segundo) |

> **IMPORTANTE:** La ER14250 es una bateria PRIMARIA (NO recargable). NUNCA intentar recargarla. Riesgo de explosion, incendio o fuga de compuestos toxicos.

---

## Principio de Funcionamiento

### Ciclo de operacion

El dispositivo opera en un ciclo continuo de 10 minutos gobernado por el RTC interno del ASR6501:

```
  ┌──────────────────────────────────────────────────────────────┐
  │                                                               │
  │   [POWER ON / RESET]                                          │
  │          │                                                    │
  │          ▼                                                    │
  │   ┌─────────────┐    ┌──────────────┐   ┌────────────────┐   │
  │   │  INIT       │───▶│  JOIN OTAA   │──▶│  SEND          │   │
  │   │  (setup)    │    │  (primera vez)│   │  (leer+enviar) │   │
  │   │  .pinMode() │    │  .join()     │   │  prepareTxFrame│   │
  │   │  .ifskipj   │    └──────┬───────┘   │  .send()       │   │
  │   └─────────────┘           │           └───────┬────────┘   │
  │                             │ (si ya hay     │               │
  │                             │  sesion: skip) │               │
  │                             └───────────────┘               │
  │                                                    │         │
  │                                                    ▼         │
  │                ┌──────────────────┐     ┌──────────────────┐ │
  │   ◄─────────── │  DEEP SLEEP      │◄────│  CYCLE           │ │
  │     RTC wakeup │  10 minutos      │     │  (programar      │ │
  │     (600 s)    │  ~4 uA consumo   │     │   siguiente TX)  │ │
  │                │  Radio.Sleep()   │     │  .cycle(time)    │ │
  │                └──────────────────┘     └──────────────────┘ │
  │                                                               │
  └──────────────────────────────────────────────────────────────┘
```

### Fases del ciclo (detalladas)

#### 1. Deep Sleep (598.85 segundos, ~4 uA)

- MCU en modo STOP. Solo el RTC y el timer de wakeup permanecen activos.
- Radio SX1262 en SLEEP mode (`Radio.Sleep()`).
- DHT11 completamente apagado (GPIO3 en LOW - power gating).
- Todos los GPIOs no utilizados en `INPUT_PULLDOWN` para evitar corrientes de fuga.
- UART deshabilitado (en produccion, `SERIAL_DEBUG=0`).
- Perifericos ADC, I2C, SPI apagados.
- Vext en LOW (corta alimentacion a perifericos externos).
- Watchdog deshabilitado, Brown-Out Detector apagado.

#### 2. Wakeup + Lectura DHT11 (~1.05 segundos, ~10 mA)

1. El RTC despierta al MCU tras 600 segundos.
2. Se activa GPIO3 (HIGH) para alimentar el DHT11 -> ~3 mA fluyendo al sensor.
3. Espera de 1000 ms para estabilizacion del sensor (requerido por datasheet DHT11).
4. Protocolo one-wire bit-bang en GPIO2:
   - **START:** MCU baja linea 20 ms, sube 40 us.
   - **ACK:** DHT11 baja 80 us, sube 80 us.
   - **Datos:** 40 bits = 5 bytes [hum_int][hum_dec][temp_int][temp_dec][checksum].
   - **Lectura de bit:** Muestrear a 40 us tras flanco HIGH. Si HIGH -> bit 1, si LOW -> bit 0.
   - Timeout de 200 us por bit para robustez.
5. Verificacion de checksum: `data[0]+data[1]+data[2]+data[3] == data[4]`.
6. Se apaga GPIO3 (LOW) inmediatamente tras la lectura.
7. Si la lectura falla, se reintenta una vez mas.

#### 3. Transmision LoRaWAN (~0.10 segundos, ~30 mA)

1. Se construye payload de 4 bytes: `[random LSB][random MSB][temp LSB][temp MSB]`.
2. Radio SX1262 se activa, sintonia en canal US915 sub-banda 1 (902.3-903.7 MHz).
3. Transmite en SF7, BW 125 kHz, CR 4/5, +14 dBm.
4. Unconfirmed uplink: NO se esperan ventanas RX1/RX2 tras TX.
5. Time-on-air: ~46 ms para payload de 4 bytes en SF7 US915.

#### 4. Preparacion para dormir (~0.005 segundos)

1. `Radio.Sleep()` apaga el SX1262 a modo SLEEP (~0.5 uA).
2. GPIO3 se asegura en LOW (DHT11 apagado).
3. Se programa RTC para 600 segundos + jitter aleatorio (evita colisiones recurrentes entre nodos).
4. GPIOs no usados vueltos a `INPUT_PULLDOWN`.
5. MCU entra en deep sleep (modo STOP).

### Diagrama temporal de un ciclo

```
   0 ms      ┌── Wakeup ────────────────────────────────────────┐
              │                                                  │
   0 ms       │ MCU despierta de deep sleep                      │
              │                                                  │
   0-1 ms     │ Inicializacion GPIOs, verificacion de estado     │
              │                                                  │
   1 ms       │ GPIO3 = HIGH (enciende DHT11)                    │
              │                                                  │
   1-1001 ms  │ ███████████████████████████████████████████████  │
              │ ███████████████████████████████████████████████  │
              │       Espera estabilizacion DHT11 (1000 ms)      │
              │                                                  │
   1001 ms    │ Protocolo DHT11: START (LOW 20ms)                │
              │                                                  │
   1021 ms    │ Protocolo DHT11: START (HIGH 40us)               │
              │                                                  │
   1021 ms    │ Protocolo DHT11: ACK + 40 bits (~4ms)            │
              │                                                  │
   1025 ms    │ GPIO3 = LOW (apaga DHT11)                        │
              │                                                  │
   1025 ms    │ Genera random, construye payload                 │
              │                                                  │
   1050 ms    │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
              │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
              │       Transmision LoRaWAN SF7 (~100 ms)          │
              │                                                  │
   1150 ms    │ Radio.Sleep(), prepara deep sleep                │
              │                                                  │
   1155 ms    │ Entra deep sleep ────────────────────┐          │
              │                                      │          │
              │                                      ▼          │
              │                        ┌─────────────────────┐  │
   1155 ms -- │ ---- 598,845 ms -----> │ DEEP SLEEP (~4 uA)  │  │
              │                        │ Solo RTC activo     │  │
   600,000 ms │ ◄─── RTC wakeup ────── │                      │  │
              │                        └─────────────────────┘  │
              │                                                  │
   └──────────┴──────────────────────────────────────────────────┘

   Duracion total fase activa: ~1.155 segundos (0.19% del ciclo de 600 s)
   Duracion deep sleep:      ~598.845 segundos (99.81% del ciclo)
```

---

## Forma de Operar

### Lo que necesitas

1. **Hardware:**
   - 1x Heltec CubeCell HTCC-AB02A
   - 1x Bateria ER14250 3.6V Li-SOCl2
   - 1x Sensor DHT11 (modulo de 3 pines: VCC, DATA, GND)
   - 3x Cables Dupont hembra-hembra (10-20 cm)
   - Gateway LoRaWAN (ej. Milesight UG56) + Network Server (ChirpStack, TTN, Helium)

2. **Software:**
   - `arduino-cli` instalado en tu computadora (v0.35+)
   - Board package de Heltec CubeCell
   - Cable USB-C para programacion (opcional: solo para carga inicial del firmware)

### Paso a paso

1. **Conectar el DHT11** siguiendo el [diagrama de conexion](#diagrama-de-conexion).

2. **Obtener credenciales LoRaWAN** de tu Network Server:
   - Registra el dispositivo en tu Network Server (ChirpStack, TTN, Helium, etc.).
   - Anota: DevEUI, AppEUI/JoinEUI, AppKey.
   - Si usas `LORAWAN_DEVEUI_AUTO`, el DevEUI se genera automaticamente desde el Chip ID del ASR6501.

3. **Configurar credenciales:**
   - Edita `credentials.h` y reemplaza los valores placeholder (`0x00`) con tus credenciales reales.
   - Formato: little-endian (LSB first).
   - Ejemplo:
     ```cpp
     // credentials.h - ejemplo con valores reales
     uint8_t devEui[8] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
     uint8_t appEui[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
     uint8_t appKey[16] = {
         0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
         0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90
     };
     ```

4. **Compilar y cargar** el firmware (ver [Instrucciones de Compilacion y Carga](#instrucciones-de-compilacion-y-carga)).

5. **Insertar la bateria ER14250** en el holder de la placa (observar polaridad: + hacia arriba en el holder de la HTCC-AB02A).

6. **Verificar operacion:**
   - Si `SERIAL_DEBUG=1`, conecta por USB y abre monitor serial a 115200 baud.
   - Al arrancar, veras el banner y el proceso de JOIN OTAA.
   - Cada 10 minutos: lectura DHT11, construccion de payload, transmision LoRaWAN, deep sleep.

7. **En produccion:**
   - Desconecta el USB (el dispositivo funciona solo con bateria).
   - Establece `SERIAL_DEBUG=0` en el codigo para ahorrar ~1 mA del UART.
   - El dispositivo operara de forma autonoma durante anos.

### Verificacion de cobertura LoRaWAN (prueba de rango)

Antes del despliegue definitivo, realizar una prueba de cobertura:

1. Cargar firmware con `SERIAL_DEBUG=1`.
2. Conectar USB y abrir monitor serial.
3. Colocar el dispositivo en la ubicacion definitiva.
4. Verificar que el JOIN es exitoso y se reciben datos en el Network Server.
5. Mover el dispositivo gradualmente mientras se monitorean RSSI y SNR en el Network Server.
6. Valores recomendados: RSSI > -120 dBm, SNR > -15 dB para SF7.
7. Si la senal es marginal, el dispositivo seguira funcionando (ADR puede subir el SF), pero el consumo aumentara.

---

## Tabla de Pines (Pinout)

### Conexiones del proyecto

| Pin Arduino | GPIO Fisico | Modo | Componente Conectado | Notas |
|---|---|---|---|---|
| GPIO0 | P2_3 | ADC_IN | VBAT_ADC_CTL | Monitoreo de voltaje de bateria (opcional). No usado activamente en este proyecto. Lectura con `analogRead(ADC)`. |
| GPIO1 | P6_0 | INPUT_PULLDOWN | -- (no usado) | Configurado como pulldown para evitar corriente de fuga por pin flotante (~100 uA si se deja flotante). |
| **GPIO2** | **P6_1** | **INPUT** (cambia a OUTPUT temporalmente) | **DHT11 -- DATA** | Protocolo one-wire propietario del DHT11. Implementacion bit-bang: START (OUTPUT LOW 20 ms + OUTPUT HIGH 40 us) -> ACK (INPUT) -> lectura de 40 bits (INPUT, muestreo a 40 us tras flanco HIGH). **Sin pull-up externo** (el modulo DHT11 incluye resistencia interna de ~5.1 kOhm). |
| **GPIO3** | **P6_2** | **OUTPUT** | **DHT11 -- VCC (alimentacion)** | **CRITICO -- POWER GATING.** Este pin suministra 3.3 V al sensor DHT11. HIGH = sensor encendido (~1-3 mA). LOW = sensor apagado (~0 uA). Capacidad de corriente del pin: ~8 mA max. El consumo tipico del DHT11 es de 1-3 mA, muy por debajo del limite del pin. |
| GPIO4 | P6_4 | INPUT_PULLDOWN | -- (I2C SDA, no usado) | En prototipado: OLED I2C SDA. En produccion: INPUT_PULLDOWN para evitar fugas. |
| GPIO5 | P3_4 | INPUT_PULLDOWN | -- (I2C SCL, no usado) | En prototipado: OLED I2C SCL. En produccion: INPUT_PULLDOWN para evitar fugas. |
| Vext | -- | OUTPUT (LOW) | -- (alimentacion perifericos) | Control de alimentacion para perifericos externos. Se mantiene LOW para cortar consumo (~0 uA a perifericos). |
| VBAT | -- | POWER_IN | Bateria ER14250 (+) | 3.6 V nominal. Holder integrado en placa, formato 1/2 AA. Polaridad: + hacia arriba. |
| GND | -- | GND | Bateria ER14250 (-) + DHT11 GND | Tierra comun para bateria y sensor. |
| LED (integrado) | -- | OUTPUT | LED usuario onboard | Usado por el framework LoRaWAN para indicar estado de JOIN. Se apaga automaticamente durante deep sleep. |
| USB | -- | UART / POWER | Puerto USB-C | Solo para programacion y debug (115200 baud, 8N1). No necesario en produccion. |

### Notas sobre pines

- **GPIO2 (DATA):** El protocolo DHT11 requiere temporizacion precisa a nivel de microsegundos. La implementacion usa `delayMicroseconds()` y `micros()` del ASR6501 a 48 MHz. El pin cambia entre `OUTPUT` (para enviar START) e `INPUT` (para leer datos). Despues de cada operacion (exito o error), se restaura a `INPUT` para evitar contention en el bus.
- **GPIO3 (VCC):** Capacidad de corriente de ~8 mA. El DHT11 consume ~1-3 mA, muy por debajo del limite. Verificar que el pin este en LOW antes de entrar en deep sleep (el codigo lo asegura en `DEVICE_STATE_SLEEP` y en todas las ramas de error de `leerDHT11()`).
- **GPIO1, GPIO4, GPIO5:** Configurados como `INPUT_PULLDOWN` para prevenir estados flotantes que causan consumo adicional. Un pin CMOS flotante puede oscilar y consumir hasta ~100 uA.
- **VBAT_ADC_CTL (GPIO0):** Permite leer el voltaje de bateria mediante ADC interno (12 bits). No habilitado en este firmware base, pero puede anadirse para telemetria de bateria (requiere ampliar el payload a 6 bytes y leer el ADC durante la fase activa).
- **Vext:** Pin de control de alimentacion para perifericos onboard (OLED, sensores I2C). En LOW se corta el suministro. La placa HTCC-AB02A tiene este pin disponible; en otras revisiones puede no estar expuesto.

### Tabla de consumos por pin

| Pin | Estado en Deep Sleep | Corriente de fuga | Consumo |
|---|---|---|---|
| GPIO0 | No configurado (reset state) | Indeterminado | ~0 uA (ADC apagado en sleep) |
| GPIO1 | INPUT_PULLDOWN | ~0 uA | ~0 uA |
| GPIO2 (DATA) | INPUT (tras lectura) | ~0 uA | ~0 uA |
| GPIO3 (VCC DHT11) | OUTPUT LOW | ~0 uA | ~0 uA |
| GPIO4 | INPUT_PULLDOWN | ~0 uA | ~0 uA |
| GPIO5 | INPUT_PULLDOWN | ~0 uA | ~0 uA |
| Vext | OUTPUT LOW | ~0 uA | ~0 uA |
| LED | LOW (apagado) | ~0 uA | ~0 uA |

---

## Diagrama de Conexion

### Conexion fisica DHT11 -- CubeCell HTCC-AB02A

```
                    Heltec CubeCell HTCC-AB02A
                    ╔═══════════════════════════════╗
                    ║     ┌─────────────────┐       ║
   Bateria          ║     │  Holder ER14250 │       ║
   ER14250          ║  VBAT (+) ◄─── ER14250 +      ║
   3.6V             ║  GND  (-) ◄─── ER14250 -      ║
   1200 mAh         ║     └─────────────────┘       ║
   (NO recargable)  ║                               ║
                    ║  ┌─── GPIO ──────────────────┐║
                    ║  │                            │║
                    ║  │                            │║
                    ║  │ GPIO2 (P6_1) ───────────►  │║──┐
                    ║  │   DATA (amarillo/naranja)   │║  │
                    ║  │                            │║  │
                    ║  │ GPIO3 (P6_2) ───────────►  │║──┤
                    ║  │   VCC (rojo)               │║  │
                    ║  │                            │║  │
                    ║  │ GND  ────────────────────► │║──┤
                    ║  │   GND (negro/marron)       │║  │
                    ║  │                            │║  │
                    ║  └────────────────────────────┘║  │
                    ╚═══════════════════════════════╝  │
                                                       │
                                                       │      DHT11
                                                       │   ┌──────────────┐
                                                       ├───│ Pin 1: VCC   │
                                                       ├───│ Pin 2: DATA  │
                                                       └───│ Pin 3: GND   │
                                                           └──────────────┘
```

### Codigo de colores recomendado

```
   DHT11           Color del cable        CubeCell
   ──────────────────────────────────────────────────
   Pin 1 (VCC)     Rojo  ──────────────── GPIO3 (P6_2)
   Pin 2 (DATA)    Amarillo / Naranja ─── GPIO2 (P6_1)
   Pin 3 (GND)     Negro / Marron ─────── GND
```

### Esquema electrico funcional

```
   ┌──────────────────────────────────────────────────────┐
   │                  CubeCell HTCC-AB02A                  │
   │                                                       │
   │   VBAT ──────┬──── ER14250 (+) 3.6V                  │
   │              │                                        │
   │   GND  ──────┼──── ER14250 (-)                       │
   │              │    ┌─────────┐                         │
   │              │    │         │                         │
   │   GPIO3 ─────┼────┤ DHT11   │                         │
   │   (P6_2)     │    │  VCC    │                         │
   │              │    │         │                         │
   │   GPIO2 ─────┼────┤ DHT11   │  Pull-up interno        │
   │   (P6_1)     │    │  DATA   │◄─ ~5.1 kOhm ── VCC    │
   │              │    │         │                         │
   │   GND  ──────┴────┤ DHT11   │                         │
   │                   │  GND    │                         │
   │                   └─────────┘                         │
   │                                                       │
   │   Notas:                                              │
   │   - La resistencia pull-up de ~5.1 kOhm esta          │
   │     incluida en el modulo DHT11.                      │
   │   - NO se requiere resistencia externa adicional.     │
   │   - GPIO3 suministra 3.3V al DHT11 solo durante       │
   │     la lectura (~1.05s cada 600s).                    │
   │   - El pin de VCC del DHT11 NO debe conectarse a     │
   │     VBAT o 3.3V fijo. Usar EXCLUSIVAMENTE GPIO3.     │
   └──────────────────────────────────────────────────────┘
```

### Vista general del sistema

```
┌──────────────────┐          ┌─────────────────┐          ┌──────────────────┐
│                  │          │                 │          │                  │
│  CubeCell        │  SF7     │  Gateway        │  IP      │  Network Server  │
│  HTCC-AB02A      ├─────────►│  Milesight UG56 ├─────────►│  ChirpStack/TTN  │
│  + DHT11         │  US915   │                 │  MQTT    │  /Helium         │
│  + ER14250       │  125 kHz │  Puerto: 1700   │  HTTP    │                  │
│                  │          │                 │          │                  │
│  Payload: 4B     │  ~46 ms  │  SF7-SF12       │          │  Decodificador JS│
│  Puerto: 1       │  TOA     │  US915 8ch      │          │  Puerto: 1       │
│  Clase: A        │          │                 │          │                  │
│                  │          │                 │          │                  │
└──────────────────┘          └─────────────────┘          └────────┬─────────┘
                                                                     │
                                                           ┌─────────▼─────────┐
                                                           │  Dashboard / App  │
                                                           │  (Grafana, etc.)  │
                                                           └───────────────────┘
```

### Advertencias de cableado

| Riesgo | Prevencion |
|---|---|
| Cables largos en DATA (> 20 cm) | La capacitancia parasita en cables largos distorsiona los flancos del protocolo DHT11. Usar cables de maximo 20 cm. |
| GPIO3 cortocircuitado a GND | Verificar aislamiento de las conexiones. Usar conectores Dupont con funda aislante. |
| Conexion de VCC DHT11 a VBAT | Si se conecta a VBAT (3.6V constante), el DHT11 consumira ~1 mA continuamente, agotando la bateria en ~50 dias. |
| Polaridad de bateria invertida | El holder de la HTCC-AB02A tiene proteccion mecanica contra inversion. Verificar + hacia arriba. |

---

## Instrucciones de Compilacion y Carga

### Prerrequisitos

- `arduino-cli` instalado (v0.35+ recomendado)
- Cable USB-C para conectar la placa al ordenador
- Conexion a internet (para descargar el board package)
- Sistema operativo: Linux, macOS, o Windows (con WSL o Git Bash)

### 1. Instalar arduino-cli

```bash
# Linux / macOS
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Verificar instalacion
arduino-cli version
# Ejemplo de salida: arduino-cli 0.35.0
```

### 2. Instalar el board package de Heltec CubeCell

```bash
# Agregar el indice de placas de Heltec
arduino-cli config add board_manager.additional_urls https://resource.heltec.cn/download/package_CubeCell_index.json

# Actualizar el indice de placas
arduino-cli core update-index

# Instalar el core de CubeCell
arduino-cli core install heltec_cubecell:cubecell

# Verificar instalacion
arduino-cli core list | grep cubecell
# Ejemplo de salida: heltec_cubecell:cubecell  1.5.0  Heltec CubeCell Board
```

### 3. Configurar las credenciales LoRaWAN

```bash
# Editar el archivo de credenciales
nano credentials.h

# Reemplazar los valores placeholder (0x00) con tus credenciales reales:
#   devEui[8]  -- Device EUI (o dejar 0x00 si usas LORAWAN_DEVEUI_AUTO)
#   appEui[8]  -- Application EUI / JoinEUI
#   appKey[16] -- Application Key
```

### 4. Compilar el sketch

```bash
# Desde el directorio del proyecto
# PROYECTO_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/cubecell-dht11-lora
cd PROYECTO_DIR

# Compilacion basica con FQBN
arduino-cli compile --fqbn heltec_cubecell:cubecell:HTCC-AB02A .

# Compilacion con opciones de configuracion completas:
arduino-cli compile \
  --fqbn "heltec_cubecell:cubecell:HTCC-AB02A:\
LORAWAN_REGION=REGION_US915,\
LORAWAN_NETMODE=LORAWAN_OTTA,\
LORAWAN_CLASS=LORAWAN_CLASS_A,\
LORAWAN_UPLINKMODE=LORAWAN_UNCNF,\
LORAWAN_ADR=ADR_OFF,\
LORAWAN_NET_RESERVE=LORAWAN_NET_RESERVE_ON,\
LORAWAN_AT_SUPPORT=AT_SUPPORT_OFF,\
LORAWAN_DEVEUI_AUTO=LORAWAN_DEVEUI_AUTO_ON" \
  .

# Salida esperada:
# Sketch uses 92640 bytes (70%) of program storage space. Maximum is 131072 bytes.
# Global variables use 5500 bytes (33%) of dynamic memory, leaving 10884 bytes for local variables.
```

### 5. Transferir a la placa

```bash
# Identificar el puerto
arduino-cli board list
# Linux:   /dev/ttyUSB0
# macOS:   /dev/cu.usbmodem*
# Windows: COM3 (requiere driver CH340/CP2102)

# Transferir el firmware
arduino-cli upload \
  -p /dev/ttyUSB0 \
  --fqbn heltec_cubecell:cubecell:HTCC-AB02A \
  .

# En macOS:
# arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn heltec_cubecell:cubecell:HTCC-AB02A .

# En Windows (PowerShell como Administrador):
# arduino-cli upload -p COM3 --fqbn heltec_cubecell:cubecell:HTCC-AB02A .
```

### 6. Verificar operacion (monitor serial)

```bash
# Abrir monitor serial (115200 baud)
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200

# Salida esperada al arranque:
# === CubeCell HTCC-AB02A | DHT11 + LoRaWAN ===
# Bateria: ER14250 1200mAh | Intervalo: 10 min
# Payload: random(2B) + temp(2B) | US915 OTAA SF7
# Region: 3
# [JOIN] Joining...
# [JOIN] Joined!
# DHT11 OK: 24.5 C (245 decimas)
# Payload [4B]: random=42789 (0xA725), temp=24.5 C | HEX: 25 A7 F5 00
# Deep sleep 610 segundos...
#
# --- (despierta tras 10 min) ---
#
# DHT11 OK: 25.1 C (251 decimas)
# Payload [4B]: random=12843 (0x322B), temp=25.1 C | HEX: 2B 32 FB 00
# Deep sleep 608 segundos...
```

### Compilacion sin placa fisica (CI/CD o verificacion)

```bash
arduino-cli compile --fqbn heltec_cubecell:cubecell:HTCC-AB02A .
# Verifica que compila sin errores, sin necesidad de placa conectada.
```

### Solucion de problemas de compilacion

| Error | Causa | Solucion |
|---|---|---|
| `Error: board heltec_cubecell:cubecell:HTCC-AB02A not found` | Board package no instalado | Ejecutar paso 2 completo |
| `fatal error: LoRaWan_APP.h: No such file` | FQBN incorrecto | Usar `heltec_cubecell:cubecell:HTCC-AB02A` exactamente |
| `fatal error: credentials.h: No such file` | Falta el archivo de credenciales | Crear `credentials.h` con los placeholders del repositorio |
| `Sketch too big` | Binario excede 128 KB | Verificar que no se hayan anadido librerias pesadas. El binario base es ~92 KB |
| `Permission denied: /dev/ttyUSB0` | Permisos de puerto serial | `sudo chmod a+rw /dev/ttyUSB0` o anadir usuario al grupo `dialout` |
| `Timed out waiting for upload` | Placa no detectada | Presionar RESET en la placa durante la transferencia |

### Configuracion de opciones en Arduino IDE (alternativa grafica)

Si prefieres usar el Arduino IDE tradicional en lugar de arduino-cli, configura estas opciones en **Tools**:

| Menu | Opcion |
|---|---|
| Board | Heltec CubeCell HTCC-AB02A |
| LoRaWAN Region | REGION_US915 |
| LoRaWAN Network Mode | LORAWAN_OTTA |
| LoRaWAN Class | LORAWAN_CLASS_A |
| LoRaWAN Uplink Mode | LORAWAN_UNCNF |
| LoRaWAN ADR | ADR_OFF |
| LoRaWAN Net Reserve | LORAWAN_NET_RESERVE_ON |
| LoRaWAN AT Support | AT_SUPPORT_OFF |
| LoRaWAN DevEUI Auto | LORAWAN_DEVEUI_AUTO_ON |
| Upload Speed | 921600 |
| Port | /dev/ttyUSB0 (o el que corresponda) |

---

## Ejemplos de Uso

### Ejemplo 1: Transmision normal (temperatura valida)

**Monitor Serial (SERIAL_DEBUG=1):**
```
=== CubeCell HTCC-AB02A | DHT11 + LoRaWAN ===
Bateria: ER14250 1200mAh | Intervalo: 10 min
Payload: random(2B) + temp(2B) | US915 OTAA SF7
Region: 3
[JOIN] Joining...
[JOIN] Joined!
DHT11 OK: 24.5 C (245 decimas)
Payload [4B]: random=42789 (0xA725), temp=24.5 C | HEX: 25 A7 F5 00
Deep sleep 610 segundos...

--- (despierta tras 10 min) ---

DHT11 OK: 25.1 C (251 decimas)
Payload [4B]: random=12843 (0x322B), temp=25.1 C | HEX: 2B 32 FB 00
Deep sleep 608 segundos...
```

**Decodificacion del payload (Network Server):**

| Ciclo | Payload HEX | Random (uint16) | Temp (decimas) | Temp (C) |
|---|---|---|---|---|
| N | `25 A7 F5 00` | 0xA725 = 42789 | 0x00F5 = 245 | 24.5 C |
| N+1 | `2B 32 FB 00` | 0x322B = 12843 | 0x00FB = 251 | 25.1 C |
| N+2 | `A1 B2 10 01` | 0xB2A1 = 45729 | 0x0110 = 272 | 27.2 C |

### Ejemplo 2: Error de lectura DHT11 (sensor desconectado o fallo)

**Monitor Serial:**
```
DHT11 OK: 24.0 C (240 decimas)
Payload [4B]: random=55123 (0xD753), temp=24.0 C | HEX: 53 D7 F0 00
Deep sleep 612 segundos...

--- (siguiente ciclo: DHT11 desconectado) ---

ERROR: DHT11 timeout o no responde
ERROR: DHT11 timeout o no responde
Payload [4B]: random=8921 (0x22D9), temp=ERROR | HEX: D9 22 FF FF
Deep sleep 605 segundos...
```

**Decodificacion:**
- Temperatura `0xFFFF` = valor centinela que indica error de lectura.
- El Network Server debe interpretar `0xFFFF` como "dato no disponible" y no como -0.1 C.
- `0xFFFF` como int16_t = -1. El decodificador debe tratar -1 como error.

### Ejemplo 3: Decodificacion en JavaScript (para integracion con Node-RED o similar)

```javascript
/**
 * Decodificador de uplink para CubeCell DHT11 LoRaWAN Sensor.
 * Formato payload: [random_LSB, random_MSB, temp_LSB, temp_MSB]
 * Little-endian. Puerto LoRaWAN: 1.
 */
function decodeUplink(input) {
  var bytes = input.bytes;

  if (bytes.length < 4) {
    return { data: { error: "Payload debe ser al menos 4 bytes" } };
  }

  // Random: 2 bytes uint16 little-endian
  var randomValue = bytes[0] | (bytes[1] << 8);

  // Temperatura: 2 bytes int16 little-endian
  var tempDecimas = bytes[2] | (bytes[3] << 8);

  // Manejar signo (int16_t)
  if (tempDecimas >= 32768) {
    tempDecimas = tempDecimas - 65536;
  }

  var data = {
    random: randomValue,
    temp_c: null,
    temp_decimas: tempDecimas,
    error: false
  };

  if (tempDecimas === -1) { // 0xFFFF interpretado como int16_t = -1
    data.temp_c = null;
    data.error = true;
  } else {
    data.temp_c = tempDecimas / 10.0;
  }

  return { data: data };
}

// Ejemplo: payload 25 A7 F5 00
var result = decodeUplink({bytes: [0x25, 0xA7, 0xF5, 0x00]});
// result.data = { random: 42789, temp_c: 24.5, temp_decimas: 245, error: false }
```

### Ejemplo 4: Decodificacion en Python

```python
import struct

def decode_payload(payload_hex: str) -> dict:
    """
    Decodifica payload de 4 bytes (hex string sin espacios).
    Retorna dict con random y temperatura.
    """
    payload = bytes.fromhex(payload_hex)
    if len(payload) != 4:
        raise ValueError(f"Payload debe ser 4 bytes, recibidos {len(payload)}")

    # Little-endian: <H = uint16, <h = int16
    random_val = struct.unpack('<H', payload[0:2])[0]
    temp_decimas = struct.unpack('<h', payload[2:4])[0]

    result = {
        'random': random_val,
        'temp_decimas': temp_decimas,
        'temp_c': None,
        'error': False
    }

    if temp_decimas == -1:  # 0xFFFF como int16_t = -1
        result['error'] = True
    else:
        result['temp_c'] = temp_decimas / 10.0

    return result

# Ejemplo
payload_hex = "25A7F500"
data = decode_payload(payload_hex)
print(data)
# {'random': 42789, 'temp_decimas': 245, 'temp_c': 24.5, 'error': False}

# Lote de ejemplos
payloads = ["25A7F500", "2B32FB00", "D922FFFF"]
for p in payloads:
    d = decode_payload(p)
    estado = "ERROR" if d['error'] else f"{d['temp_c']}C"
    print(f"  Payload {p}: random={d['random']}, temp={estado}")
```

### Ejemplo 5: Decodificacion en Node-RED (Function node)

```javascript
var bytes = msg.payload.bytes || msg.payload;

var random = bytes[0] | (bytes[1] << 8);
var tempRaw = bytes[2] | (bytes[3] << 8);
if (tempRaw >= 32768) tempRaw -= 65536;

msg.payload = {
    random: random,
    temperature_c: tempRaw === -1 ? null : tempRaw / 10.0,
    temperature_error: tempRaw === -1,
    timestamp: new Date().toISOString()
};

return msg;
```

### Ejemplo 6: Integracion MQTT con Home Assistant

```yaml
# configuration.yaml (Home Assistant)
mqtt:
  sensor:
    - name: "Cubecell DHT11 Temperatura"
      state_topic: "application/1/device/+/event/up"
      unit_of_measurement: "°C"
      value_template: >-
        {% set payload = value_json.object %}
        {% if payload.temperature_error %}
          unavailable
        {% else %}
          {{ payload.temperature_c }}
        {% endif %}
      device_class: temperature
```

---

## Formato del Payload LoRaWAN

| Offset | Tamano | Tipo | Descripcion |
|---|---|---|---|
| 0 | 2 bytes | `uint16_t` (little-endian) | Numero aleatorio (0-65535). Generado con `random(65536)`. |
| 2 | 2 bytes | `int16_t` (little-endian) | Temperatura en decimas de C. Ej: 245 = 24.5 C. `0xFFFF` = error de lectura. |

**Total: 4 bytes.** Puerto LoRaWAN: 1. Modo: unconfirmed uplink.

### Representacion visual del payload

```
Byte 0    Byte 1    Byte 2    Byte 3
┌────────┬────────┬────────┬────────┐
│ Rand   │ Rand   │ Temp   │ Temp   │
│ LSB    │ MSB    │ LSB    │ MSB    │
│ uint16 little-endian │ int16 little-endian │
└────────┴────────┴────────┴────────┘
  [0]      [1]      [2]      [3]

Ejemplo: 24.5 C, random=42789 (0xA725):
┌────────┬────────┬────────┬────────┐
│  0x25  │  0xA7  │  0xF5  │  0x00  │
└────────┴────────┴────────┴────────┘
  Random=0xA725=42789   Temp=0x00F5=245 (24.5 C)

Ejemplo: error de sensor:
┌────────┬────────┬────────┬────────┐
│  0xD9  │  0x22  │  0xFF  │  0xFF  │
└────────┴────────┴────────┴────────┘
  Random=0x22D9=8921     Temp=0xFFFF (ERROR)
```

---

## Calibracion

El DHT11 es un sensor digital calibrado de fabrica. No requiere calibracion por parte del usuario. Sin embargo, se recomiendan las siguientes verificaciones:

### Verificacion de precision

1. **Comparacion con termometro de referencia:**
   - Coloca el DHT11 junto a un termometro calibrado en un ambiente con temperatura estable.
   - Espera al menos 30 minutos para equilibrar termicamente ambos sensores.
   - Compara la lectura del payload con la del termometro de referencia.
   - Precision esperada del DHT11: +-2 C (tipica +-1 C en rango 0-50 C).

2. **Prueba de estabilidad en ciclo de power gating:**
   - El DHT11 se apaga y enciende cada 10 minutos.
   - La primera lectura tras power-on (con 1s de estabilizacion) debe ser consistente con lecturas consecutivas.
   - Si la primera lectura difiere significativamente (> 1 C) de lecturas sin power cycling, aumentar `DHT11_STABILIZE_MS` a 2000 ms.

3. **Compensacion por autocalentamiento de la placa:**
   - La placa CubeCell genera calor durante la transmision LoRa (~30 mA x 3.3V = ~100 mW durante ~100 ms).
   - Este calor es insignificante comparado con los 10 minutos de inactividad. No se requiere compensacion.
   - Si el DHT11 esta muy proximo al modulo SX1262, alejarlo al menos 2 cm.

### Ajuste de constantes

| Constante | Valor por defecto | Cuando ajustar |
|---|---|---|
| `DHT11_STABILIZE_MS` | 1000 ms | Aumentar a 2000 ms si la primera lectura es inconsistente (especialmente en ambientes frios < 10 C) |
| `DHT11_TIMEOUT_US` | 200 us | Aumentar a 300 us si hay timeouts frecuentes con bateria baja (< 2.8 V) |
| `SERIAL_DEBUG` | 1 | Cambiar a 0 en produccion para ahorrar ~1 mA del UART |

### Verificacion del valor centinela

Para probar el comportamiento de error intencionadamente:

1. Desconecta el cable DATA del DHT11 (GPIO2).
2. Espera al siguiente ciclo de transmision.
3. Verifica que el monitor serial muestra `ERROR: DHT11 timeout o no responde` (dos veces, por el reintento).
4. Verifica que el payload contiene `FF FF` en los bytes de temperatura.
5. Verifica que el Network Server interpreta el valor como "dato no disponible".

---

## Calculo de Consumo y Autonomia

### Parametros de consumo por fase

| Fase | Corriente | Duracion | Energia por ciclo | % del ciclo |
|---|---|---|---|---|
| Deep Sleep (MCU STOP + SX1262 SLEEP) | 4 uA (tipico) | 598.845 s | 2,395.4 uAs | 99.81% |
| Wakeup + inicializacion | ~8 mA | 0.001 s | 8 uAs | < 0.01% |
| DHT11 power-on + estabilizacion | ~10 mA | 1.000 s | 10,000 uAs | 0.17% |
| Lectura DHT11 (protocolo bit-bang) | ~10 mA | 0.025 s | 250 uAs | < 0.01% |
| Generacion de random | ~5 mA | 0.001 s | 5 uAs | < 0.01% |
| TX LoRaWAN (SF7, +14 dBm, 125 kHz) | ~30 mA | 0.100 s | 3,000 uAs | 0.02% |
| Preparacion para sleep | ~5 mA | 0.005 s | 25 uAs | < 0.01% |
| **Total por ciclo (600 s)** | -- | **600.00 s** | **15,683 uAs** | **100%** |

### Corriente media

```
I_media = Energia total / Duracion del ciclo
        = 15,683 uAs / 600 s
        = 26.14 uA
```

### Estimacion de vida util

```
Vida util = Capacidad bateria / Corriente media
          = 1200 mAh / 0.02614 mA
          = 45,908 horas
          = 1,913 dias
          = 5.24 anos (nominal)
```

### Escenarios de autonomia

| Escenario | Sleep I | TX time | I_media | Vida estimada | Cumple >1 ano? |
|---|---|---|---|---|---|
| Optimista (nominal) | 4 uA | 100 ms | 26.1 uA | **~5.2 anos** | Si |
| Realista | 8 uA | 150 ms | 34.8 uA | **~3.9 anos** | Si |
| Conservador | 15 uA | 200 ms | 49.0 uA | **~2.8 anos** | Si |
| Peor caso | 20 uA | 300 ms | 65.6 uA | **~2.1 anos** | Si |
| Extremo (bateria vieja, frio) | 25 uA | 400 ms | 83.3 uA | **~1.6 anos** | Si |
| Catastrofico (GPIO flotante) | 100 uA | 500 ms | 125.3 uA | **~1.1 anos** | Si (marginal) |

### Estrategias de optimizacion energetica implementadas

1. **Power gating del DHT11:** GPIO3 alimenta el sensor solo durante la lectura (~1.05s de cada 600s). Ahorra ~1 mA continuo que consumiria si estuviera siempre encendido.
2. **Unconfirmed uplink:** No espera ventanas RX1/RX2 (~1-2s de radio encendida ahorrados por ciclo). Reduce el tiempo de radio de ~100ms a ~100ms (la radio se apaga inmediatamente tras TX).
3. **SF7:** Menor time-on-air (~46 ms para 4 bytes en US915 125 kHz). Si se usara SF12, el time-on-air seria ~990 ms (20x mas energia).
4. **GPIOs en INPUT_PULLDOWN:** Evita corrientes de fuga por pines flotantes (hasta ~100 uA por pin en CMOS).
5. **Radio.Sleep() explicito:** SX1262 en modo SLEEP (~0.5 uA) en lugar de standby (~1 mA).
6. **randomSeed() solo en setup():** Evita operaciones de ADC innecesarias en cada ciclo.
7. **Vext en LOW:** Corta alimentacion a perifericos externos (OLED, sensores I2C).
8. **Sin librerias externas:** Protocolo DHT11 implementado manualmente, eliminando overhead y dependencias.

### Impacto de la temperatura en la autonomia

| Temperatura ambiente | Capacidad relativa ER14250 | Autonomia estimada |
|---|---|---|
| +25 C | 100% | ~5.2 anos |
| +10 C | ~90% | ~4.7 anos |
| 0 C | ~80% | ~4.1 anos |
| -10 C | ~70% | ~3.6 anos |
| -20 C | ~50% | ~2.6 anos |
| -30 C | ~30% | ~1.5 anos |

---

## Advertencias y Precauciones

### ADVERTENCIA: Seguridad electrica

| Riesgo | Prevencion |
|---|---|
| **Cortocircuito en GPIO3:** Si GPIO3 (power gating) se cortocircuita a GND, la corriente puede exceder el limite del pin (~8 mA), danando el ASR6501. | Verificar aislamiento de conexiones. Usar cables con conectores Dupont hembra con funda aislante. |
| **Polaridad de bateria invertida:** La ER14250 invertida puede danar el regulador LDO de la placa (> 3.6V inverso = LDO destruido). | El holder de la placa tiene proteccion mecanica contra inversion. Verificar que el signo + esta hacia arriba. |
| **Voltaje de bateria fuera de rango:** < 2.5 V puede causar comportamiento erratico del ASR6501 (resets, corrupcion de flash). | No usar baterias agotadas. El dispositivo dejara de funcionar correctamente por debajo del cutoff. |
| **Carga de bateria:** La ER14250 es una bateria PRIMARIA (NO recargable). La recarga puede causar explosion, incendio o fuga de compuestos toxicos de tionil cloruro. | NUNCA intentar recargar una Li-SOCl2. Usar solo reemplazo con bateria nueva identica. |
| **Corriente de pin excedida:** GPIO3 suministrando > 8 mA (especificacion del ASR6501). | El DHT11 consume ~1-3 mA, muy por debajo del limite. No conectar cargas adicionales a GPIO3. |

### ADVERTENCIA: Operacion

| Riesgo | Prevencion |
|---|---|
| **Credenciales placeholder en produccion:** Usar valores `0x00` en `credentials.h` impedira la conexion al Network Server. El dispositivo entrara en bucle de JOIN fallido. | Reemplazar SIEMPRE los placeholders en `credentials.h` antes de desplegar. |
| **credentials.h versionado:** Si se commitea con credenciales reales, quedaran expuestas en el repositorio publico. | El archivo esta en `.gitignore`. Verificar con `git status` antes de cada commit. NO hacer `git add -f credentials.h`. |
| **Desconexion del DHT11:** Si el DHT11 se desconecta, el payload enviara `0xFFFF` como temperatura. El Network Server debe manejar este valor centinela. | El decodificador JavaScript incluido ya maneja `0xFFFF` como `temperature_error: true`. |
| **Cobertura LoRaWAN insuficiente:** Si el dispositivo esta fuera de cobertura, las transmisiones se perderan silenciosamente. El dispositivo no almacena datos -- lo no transmitido se pierde para siempre. | Verificar cobertura con un dispositivo de prueba antes del despliegue definitivo. Monitorear RSSI/SNR en el Network Server. |
| **Colisiones LoRaWAN:** Multiples nodos transmitiendo en el mismo instante y canal pueden colisionar. | El jitter aleatorio (`randr()`) en el intervalo de sleep reduce la probabilidad de colisiones recurrentes. |
| **Temperatura extrema:** DHT11 opera en 0-50 C. La ER14250 pierde capacidad significativamente por debajo de -20 C. | No desplegar en ambientes fuera del rango 0-50 C sin validacion previa. |
| **Re-join tras deep sleep:** Si `keepNet = false`, el dispositivo hara OTAA join en cada ciclo (~5-10s de radio encendida por ciclo = bateria agotada en dias). | `keepNet = LORAWAN_NET_RESERVE` esta habilitado por defecto. Verificar que no se ha cambiado accidentalmente. |
| **Interferencia con otros dispositivos US915:** La banda 902-928 MHz es compartida. Dispositivos como telefonos LTE banda 8, microondas industriales, etc. | La modulacion LoRa es robusta frente a interferencia de banda estrecha. SF7 es el mas vulnerable; si hay mucha interferencia, ADR puede subir el SF automaticamente (requiere habilitar ADR). |

### ADVERTENCIA: Limitaciones del DHT11

| Limitacion | Impacto |
|---|---|
| **Precision:** +-2 C | No usar para aplicaciones que requieran precision < 1 C. Considerar DHT22 (+-0.5 C) o BME280 (+-0.5 C) para mayor precision. |
| **Rango:** 0-50 C | No apto para ambientes bajo cero o extremadamente calurosos (> 50 C). |
| **Frecuencia de muestreo:** Max. 1 Hz (1 lectura/segundo) | No es limitante para este proyecto (1 lectura cada 10 minutos). |
| **Tiempo de estabilizacion:** Requiere 1s tras power-on | Ya contemplado en `DHT11_STABILIZE_MS = 1000`. |
| **Deriva temporal:** La precision del DHT11 puede degradarse con los anos en ambientes humedos o corrosivos | Para aplicaciones de larga duracion (> 3 anos), considerar recalibracion o sustitucion del sensor. |

### ADVERTENCIA: Pruebas de escritorio vs produccion

- Durante el desarrollo con USB conectado, la placa recibe alimentacion adicional (~5V via USB) que enmascara el consumo real de bateria.
- Para medir el consumo real, usar un multimetro en serie con la bateria ER14250 (rango uA), con el USB **desconectado**.
- El LED onboard consume ~2-5 mA cuando esta encendido. El framework LoRaWAN lo usa para indicar estado de JOIN. En produccion no es relevante porque el JOIN solo ocurre una vez al inicio de la vida del dispositivo.
- La corriente de deep sleep medida en laboratorio puede ser mayor que en campo debido a la capacitancia de las puntas del multimetro y el ruido electromagnetico del entorno de pruebas.

---

## Documentacion Adicional

- [`docs/datasheet.md`](docs/datasheet.md) -- Documentacion de usuario detallada: especificaciones tecnicas, formato de payload, decodificacion en multiples lenguajes (JavaScript, Python, C++), configuracion de Network Servers (ChirpStack, TTN, Helium), solucion de problemas comunes, tabla de compatibilidad con otras placas y regiones LoRaWAN, especificaciones de bateria ER14250, y cumplimiento normativo (FCC/CE).
- [`docs/developer.md`](docs/developer.md) -- Documentacion para desarrolladores: explicacion exhaustiva del codigo fuente, arquitectura del sketch, maquina de estados LoRaWAN (6 estados), funcionamiento detallado de `setup()` y `loop()`, API de funciones auxiliares (`leerDHT11()`, `dht11ReadByte()`, `dht11WaitFor()`, `generarRandom()`, `prepareTxFrame()`), diagrama de flujo de la logica principal, protocolo DHT11 bit-bang explicado paso a paso con diagramas de temporizacion, guia de modificacion y extension (cambio de sensor, region, anadir humedad, monitoreo de bateria), y directrices de contribucion.

---

## Referencias Tecnicas

| Recurso | URL |
|---|---|
| Heltec CubeCell HTCC-AB02A (producto) | [https://heltec.org/project/htcc-ab02a/](https://heltec.org/project/htcc-ab02a/) |
| CubeCell Arduino Framework (GitHub) | [https://github.com/HelTecAutomation/CubeCell-Arduino](https://github.com/HelTecAutomation/CubeCell-Arduino) |
| Documentacion ASR650x | [https://docs.heltec.org/en/node/asr650x/index.html](https://docs.heltec.org/en/node/asr650x/index.html) |
| SX1262 Datasheet | Semtech SX1261/2 Long Range Low Power LoRa Transceiver |
| DHT11 Datasheet | [Mouser DHT11 Technical Data Sheet](https://www.mouser.com/datasheet/2/758/DHT11-Technical-Data-Sheet-Translated-Version-1143054.pdf) |
| LoRaWAN Regional Parameters US915 | RP002-1.0.3 LoRaWAN Regional Parameters |
| Milesight UG56 Gateway | [https://www.milesight.com/iot/product/lorawan-gateway/ug56](https://www.milesight.com/iot/product/lorawan-gateway/ug56) |
| ER14250 Battery Specification | Li-SOCl2 1/2AA 3.6V 1200mAh primary cell |
| Arduino CLI Documentation | [https://arduino.github.io/arduino-cli/](https://arduino.github.io/arduino-cli/) |
| LoRaWAN Specification 1.0.3 | [https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-3/](https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-3/) |
| ChirpStack (Network Server) | [https://www.chirpstack.io/](https://www.chirpstack.io/) |
| The Things Network | [https://www.thethingsnetwork.org/](https://www.thethingsnetwork.org/) |

---

**Proyecto disenado para operacion autonoma de ultra-bajo consumo con mas de 5 anos de autonomia teorica en bateria ER14250 (> 1 ano garantizado en peor caso). Ideal para monitoreo ambiental de largo plazo con infraestructura LoRaWAN.**
