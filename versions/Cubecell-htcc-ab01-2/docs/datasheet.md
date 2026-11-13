# Datasheet — Cubecell-htcc-ab01-2

**Documentación de usuario del nodo LoRaWAN Cubecell-htcc-ab01-2**

| | |
|---|---|
| Proyecto | `Cubecell-htcc-ab01-2` |
| Hardware | Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262) |
| Firmware | v1.0 — RadioLib 7.7.1 |
| Red | LoRaWAN 1.0.4, Clase A, OTAA |
| Región | AU915, sub-banda 2 (canales 8–15) |
| Network Server | Milesight UG56 embebido (compatible con TTN/ChirpStack) |

---

## Índice

1. [Descripción general](#1-descripción-general)
2. [Especificaciones técnicas](#2-especificaciones-técnicas)
3. [Principio de funcionamiento](#3-principio-de-funcionamiento)
4. [Instalación y puesta en marcha](#4-instalación-y-puesta-en-marcha)
5. [Forma de operar](#5-forma-de-operar)
6. [Librerías requeridas e instalación](#6-librerías-requeridas-e-instalación)
7. [Configuración del Network Server](#7-configuración-del-network-server)
8. [Guía de solución de problemas](#8-guía-de-solución-de-problemas)
9. [Compatibilidad con otras placas LoRaWAN](#9-compatibilidad-con-otras-placas-lorawan)
10. [Mantenimiento y actualización](#10-mantenimiento-y-actualización)

---

## 1. Descripción general

El **Cubecell-htcc-ab01-2** es un nodo LoRaWAN de propósito general sobre la placa **Heltec CubeCell HTCC-AB01**. En esta primera versión envía, en cada ciclo, un **número aleatorio de 32 bits** junto a una versión de payload, empaquetados en 5 bytes por el puerto 1. Su objetivo es servir como **nodo base de traza/telemetría mínima** —y como plataforma limpia sobre la que añadir sensores (el proyecto hermano con sensor térmico AMG8833 lo demuestra).

Características principales:

- **OTAA** con **persistencia total de sesión y nonces en EEPROM** → sin re-join tras power-cycle, sin rechazos `-1116` por DevNonce repetido.
- **Clase A** pura, con deep sleep agresivo (consumo de reposo en µA).
- **Downlink configurable**: SF (7–9), intervalo (1–1440 min), consulta de RSSI.
- **Config Persistente**: SF e intervalo sobreviven reinicios (EEPROM).
- **Payload mínimo**: `[0x01 | uint32 random big-endian]` (5 bytes, puerto 1).

---

## 2. Especificaciones técnicas

### 2.1 Hardware

| Parámetro | Valor |
|---|---|
| Placa | Heltec CubeCell HTCC-AB01 (revisión reciente, **sin** portapilas 1/2AA) |
| SoC | ASR6502: Cortex-M0+ @ 48 MHz + SX1262 integrado |
| Radio | Semtech SX1262, 862–930 MHz |
| Flash | 128 KiB (131.072 B) |
| RAM | ~16 KiB |
| EEPROM emulada | 512 B |
| Alimentación | Conector externo 2 cables, ~3,5–5,5 V DC |
| USB-serie | CP2102 → `/dev/ttyUSB0` en Linux |
| Antena | IPEX/U.FL o integrada (según lote) |
| LED | WS2812 RGB (P0_7) — no usado por el firmware |
| Botón | USER_KEY (P3_3) — no usado por el firmware |

### 2.2 Firmware

| Parámetro | Valor |
|---|---|
| Versión | 1.0 |
| Stack | RadioLib **7.7.1** (`LoRaWANNode`) |
| Core | CubeCell 1.4.0 |
| Región | AU915, sub-banda 2 |
| Activación | OTAA (join obligatorio, ABP no soportado) |
| Clase | A |
| ADR | Desactivado |
| Dwell time (radio) | Desactivado |
| SF de trabajo | 7..9 (whitelist de downlink) |
| Puerto uplink/downlink | 1 |
| Confirmaciones | Desactivadas (unconfirmed) |
| Intervalo por defecto | 1 minuto |
| Rango de intervalo | 1..1440 minutos |
| Uso de flash | 89.364 B / 131.072 B (68%) |
| Uso de RAM | ~1,3 KiB / ~16 KiB (~8%) |

### 2.3 Consumo (orientativo)

| Estado | Corriente (típica) |
|---|---|
| Deep sleep (radio dormida, CPU low-power) | ~3,5–15 µA (placa sin periféricos; dominado por reguladores) |
| Uplink SF7 | pico ~40–120 mA durante ~0,3 s (TX + RX windows) |
| Ciclo completo 1 min a SF7 | media del orden de ~100–300 µA |

> Los valores exactos dependen del lote de placa y del tiempo de aire real. Para estimaciones precisas, medir con un perfilador en la línea de alimentación.

---

## 3. Principio de funcionamiento

### 3.1 Diagrama de flujo

```text
                 ┌──────────────┐
                 │    RESET     │
                 └──────┬───────┘
                        ▼
        ┌────────────────────────────────┐
        │ setup()                        │
        │  · Serial 115200 + banner      │
        │  · randomSeed(micros/ADC)      │
        │  · loadConfig() ← EEPROM       │
        │  · radio.begin()               │
        │  · node.beginOTAA(...)         │
        │  · scanGuard = 200 ms          │
        │  · loadSession() ← EEPROM      │
        │  · activateOTAA()              │
        └───────────────┬────────────────┘
                        ▼
              ┌───────────────────┐         ┌──────────────────────────┐
              │ ¿isActivated()?   │──No────►│ ensureJoined()           │
              └─────────┬─────────┘         │  · activateOTAA()        │
                        │Sí                 │  · saveNoncesToEeprom()  │
                        │                   │  · ¿OK? → saveSession()  │
                        │                   │  · ¿Fallo? → sleep 10 s  │
                        │                   └────────────┬─────────────┘
                        ▼                                │
        ┌────────────────────────────────┐               │
        │ buildPayload()                 │               │
        │  [0x01 | R32 big-endian]       │               │
        ├────────────────────────────────┤               │
        │ sendReport()                   │               │
        │  · uplink puerto 1 (unconf.)   │◄──────────────┘
        │  · RX1/RX2 (Clase A)           │
        │  · ¿downlink? → whitelist      │
        │    · 0x01 SF / 0x02 INT / 0x03 │
        │  · saveSessionThrottled()      │
        ├────────────────────────────────┤
        │ deepSleep(intervalo × 60000)   │
        └───────────────┬────────────────┘
                        └──────────► (loop)
```

### 3.2 Persistencia (por qué el nodo "recuerda")

| Bloque EEPROM | Dirección | Contenido | Cuándo se guarda |
|---|---|---|---|
| Config | `0x000` | magic + SF + intervalo (4 B) | Al arrancar (defaults si corrupto) y tras cada downlink válido |
| Sesión | `0x040` | magic + nonces (14 B) + sesión LoRaWAN (302 B) | Nonces: **antes de cada intento de join**. Sesión: tras join OK y cada 32 uplinks / downlink |

La sesión LoRaWAN incluye DevAddr, claves de sesión y frame counters. Gracias a esto, un corte de energía no fuerza re-join (y no se repiten DevNonces → no hay `-1116` por nonce).

### 3.3 Lógica de energía

- **Deep sleep** con `radio.sleep()` + `lowPowerHandler()` + `TimerEvent_t`.
- Al despertar: `radio.standby()` y siguiente ciclo.
- Fallo de join: sleep de 10 s antes de reintentar (no satura el espectro).
- SF más alto (SF9) consume más por uplink; el intervalo domina el consumo medio.

---

## 4. Instalación y puesta en marcha

### 4.1 Lista de verificación

- [ ] Antena conectada.
- [ ] Core CubeCell 1.4.0 y RadioLib 7.7.1 instalados (§6).
- [ ] `credentials.h` creado y rellenado (DevEUI, JoinEUI, AppKey, NwkKey).
- [ ] Device registrado en el Network Server con esas credenciales (§7).
- [ ] Gateway UG56 en AU915 sub-banda 2, encendido y con backhaul OK.
- [ ] Compilado y grabado (§4.2).
- [ ] Monitor serie a 115200 para verificar.

### 4.2 Compilar y grabar (resumen)

```bash
arduino-cli compile --fqbn CubeCell:CubeCell:CubeCell-Board .
arduino-cli upload  -p /dev/ttyUSB0 --fqbn CubeCell:CubeCell:CubeCell-Board .
```

### 4.3 Verificación

Con el monitor serie:

```bash
timeout 30 sh -c 'sleep 60 | arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200'
```

Debe observarse el banner `[INIT]`, el join (o la sesión restaurada) y el primer `[UPLINK]`.

---

## 5. Forma de operar

### 5.1 Encendido

Alimentar por USB (programación) o por el conector externo (campo). El nodo arranca, loguea el banner y trata de unirse (o restaura sesión).

### 5.2 Indicadores

Todo se observa por el monitor serie (115200):

| Log | Significado |
|---|---|
| `[INIT] Radio OK` / `[INIT] Node OK` | Hardware listo |
| `[JOIN] +++ JOINED +++` | Join OTAA exitoso (nueva sesión) |
| `[JOIN] +++ SESION RESTAURADA +++` | Sesión recuperada de EEPROM (sin re-join) |
| `[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)` | Device no registrado o credenciales no coinciden con el NS |
| `[UPLINK] OK (sin downlink)` | Uplink aceptado por el NS |
| `[RX] ... (RSSI=..)` | Downlink recibido |
| `[DL] ...` | Acción de downlink aplicada |
| `[SLEEP] Durmiendo ...` | Entrando en deep sleep |

### 5.3 Ajustes en caliente (downlink)

Desde el Network Server, encolar en el puerto 1 (sin confirmación):

| Objetivo | Hex | Resultado |
|---|---|---|
| SF9 | `01 09` | Más alcance |
| SF7 | `01 07` | Más rápido |
| Intervalo 10 min | `02 00 0A` | `0x000A` = 10 |
| Intervalo 60 min | `02 00 3C` | `0x003C` = 60 |
| Consultar RSSI | `03 00` | Aparece en el monitor serie |

---

## 6. Librerías requeridas e instalación

### 6.1 Core CubeCell 1.4.0

```bash
arduino-cli config add board_manager.additional_urls https://resource.heltec.cn/download/package_CubeCell_index.json
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell
arduino-cli core list    # verificar: CubeCell 1.4.0
```

### 6.2 RadioLib 7.7.1

```bash
arduino-cli lib install RadioLib@7.7.1
arduino-cli lib list     # verificar: RadioLib 7.7.1
```

> **Versión exacta**: el firmware usa APIs de RadioLib 7.7.1 (`setBufferNonces`, `setBufferSession`, `SESSION_RESTORED`, orden `beginOTAA` → `loadSession` → `activateOTAA`). Con versiones anteriores el sketch **no compila** o la restauración de sesión falla.

### 6.3 FQBN

Único para toda la familia AB01 en el core 1.4.0 (no existe variante "AB01 V2"):

```
CubeCell:CubeCell:CubeCell-Board
```

---

## 7. Configuración del Network Server

### 7.1 Milesight UG56 (embebido) — caso principal

1. **Acceso**: navegador a la IP LAN del UG56 → login de administración.
2. **Network Server → Devices → Add**:
   - Device Name: `Cubecell-htcc-ab01-2`
   - DevEUI: el de `credentials.h` (16 hex)
   - JoinEUI: **el que el formulario asigne** (p. ej. `24E124C0002ADD01`); copiarlo al firmware si difiere
   - AppKey: el de `credentials.h` (32 hex)
   - Activation: OTAA — Class: A
3. **Región**: AU915, sub-banda 2.
4. **Payload Codec**: pegar `decoder.js` completo (formato `decodeUplink(input)`).
5. **Guardar** y observar el primer join en **Last Seen** / **Live Data**.

> El UG56 asigna JoinEUI automáticamente en muchos firmwares. Si no permite editarlo, **usa ese valor en `credentials.h` y re-flashea**. RadioLib rechaza join-accepts con JoinEUI distinto.

### 7.2 TTN (The Things Stack)

1. Console → Application → **Register end device**.
2. End device type: OTAA, LoRaWAN 1.0.4.
3. Frequency plan: **Australia 915-928 MHz, FSB2**.
4. DevEUI / JoinEUI (AppEUI) / AppKey.
5. Payload formatter: **Custom JavaScript** → pegar `decoder.js`.

### 7.3 ChirpStack v4

1. Device Profile: región AU915, sub-banda 2, OTAA, Class A, MAC 1.0.x.
2. Application → Add device → DevEUI/JoinEUI/AppKey.
3. Codec: **JavaScript** → pegar `decoder.js` (`decodeUplink` / `encodeDownlink`).

### 7.4 Generación de credenciales

```bash
openssl rand -hex 8     # DevEUI / JoinEUI
openssl rand -hex 16    # AppKey / NwkKey
```

Colocar en `credentials.h` en el **mismo orden** que los muestra el NS (MSB→LSB).

---

## 8. Guía de solución de problemas

### 8.1 Tabla rápida

| Síntoma | Causa probable | Acción |
|---|---|---|
| `[JOIN] Fallo: -1116` repetido | Device no registrado en el NS, o credenciales/JoinEUI distintos, o sub-banda incorrecta | Registrar/verify en UG56 (§7.1); re-flashear si cambia el JoinEUI |
| Join nunca entra, sin `-1116` | Sin cobertura, antena desconectada, gateway caído, región/sub-banda del gateway incorrecta | Revisar gateway (Last Seen, región), antena, distancia |
| No hay uplinks tras join | Intervalo muy largo, duty cycle barrado, sesión rechazada | Ver logs; revisar frame counters en el NS |
| Downlink no llega | Encolado sin uplink previo, puerto ≠ 1, device no "seen" | Encolar tras un uplink; usar puerto 1 |
| Payload no decodifica | Codec no pegado o formato incorrecto | Pegar `decoder.js` tal cual (§7.1.4) |
| Monitor serie vacío | Quirk del monitor no interactivo | Usar `timeout 30 sh -c 'sleep 60 | arduino-cli monitor ...'` |
| `arduino-cli board list` no muestra nada | CP2102 ocupado, cable de carga, permisos | `dmesg`, otro cable, grupo `dialout` |
| Compila pero no graba | Puerto incorrecto o boot en curso | Verificar `/dev/ttyUSB0` y repetir upload |

### 8.2 Análisis del error `-1116 (NO_JOIN_ACCEPT)`

Es **el error esperable** hasta completar el registro del device en el UG56:

1. El nodo envía un JoinRequest con DevEUI + DevNonce (persistido).
2. El UG56 no reconoce el DevEUI → descarta.
3. A los ~2 s, RadioLib reporta `-1116` (no llegó join-accept).
4. El firmware loguea y duerme 10 s; reintenta (~12 s por ciclo).
5. **En cuanto el device exista en el UG56 con las credenciales correctas, el siguiente intento entra solo** (sin reset).

Checklist anti `-1116`:

- [ ] DevEUI del NS == DevEUI del firmware (comparar hex completo).
- [ ] AppKey del NS == AppKey del firmware.
- [ ] JoinEUI del NS == JoinEUI del firmware (ojo con el auto-asignado).
- [ ] Sub-banda 2 en gateway y NS.
- [ ] LoRaWAN 1.0.x (no 1.1 con claves separadas mal puestas).

### 8.3 Diagnóstico avanzado

- **RSSI/SNR en el gateway**: si el join no llega al NS, el gateway no lo ve. Si el gateway lo ve pero el NS no responde, es configuración del gateway (forwarder/NS).
- **Live Data / packet logger del NS**: confirma si el join llega y si el NS lo rechaza.
- **Intento de join manual**: re-grabar con un intervalo corto (1 min) acelera el ciclo de prueba.

---

## 9. Compatibilidad con otras placas LoRaWAN

El firmware está escrito contra **RadioLib** (no contra el SDK CubeCell), pero el mapeo de pines de la radio es específico del ASR6502. Tabla orientativa:

| Placa | MCU + Radio | ¿Compila tal cual? | Adaptaciones necesarias |
|---|---|---|---|
| **Heltec CubeCell HTCC-AB01** | ASR6502 + SX1262 | ✅ Sí (objetivo) | Ninguna |
| Heltec CubeCell HTCC-AB02 / AB02A | ASR6502 + SX1262 | ⚠️ Casi | Cambiar FQBN (AB02: `CubeCell:CubeCell:CubeCell-BoardPlus`; AB02A: `CubeCell:CubeCell:CubeCell-1/2AA`), verificar pines del `Module` (varían por placa), revisar `Vext` (AB01/AB02: `LOW`=ON; **AB02A la invierte: `HIGH`=ON**) |
| Heltec CubeCell HTCC-AB01 (versión V2) | ASR6502 + SX1262 | ⚠️ | En core 1.4.0 no hay variante V2; usar `CubeCell-Board` |
| LilyGO T-Beam (v1.0–v1.1) | ESP32 + SX1276 | ❌ No directo | Cambiar `Module` a SX1276 y pines (NSS=18, DIO0=26, RST=23, BUSY n/a), `radio.begin()` distinto, core ESP32 (no CubeCell), deep sleep propio del ESP32 |
| LilyGO T-Beam (v1.2) | ESP32 + SX1262 | ❌ No directo | `Module` SX1262 con pines del T-Beam (NSS=18, DIO1=33, BUSY=32, NRST=23), core ESP32 |
| LilyGO T-Beam Supreme | ESP32-S3 + SX1262 | ❌ No directo | Pines del Supreme, core ESP32-S3 |
| Heltec WiFi LoRa 32 (v2/v3) | ESP32 + SX1276/SX1262 | ❌ No directo | Core ESP32, pines propios, `radio.begin()` según chip |
| Arduino MKR WAN 1310 | SAMD21 + CMWX1ZZABZ | ❌ No | Usar la librería `MKRWAN` (no RadioLib), otro stack |
| Nodos clase RAK (RAK4631) | nRF52840 + SX1262 | ⚠️ Casi | Core Adafruit nRF52 + RadioLib: `Module` con pines del RAK (NSS=42, DIO1=47, BUSY=46, NRST=38), deep sleep distinto |

**Resumen**: el 90% de la lógica (join persistente, whitelist, payload, EEPROM) es portable; los dos cambios por placa son siempre **(1) el `Module`/pines de radio** y **(2) el mecanismo de deep sleep** del MCU.

---

## 10. Mantenimiento y actualización

- **Actualizar firmware**: recompilar y `arduino-cli upload`. La EEPROM **no se borra** por defecto: si cambias el layout, sube los magic bytes (`CFG_MAGIC*`, `SESS_MAGIC*`) para invalidar datos viejos.
- **Borrar sesión** (forzar re-join limpio): incrementar `SESS_MAGIC0/1` en el código y re-flashear; o borrar la sesión desde el NS.
- **Cambiar región/sub-banda**: requiere editar `Region`/`subBand` en el sketch, re-flashear **y** re-registrar en un NS de esa región.
- **Registro de consumo**: medir en serie con la alimentación externa (µA en reposo, mA en TX).

---

*Última verificación de esta build: compilación 89.364 B (68%) — RAM ~8% — grabación OK en `/dev/ttyUSB0` (Silicon ID `0x256a11b5`, ASR6502).*
