# Cubecell-htcc-ab01-2 — Nodo LoRaWAN OTAA (AU915 sub-banda 2) sobre Heltec CubeCell HTCC-AB01

Nodo sensor LoRaWAN minimalista basado en la placa **Heltec CubeCell HTCC-AB01** (SoC ASR6502 + radio SX1262). El firmware está construido sobre **RadioLib 7.7.1** (implementación LoRaWAN 1.0.4 completa en C++) e implementa un ciclo de trabajo **Clase A** con:

- **Join OTAA obligatorio** (nada de ABP), con **persistencia total de la sesión y de los nonces en EEPROM**: el nodo sobrevive power-cycles sin re-join y sin el clásico rechazo `-1116` por DevNonce repetido.
- **Uplink periódico en el puerto 1**: 5 bytes con versión de payload + número aleatorio de 32 bits (big-endian). El intervalo es configurable en runtime (1–1440 min) por downlink.
- **Downlink whitelist estricto en el puerto 1**: cambiar Spreading Factor (SF7–SF9), cambiar intervalo de TX, o consultar el RSSI del último downlink. Sin confirmación (unconfirmed), como manda el espíritu Clase A.
- **Deep sleep agresivo** entre ciclos: radio dormida, CPU en modo low-power, despertar por TimerEvent. Consumo en reposo del orden de microamperios.
- Configuración **persistida en EEPROM** (SF y intervalo sobreviven reinicios).

> **Estado de campo**: el firmware está compilado y grabado correctamente (verificado: 89.364 B / 68% de flash, RAM ~8%, upload OK en `/dev/ttyUSB0`). El join OTAA se aceptará cuando el Network Server (gateway **Milesight UG56** del usuario) tenga registradas las credenciales del nodo con el DevEUI/JoinEUI/AppKey exactos. Mientras tanto el nodo reintenta cada ~12 s y reporta `-1116 (NO_JOIN_ACCEPT)`. Ver [§12 Solución de problemas](#12-solución-de-problemas).

---

## Índice

1. [Placa objetivo y especificaciones](#1-placa-objetivo-y-especificaciones)
2. [Qué hace el dispositivo (principio de funcionamiento)](#2-qué-hace-el-dispositivo-principio-de-funcionamiento)
3. [Forma de operar](#3-forma-de-operar)
4. [Tabla de pines (pinout)](#4-tabla-de-pines-pinout)
5. [Diagrama de conexión](#5-diagrama-de-conexión)
6. [Configuración LoRaWAN (Network Server)](#6-configuración-lorawan-network-server)
7. [Guía de comandos downlink](#7-guía-de-comandos-downlink)
8. [Formato de payload](#8-formato-de-payload)
9. [Compilación y carga](#9-compilación-y-carga)
10. [Ejemplos de uso (salida del monitor serie)](#10-ejemplos-de-uso-salida-del-monitor-serie)
11. [Arquitectura del firmware](#11-arquitectura-del-firmware)
12. [Solución de problemas](#12-solución-de-problemas)
13. [Advertencias y precauciones](#13-advertencias-y-precauciones)
14. [Referencias](#14-referencias)

---

## 1. Placa objetivo y especificaciones

| Parámetro | Valor |
|---|---|
| Placa | Heltec CubeCell HTCC-AB01 (revisión reciente, **sin** holder de pila 1/2AA) |
| Variante de placa del core | `CubeCell-Board` (FQBN `CubeCell:CubeCell:CubeCell-Board`) |
| MCU | ASR6502 (ASR650x) — ARM Cortex-M0+ @ 48 MHz + radio LoRa integrada SX1262 en el mismo die |
| Radio | SX1262 (LoRa), 862–930 MHz |
| Memoria Flash | 128 KiB (131.072 B) — uso del firmware: **89.364 B (68%)** |
| RAM | ~16 KiB — uso del firmware: **≈1,3 KiB (~8%)** |
| EEPROM emulada | 512 B (layout propio, ver §11) |
| USB ↔ Serie | Puente **CP2102** integrado (aparece como `/dev/ttyUSB0`) |
| Alimentación | Conector externo de 2 cables (versión sin portapilas); también por USB para programar |
| Antena | U.FL/IPEX o antena integrada según lote — **verificar antes de transmitir** |
| LED | WS2812 RGB en `GPIO4` (P0_7) — **no usado** por este firmware |
| Botón | USER_KEY en `P3_3` — **no usado** por este firmware |

### Stack de software

| Componente | Versión | Origen |
|---|---|---|
| Core Arduino CubeCell | 1.4.0 | `package_CubeCell_index.json` (resource.heltec.cn) |
| RadioLib | **7.7.1** | Arduino Library Manager / librería de usuario `~/Arduino/libraries/RadioLib` |
| Arduino CLI | ≥ 1.0 | binario del sistema (`~/.local/bin/arduino-cli`) |

> Nota: el core 1.4.0 **no** tiene variante "V2" para la AB01. El FQBN único es `CubeCell:CubeCell:CubeCell-Board`, incluso para las revisiones recientes de la placa.

---

## 2. Qué hace el dispositivo (principio de funcionamiento)

El nodo ejecuta una máquina de estados muy simple, optimizada para **Clase A** y **deep sleep**:

```text
┌─────────────────────────────────────────────────────────────────┐
│ setup()                                                         │
│  ├─ Serial 115200, seed del PRNG (micros/millis/ADC)           │
│  ├─ Carga config de EEPROM (SF, intervalo)                      │
│  ├─ radio.begin()  (SX1262)                                     │
│  ├─ node.beginOTAA(JoinEUI, DevEUI, NwkKey, AppKey)             │
│  ├─ loadSession()  ← restaura nonces+sesión de EEPROM si hay    │
│  └─ activateOTAA() → JOINED (nueva sesión) o SESIÓN RESTAURADA  │
│                                                                 │
│ loop()                                                          │
│  ├─ ¿Sesión activa? ──No──► ensureJoined() → ¿fallo?            │
│  │                             ├─Sí: deepSleep 10 s y retry     │
│  │                             └─No: guardar sesión en EEPROM   │
│  ├─ buildPayload() → [0x01, R32big-endian]                      │
│  ├─ sendReport() → uplink puerto 1 + ventana RX1/RX2            │
│  │     └─ si llega downlink: whitelist → aplicar y persistir    │
│  └─ deepSleep(intervalo_min × 60000 ms)                         │
└─────────────────────────────────────────────────────────────────┘
```

### 2.1 Join OTAA con persistencia (anti `-1116`)

El stack LoRaWAN genera un **DevNonce** nuevo en cada intento de join. Si el nodo se reinicia sin recordar los nonces usados, repite DevNonces y el Network Server los rechaza (en RadioLib: error `-1116`, `NO_JOIN_ACCEPT`). Este firmware resuelve el problema de raíz:

1. **Antes de cada intento de join** (`activateOTAA()` / `ensureJoined()`), guarda el buffer de nonces en EEPROM (`saveNoncesToEeprom()`).
2. **Tras un join exitoso**, guarda la sesión completa (DevAddr, claves de sesión, frame counters) en EEPROM.
3. **En cada arranque**, `loadSession()` restaura nonces + sesión **después de `beginOTAA()` y antes de `activateOTAA()`** (orden obligatorio en RadioLib 7.7.1). Si la sesión restaurada es válida, `activateOTAA()` devuelve `SESSION_RESTORED` y **no hace falta re-join**.
4. Durante la operación, la sesión se re-guarda de forma **throttled cada 32 uplinks** (para no desgastar la EEPROM) y siempre que llega un downlink.

Resultado: power-cycle = arranque en segundos, sin DevNonces repetidos, sin rechazos del NS.

### 2.2 Uplink

Cada ciclo de trabajo:
- Construye 5 bytes: `0x01` (versión de payload) + `uint32` aleatorio big-endian.
- Llama a `sendReceive(payload, len, fPort=1, ..., confirmed=false)`.
- Tras el uplink abre las ventanas RX1/RX2 (comportamiento Clase A estándar).

El **número aleatorio** se siembra con `micros() ^ (millis() << 16) ^ (analogRead(ADC) << 8) ^ analogRead(ADC)` — entropía suficiente para un identificador de traza, no para criptografía. En esta primera versión del proyecto el payload **solo** lleva ese contador aleatorio (decisión explícita del usuario); el sensor térmico AMG8833 y la lectura de batería quedan como extensiones futuras (ver `docs/developer.md`).

### 2.3 Downlink (Clase A)

Tras cada uplink, si el Network Server encola un downlink en el puerto 1, el nodo lo procesa **inmediatamente** (whitelist estricta, comandos de 1–3 bytes):

| Comando | Bytes | Acción |
|---|---|---|
| Cambiar SF | `0x01 <SF>` (SF 7..9) | `setADR(false)` + re-aplica DR + persiste en EEPROM |
| Cambiar intervalo | `0x02 <hi> <lo>` | Intervalo en minutos (1..1440) + persiste en EEPROM |
| Consultar RSSI | `0x03 0x00` | Reporta por puerto serie el RSSI del downlink recibido |

Sin ACK (downlink no confirmado): el nodo **no** gasta energía confirmando cada comando; si el comando no llega, se reintenta desde el NS.

### 2.4 Deep sleep

Entre ciclos el nodo entra en deep sleep:

- `radio.sleep()` para dormir el SX1262.
- Bucle `lowPowerHandler()` (del core CubeCell) hasta que expira un `TimerEvent_t` programado con `deepSleepMillis()`.
- Al despertar: `radio.standby()` y siguiente ciclo.

Variables críticas (`gCfg`, `gLastRssi`, `gUplinksSinceSave`) viven en **SRAM retenida** durante el deep sleep, por lo que el estado no se pierde entre ciclos sin necesidad de re-guardar en EEPROM.

> El firmware **no** usa `setSleepFunction()` de RadioLib: esa API es para las esperas internas del stack LoRaWAN (p. ej. retardos entre comandos MAC), no para el deep sleep de la aplicación. Con `scanGuard=200` ms + `TimerEvent_t` + `lowPowerHandler()` se cubre el ciclo completo (detalle en `docs/BITACORA.md`).

---

## 3. Forma de operar

### 3.1 Puesta en marcha

1. **Antena conectada** (siempre, antes de energizar).
2. Preparar credenciales: `cp credentials.example.h credentials.h` y rellenar DevEUI/JoinEUI/AppKey/NwkKey (ver §6).
3. **Registrar el device en el Network Server** (UG56 → `Network Server → Devices`, ver §6).
4. Compilar y grabar (§9).
5. Abrir el monitor serie a 115200 baudios (§10) para observar join, uplink y downlink.

### 3.2 Operación normal

- El nodo despierta, (re)une o restaura sesión, envía un uplink y vuelve a dormir.
- Intervalo por defecto: **1 minuto**.
- Ajustes en caliente por downlink (§7): SF, intervalo, consulta de RSSI.
- Los ajustes **persisten en EEPROM**: apagar y encender el nodo no los pierde.

### 3.3 Lectura de estado

Toda la telemetría de operación sale por el puerto serie a **115200 baudios**:

- `[INIT]`, `[CFG]`, `[SESS]`, `[JOIN]`, `[REJOIN]` — arranque y join.
- `[UPLINK]` — payload enviado, SF y tamaño máximo permitido.
- `[RX]` / `[DL]` — downlink recibido y acción aplicada.
- `[SLEEP]` — tiempo de sueño.

---

## 4. Tabla de pines (pinout)

### 4.1 Radio SX1262 — conexión interna fija (NO reasignable)

El SX1262 está integrado en el SoC ASR6502; estas señales están cableadas internamente y **el firmware las fija** en `Module mod(35, 38, 47, 39)`:

| Función | Pin CubeCell | GPIO | En el sketch |
|---|---|---|---|
| NSS (CS) | `SS` (P4_3) | 35 | `Module mod(35, ...)` |
| DIO1 (IRQ) | P4_6 | 38 | `Module mod(..., 38, ...)` |
| BUSY | P5_7 | 47 | `Module mod(..., 47, ...)` |
| NRST (reset) | P4_7 | 39 | `Module mod(..., ..., ..., 39)` |
| SCK | `SCK` (P4_2) | 34 | (bus SPI interno) |
| MOSI | `MOSI` (P4_0) | 32 | (bus SPI interno) |
| MISO | `MISO` (P4_1) | 33 | (bus SPI interno) |

### 4.2 Pines de E/S de usuario de la HTCC-AB01

| Pin silkscreen | Nombre core | GPIO | Notas |
|---|---|---|---|
| GPIO0 | P0_2 | 2 | Digital |
| GPIO1 | P6_1 | 49 | Digital |
| GPIO2 | P6_2 | 50 | Digital / **PWM1** |
| GPIO3 | P6_4 | 52 | Digital / **PWM2** |
| GPIO4 | P0_7 | 7 | Digital — **LED WS2812 RGB** en la placa |
| GPIO5 | P0_6 | 6 | Digital |
| GPIO6 | P3_2 | 26 | Digital — **= Vext** (no usar si se controla Vext) |
| GPIO7 | P3_3 | 27 | Digital — **= USER_KEY / VBAT_ADC_CTL** (no usar si se lee batería) |
| ADC | P2_3 | 19 | Entrada analógica — **medición de VBAT** vía `getBatteryVoltage()` |
| SDA | P0_1 | 1 | I²C (Wire) |
| SCL | P0_0 | 0 | I²C (Wire) |
| UART_RX | P3_0 | 24 | Serie1 — **monitor serie / programación vía CP2102** |
| UART_TX | P3_1 | 25 | Serie1 — **monitor serie / programación vía CP2102** |
| Vext | P3_2 | 26 | **Control de alimentación conmutada de periféricos** — en la HTCC-AB01: `LOW` = ON (encendido), `HIGH` = OFF |
| USER_KEY | P3_3 | 27 | Botón integrado (activo a GND) |
| VBAT_ADC_CTL | P3_3 | 27 | Habilitador del divisor de batería (pullup externo 10K a VDD) |

### 4.3 Uso en este firmware

| Pin | Uso en el sketch |
|---|---|
| `Vext` (P3_2) | `pinMode(OUTPUT)` + `digitalWrite(LOW)` → **Vext ON** (en la AB01, `LOW` = encendido). En v1.0 no hay periféricos conectados: efecto inocuo. Al añadir sensores, poner `HIGH` antes del deep sleep |
| `UART_RX/TX` | Monitor serie 115200 vía CP2102 |
| `SS/DIO1/BUSY/NRST` | Radio SX1262 (fijos) |
| `RGB` (P0_7) | **No usado** (la librería `CubeCell_NeoPixel` no se inicializa) — el LED queda apagado/indeterminado |
| `ADC` (P2_3) | **No usado** en payload; base para extensión "leer batería" con `getBatteryVoltage()` |
| `USER_KEY` (P3_3) | **No usado**; base para extensión "forzar join/rejoin con pulsación" |

> **Aviso de polaridad Vext**: en la HTCC-AB01 (y AB02) la lógica es **activa-baja: `LOW` = ON, `HIGH` = OFF** (el factory test oficial de la AB01 energiza con `digitalWrite(Vext, LOW)`). La excepción de la familia es la **HTCC-AB02A**, que invierte la polaridad (`HIGH` = ON). El sketch escribe `LOW` tras el arranque, por lo que en esta AB01 el rail Vext queda **encendido**; como v1.0 no tiene periféricos conectados, el efecto es inocuo. Si añades un sensor alimentado por Vext: se enciende con `LOW` y se corta con `HIGH` antes de `deepSleepMillis()`. Verifica con multímetro antes de conectar nada a ese pad.

---

## 5. Diagrama de conexión

Este nodo **no requiere cableado externo**: la radio y la USB-serie ya están integradas. El único "cableado" es la alimentación y (opcionalmente) la antena.

```text
                         ┌───────────────────────────────────────────┐
                         │        Heltec CubeCell HTCC-AB01          │
                         │                                           │
      ┌──────────┐       │   ┌─────────────┐    ┌───────────────┐    │
      │  PC (USB)│◄─────►│   │  CP2102     │    │  ASR6502      │    │
      │          │ USB   │   │  USB-UART   │◄──►│  Cortex-M0+   │    │
      └──────────┘       │   └─────────────┘    │  + SX1262     │    │
                         │                      └──────┬────────┘    │
                         │                             │             │
                         │                        (antena IPEX)      │
                         │                             │             │
                         └─────────────────────────────┼─────────────┘
                                                       ▼
                                                 ┌───────────┐
                                                 │  Antena   │
                                                 │  915 MHz  │
                                                 └───────────┘

                    Alimentación externa (versión sin portapilas):
                    ┌──────────────────────────────────────────────┐
                    │  Fuente / batería 3.7–5.5 V DC               │
                    │        (+) ──────► conector 2 pines (+)      │
                    │        (−) ──────► conector 2 pines (−) GND  │
                    └──────────────────────────────────────────────┘
```

### 5.1 Descripción textual de las conexiones

1. **USB ↔ PC**: el puente CP2102 aparece como `/dev/ttyUSB0` (Linux). Con este cable se graba y se abre el monitor serie. En la AB01 reciente, el mismo cable USB alimenta la placa.
2. **Antena**: conectar la antena de 915 MHz al conector IPEX/U.FL (o desplegar la integrada según lote) **antes** de energizar. Transmitir sin antena puede dañar el PA del SX1262.
3. **Alimentación externa** (uso en campo): conector de 2 cables. Respetar polaridad: **positivo al pin marcado `+`**, negativo a `GND`. Rango admitido del CubeCell: 3,5–5,5 V (típ. batería LiPo 3,7 V o portapilas AA×3). No alimentar simultáneamente por USB y externo salvo que la fuente externa sea estable.
4. **Sin más periféricos**: en esta versión del firmware no hay sensores externos. La extensión con AMG8833 (I²C: SDA=P0_1, SCL=P0_0, alimentado por Vext) está prevista para una versión futura (ver `docs/developer.md`).

---

## 6. Configuración LoRaWAN (Network Server)

### 6.1 Registro del device en el gateway Milesight UG56 (embebido)

El **UG56** integra un Network Server LoRaWAN propio. Pasos:

1. Acceder a la web de administración del UG56 (IP LAN del gateway en el navegador).
2. Ir a **Network Server → Devices (End Devices)** y pulsar **Add** / **Create Device**.
3. Rellenar:
   - **Device Name**: `Cubecell-htcc-ab01-2` (o el que prefieras).
   - **DevEUI**: 16 hex — debe coincidir **exactamente** con `devEui` de `credentials.h`.
   - **JoinEUI (AppEUI)**: el formulario suele **auto-asignar** un JoinEUI (p. ej. `24E124C0002ADD01`). Si lo auto-asigna, **usa ese valor también en `credentials.h`** (RadioLib exige que el JoinEUI del nodo coincida con el del NS). Si el formulario permite editarlo, puedes poner el tuyo.
   - **AppKey**: 32 hex — debe coincidir con `appKey` de `credentials.h`.
   - **NwkKey**: si el NS lo pide (LoRaWAN 1.0.x usa la misma clave; en RadioLib `nwkKey` suele ser igual a `appKey` en OTAA 1.0.x). El firmware pasa ambas.
   - **Activation**: **OTAA**.
   - **Device Class**: **Class A**.
4. **Región/frecuencia**: **AU915** y **sub-banda 2** (canales 8–15). Debe coincidir con el gateway y con el firmware.
5. **Payload Codec**: pegar el contenido de `decoder.js` (ver §6.4). En el UG56/ChirpStack v4 el entrypoint es `decodeUplink(input)`.
6. Guardar. El device quedará "Nunca visto" hasta el primer join.

> **⚠️ El bloqueo `-1116`**: hasta que el device esté registrado en el UG56 con las credenciales correctas, el nodo logueará `[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)` cada ~12 s. Es un **pendiente operativo del NS**, no un bug del firmware. En cuanto el registro exista, el siguiente intento entra.

#### Alternativa: TTN / ChirpStack (fuera del UG56)

El firmware es estándar OTAA LoRaWAN 1.0.4, así que también funciona contra:

- **TTN (The Things Stack)**: crear Application → End device → OTAA → región `AU915` con **Frequency Plan "Australia 915-928 MHz, FSB2"** → pegar DevEUI/JoinEUI (AppEUI)/AppKey.
- **ChirpStack (v4)**: Device Profile (región AU915, sub-banda 2, OTAA 1.0.x, Class A) → Application → Add Device → Device EUI/JoinEUI/AppKey → Codec (`decodeUplink`/`encodeDownlink`).

> **⚠️ La sub-banda del NS, del gateway y del nodo DEBEN coincidir**: AU915 sub-banda 2 = canales 8–15 (923,3 MHz uplink). Si el gateway está en sub-banda 1 (canales 0–7) el join nunca entrará aunque las claves sean correctas.

### 6.2 Credenciales OTAA (`credentials.h`)

Crear `credentials.h` a partir de la plantilla (nunca se versiona):

```bash
cp credentials.example.h credentials.h
```

| Constante | Tamaño | Formato | Descripción |
|---|---|---|---|
| `joinEui`  | 8 B | `0x` + 16 hex, sufijo `ULL` | JoinEUI (AppEUI). Si el UG56 lo auto-asigna, **copiarlo tal cual** aquí. |
| `devEui`   | 8 B | `0x` + 16 hex, sufijo `ULL` | DevEUI del nodo (único en el mundo). |
| `appKey[16]` | 16 B | array de 16 bytes `0x..` | AppKey OTAA (LoRaWAN 1.0.x). |
| `nwkKey[16]` | 16 B | array de 16 bytes `0x..` | NwkKey. En 1.0.x = misma clave que AppKey. |

Generación de credenciales de prueba:

```bash
openssl rand -hex 8    # DevEUI y JoinEUI (16 hex = 8 bytes)
openssl rand -hex 16   # AppKey y NwkKey (32 hex = 16 bytes)
```

Colocarlas en `credentials.h` en el mismo orden MSB→LSB que muestra el Network Server (los `uint64_t` están en big-endian lógico; copiar el hex tal cual aparece en el NS).

> **🔑 Aviso de secretos**: `credentials.h` está en `.gitignore` y **nunca** debe subirse a un repositorio. En la documentación y en `credentials.example.h` los valores son placeholders `0x00`.

### 6.3 Región y sub-banda

| Parámetro | Valor en este proyecto | Dónde se configura |
|---|---|---|
| Región LoRaWAN | `AU915` | firmware (`Region Region = AU915;`) |
| Sub-banda | `2` (canales 8–15) | firmware (`subBand = 2`) y NS/gateway |
| Clase | A | firmware (comportamiento) y NS (registro) |
| ADR | **Desactivado** | firmware (`setADR(false)`) |
| Dwell time | **Desactivado** | firmware (`setDwellTime(false)`) |
| SF de trabajo | 7..9 | firmware (config EEPROM, downlink) |

El límite SF7–SF9 existe por el **dwell time** de AU915: en sub-banda 2 los canales de 500 kHz ya cumplen dwell; el firmware capa SF ≤ 9 para respetar tiempos de aire aun con dwell desactivado en radio. La tabla SF→DR usada: SF7→DR5, SF8→DR4, SF9→DR3, SF10→DR2, SF11→DR1, SF12→DR0.

### 6.4 Pegar el `decoder.js` en el Network Server

El archivo `decoder.js` del proyecto implementa **los dos formatos**:

- **Formato principal** (Milesight UG56, ChirpStack v4, TTN v3): `decodeUplink(input)` / `encodeDownlink(input)` con `input.bytes` / `input.fPort`.
- **Wrappers legacy v3** (`Decode`/`Encode`) para plataformas antiguas.

Pasos en el UG56:

1. Abrir el device recién creado → pestaña **Payload Codec** (o "Codec").
2. Seleccionar el formato **JavaScript / Custom**.
3. **Pegar el contenido completo de `decoder.js`** — tal cual, sin modificar.
4. Guardar. Llegará un uplink de prueba y el NS mostrará los campos decodificados (`version`, `random`).

> El decoder fue escrito en **ES5 puro** (sin `let`, `const`, arrow functions) para máxima compatibilidad con los motores JS embebidos de los Network Servers.

---

## 7. Guía de comandos downlink

Todos los comandos van al **puerto 1**, **sin confirmación**, y con **whitelist estricta** (cualquier byte o comando desconocido se ignora con un log, jamás se ejecuta nada fuera de la lista).

| Comando | Payload (hex) | Efecto | Persistencia |
|---|---|---|---|
| Cambiar SF | `01 07` … `01 09` | SF 7, 8 o 9. Re-aplica el DR. Fuerza `setADR(false)`. | EEPROM |
| Cambiar intervalo | `02 00 01` … `02 05 A0` | `0x02` + `uint16` big-endian de minutos (1..1440). Ej: `02 00 1E` = 30 min. | EEPROM |
| Consultar RSSI | `03 00` | Reporta por puerto serie el RSSI del **último downlink** recibido. | No aplica |
| — | cualquier otro | Ignorado, con log `[DL] Comando 0x.. desconocido; ignorado`. | — |

### Ejemplos

```text
# Poner el nodo en SF9 (más alcance, más tiempo de aire):
01 09

# Poner el nodo en SF7 (más rápido):
01 07

# Cambiar intervalo a 30 minutos (0x001E = 30):
02 00 1E

# Cambiar intervalo a 1440 minutos (24 h) — máximo:
02 05 A0

# Consultar el RSSI del último downlink (aparece solo en el puerto serie):
03 00
```

Tras aplicar un comando el nodo actualiza `gCfg` y **lo guarda en EEPROM**, por lo que el ajuste sobrevive un power-cycle. El comando de intervalo afecta al **tiempo de deep sleep del siguiente ciclo** (el uplink actual ya está enviado cuando llega el downlink... salvo que el NS encole el downlink tras ese mismo uplink: en ese caso el nuevo intervalo se aplica al mismo `deepSleepMillis()` que sigue a la ventana RX — ver `docs/developer.md` §flujo).

### Mapeo SF ↔ Data Rate (AU915)

| SF | DR | Tiempo de aire (aprox. 5 B) | Nota |
|---|---|---|---|
| SF7 | DR5 | ~70 ms | Máximo alcance "rápido" |
| SF8 | DR4 | ~120 ms | — |
| SF9 | DR3 | ~220 ms | Máximo SF permitido por el firmware |

> **⚠️ Por qué SF ≤ 9**: AU915 sub-banda 2 impone (o puede imponer) restricciones de dwell time en ciertos canales. El firmware capa a SF9 para mantenerse dentro de la ventana permitida aun con dwell desactivado a nivel de radio (`setDwellTime(false)`), y por eso el whitelist solo acepta 7..9.

---

## 8. Formato de payload

### 8.1 Uplink — puerto 1, 5 bytes, sin confirmar

| Byte | Campo | Tipo | Endianidad | Valor |
|---|---|---|---|---|
| 0 | `version` | `uint8` | — | `0x01` (versión de payload OK) |
| 1 | `random` (MSB) | `uint32` | **Big-endian** (MSB primero) | Número aleatorio de 32 bits |
| 2 | `random` | `uint32` | Big-endian | — |
| 3 | `random` | `uint32` | Big-endian | — |
| 4 | `random` (LSB) | `uint32` | Big-endian | — |

Ejemplo: payload `01 12 AB CD EF`

| Campo | Cálculo | Valor |
|---|---|---|
| version | `bytes[0]` | `0x01` |
| random | `(0x12<<24) | (0xAB<<16) | (0xCD<<8) | 0xEF` | `313.322.991` (0x12ABCDEF) |

### 8.2 Downlink — puerto 1, 1–3 bytes, sin confirmar

Ver §7. Formato exacto:

| Longitud | Comando | Estructura |
|---|---|---|
| 2 B | `0x01` | `[cmd][SF]` — SF = 7..9 |
| 3 B | `0x02` | `[cmd][hi][lo]` — `interval = hi<<8 | lo`, 1..1440 min |
| 2 B | `0x03` | `[cmd][0x00]` — consulta RSSI (reporte local por serie) |

### 8.3 Decodificación (`decoder.js`)

El decoder (a pegar en el NS) devuelve para el uplink:

```json
{
  "data": {
    "version": 1,
    "random": 313322991
  }
}
```

Y para downlinks (vía `encodeDownlink`):

```json
{
  "bytes": [1, 9],
  "fPort": 1
}
```

Warnings soportados: puerto distinto de 1, longitud < 5, versión distinta de `0x01`, comando downlink desconocido.

---

## 9. Compilación y carga

### 9.1 Requisitos

```bash
# Core CubeCell (una sola vez)
arduino-cli config add board_manager.additional_urls https://resource.heltec.cn/download/package_CubeCell_index.json
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell

# RadioLib 7.7.1 (una sola vez)
arduino-cli lib install RadioLib@7.7.1
```

`credentials.h` debe existir en la carpeta del sketch (copiado de `credentials.example.h` y rellenado).

### 9.2 Compilar

```bash
arduino-cli compile --fqbn CubeCell:CubeCell:CubeCell-Board .
```

Salida esperada (resumen):

```text
Sketch uses 89364 bytes (68%) of program storage space. Maximum is 131072 bytes.
Global variables use ~1300 bytes (8%) of dynamic memory, leaving ~15 KB for local variables.
```

### 9.3 Grabar en la placa

```bash
# Detectar el puerto (la AB01 con CP2102 suele ser /dev/ttyUSB0)
arduino-cli board list

# Grabar
arduino-cli upload -p /dev/ttyUSB0 --fqbn CubeCell:CubeCell:CubeCell-Board .
```

Salida esperada:

```text
Uploading... 14.67s
Silicon ID 0x256a11b5 (ASR6502) — OK
```

### 9.4 Monitor serie (¡ojo con el quirk!)

`arduino-cli monitor` **termina inmediatamente** si no hay TTY interactivo (por ejemplo al lanzarlo desde un script o un agente). Workaround verificado:

```bash
timeout 30 sh -c 'sleep 60 | arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200'
```

Esto mantiene el monitor 30 s y captura la salida. Baudrate: **115200**.

---

## 10. Ejemplos de uso (salida del monitor serie)

### 10.1 Primer arranque con join exitoso

```text
[INIT] Cubecell-htcc-ab01-2 - HTCC-AB01 (RadioLib) - AU915 SB2
[INIT] DevEUI=70B3D57ED0051234 JoinEUI=24E124C0002ADD01
[INIT] PRNG seed=0x3F2A91C4
[CFG] EEPROM: sf=7 interval=1 min
[INIT] Radio OK
[INIT] Node OK
[INIT] scanGuard=200 ms
[JOIN] Iniciando OTAA (AU915 SB2)...
[JOIN] +++ JOINED +++ DevAddr=0x26011F3C
[INIT] Ciclo de 1 min (60000 ms). Listo.
[UPLINK] port=1 len=5 payload=01 12 AB CD EF (sf=7 max=242 B)
[UPLINK] OK (sin downlink)
[SLEEP] Durmiendo 60000 ms (1 min)...
```

### 10.2 Arranque con sesión restaurada (sin re-join) — tras un power-cycle

```text
[INIT] Cubecell-htcc-ab01-2 - HTCC-AB01 (RadioLib) - AU915 SB2
[INIT] DevEUI=70B3D57ED0051234 JoinEUI=24E124C0002ADD01
[INIT] PRNG seed=0x8123ABCD
[CFG] EEPROM: sf=7 interval=1 min
[INIT] Radio OK
[INIT] Node OK
[INIT] scanGuard=200 ms
[SESS] Bloque de sesion cargado; restaurando sin re-join...
[JOIN] Restaurando sesion (AU915 SB2)...
[JOIN] +++ SESION RESTAURADA +++ (sin re-join) DevAddr=0x26011F3C
[INIT] Ciclo de 1 min (60000 ms). Listo.
[UPLINK] port=1 len=5 payload=01 9C 44 21 7A (sf=7 max=242 B)
[UPLINK] OK (sin downlink)
[SLEEP] Durmiendo 60000 ms (1 min)...
```

### 10.3 Downlink de cambio de intervalo (`02 00 1E` = 30 min)

```text
[UPLINK] port=1 len=5 payload=01 55 AA 33 01 (sf=7 max=242 B)
[RX] 3 bytes: 02 00 1E (RSSI=-87 dBm)
[DL] Intervalo cambiado a 30 min
[SLEEP] Durmiendo 1800000 ms (30 min)...
```

### 10.4 Downlink de consulta RSSI (`03 00`)

```text
[UPLINK] port=1 len=5 payload=01 7E 12 99 04 (sf=9 max=170 B)
[RX] 2 bytes: 03 00 (RSSI=-92 dBm)
[DL] Consulta RSSI: ultimo downlink RSSI=-92 dBm
[SLEEP] Durmiendo 60000 ms (1 min)...
```

> El RSSI reportado es el del **downlink** (el más fresco en el momento de la ventana RX), no el del uplink. Es una métrica local de propagación útil para mover el nodo o la antena.

### 10.5 Join rechazado (estado actual hasta registrar en el UG56)

```text
[JOIN] Iniciando OTAA (AU915 SB2)...
[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)
[SLEEP] Durmiendo 10000 ms (0 min)...
[JOIN] Iniciando OTAA (AU915 SB2)...
[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)
...
```

Cada ~12 s (10 s de sleep + ~2 s de intento). En cuanto el device exista en el NS con DevEUI/JoinEUI/AppKey correctos, el siguiente intento será aceptado automáticamente (y los DevNonces ya persistidos evitan rechazos posteriores).

### 10.6 Re-join en caliente (sesión perdida)

```text
[REJOIN] Iniciando join OTAA (AU915 SB2)...
[REJOIN] +++ OK +++ (NEW_SESSION)
```

---

## 11. Arquitectura del firmware

```text
Cubecell-htcc-ab01-2/
├── Cubecell-htcc-ab01-2.ino   # Todo el firmware (584 líneas)
│   ├── Configuración y constantes
│   ├── Persistencia EEPROM
│   │   ├── 0x000: NodeConfig (magic, sf, intervalMin) → 4 B útiles en 4 B
│   │   └── 0x040: Bloque de sesión:
│   │        +0  magic (2 B)
│   │        +2  nonces  (RADIOLIB_LORAWAN_NONCES_BUF_SIZE  = 14 B)
│   │        +16 session (RADIOLIB_LORAWAN_SESSION_BUF_SIZE = 302 B)
│   │        total 382 B < 512 B (estático con #error de compilación)
│   ├── join/restore (beginOTAA → loadSession → activateOTAA)
│   ├── uplink (buildPayload → sendReceive → handleDownlink)
│   ├── downlink whitelist (0x01 SF | 0x02 intervalo | 0x03 RSSI)
│   └── deep sleep (radio.sleep + lowPowerHandler + TimerEvent)
├── credentials.example.h      # Plantilla (placeholders 0x00)
├── credentials.h              # Credenciales reales — NO versionado
├── decoder.js                 # Payload codec (decodeUplink/encodeDownlink + legacy v3)
└── docs/
    ├── datasheet.md           # Documentación de usuario
    ├── developer.md           # Documentación de desarrollador
    └── BITACORA.md            # Decisiones, problemas y lecciones
```

Puntos clave de diseño:

1. **Un solo archivo `.ino`** sin dependencias frágiles: RadioLib + core.
2. **EEPROM como almacén único**: config y sesión con bloques separados, magic bytes para validación, `#error` compile-time si el layout no cabe.
3. **PRNG sembrado por hardware**: `micros()`, `millis()` y dos lecturas de ADC.
4. **Errores traducidos a texto**: `stateDecode()` convierte los códigos de RadioLib (`-1116` → `NO_JOIN_ACCEPT`, etc.) para logs legibles.
5. **Whitelist de downlink**: nunca se ejecuta nada fuera de `0x01/0x02/0x03`, evitando ataques triviales vía downlink.
6. **Reset seguro ante fallos fatales**: `fatalErrorReset()` reintenta vía `NVIC_SystemReset()` y, como último recurso, entra en deep sleep para no quedarse en bucle de consumo.

---

## 12. Solución de problemas

### 12.1 `-1116 (NO_JOIN_ACCEPT)` — join rechazado (el más común)

| Causa | Verificación | Fix |
|---|---|---|
| Credenciales no registradas en el UG56 | El device no existe o tiene otro DevEUI | Registrarlo con el DevEUI/AppKey exactos de `credentials.h` |
| JoinEUI distinto al auto-asignado por el NS | El UG56 asigna p. ej. `24E124C0002ADD01` y el firmware usa otro | Copiar el JoinEUI del NS a `joinEui` en `credentials.h` y **re-flashear** |
| AppKey/NwkKey mal copiadas | Comparar hex con el NS (32 hex) | Corregir y re-flashear |
| Sub-banda del gateway ≠ 2 | Gateway en canales 0–7 (SB1) | Configurar SB2 (canales 8–15) en gateway y NS |
| Reloj/región del gateway incorrecta | Gateway no recibe el join | Verificar región AU915 en el UG56 |

> El nodo reintenta solo cada ~12 s: **no hace falta resetear** tras corregir el NS en el primer caso. Si cambias `credentials.h`, sí hay que re-flashear.

### 12.2 No hay join pero tampoco log de fallo

- Verificar que la antena está conectada y el gateway encendido.
- Comprobar en el UG56 → **Network Server → Devices → Last Seen**.
- Comprobar la sub-banda: es la causa nº 2 después de las credenciales.

### 12.3 El downlink no llega

- Los downlinks solo se entregan en las ventanas RX1/RX2 de Clase A: encolar el downlink **justo después** de un uplink (el NS lo hace solo si el device está registrado y "visto").
- Si el downlink se encola antes de que el nodo despierte, el UG56 (con cola habilitada) lo entregará en el siguiente uplink.
- Verificar el **puerto 1**: el firmware ignora downlinks en otros puertos (log de warning implícito).

### 12.4 `arduino-cli monitor` no muestra nada y termina al instante

Quirk conocido: sin TTY interactivo el monitor sale. Usar:

```bash
timeout 30 sh -c 'sleep 60 | arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200'
```

### 12.5 La placa no aparece en `arduino-cli board list`

- Otro driver/proceso puede estar ocupando el CP2102 (`brltty`, ModemManager). Probar `dmesg | tail` al conectar.
- Probar otro cable USB (los de solo-carga no sirven).
- Verificar permisos del puerto (`dialout`).

### 12.6 Compilación: error por RadioLib o core

```bash
arduino-cli core list          # debe mostrar CubeCell 1.4.0
arduino-cli lib list           # debe mostrar RadioLib 7.7.1
arduino-cli core install CubeCell:CubeCell
arduino-cli lib install RadioLib@7.7.1
```

---

## 13. Advertencias y precauciones

> ### ⚠️ Antena
> **Nunca** transmitir sin antena conectada. El SX1262 puede dañarse. Conectar la antena **antes** de energizar.

> ### ⚠️ Alimentación
> - Respetar la polaridad del conector externo (versión sin portapilas): `+` a positivo, `−` a GND. Invertirla puede destruir la placa.
> - Rango de alimentación del CubeCell: ~3,5–5,5 V (batería LiPo 3,7 V típica). No superar 5,5 V.
> - Al grabar por USB en placas con portapilas conviene retirar la pila; en esta revisión (sin holder) no aplica, pero **no** alimentes por externo y USB a la vez salvo fuente estable.

> ### ⚠️ Duty cycle y límites regionales
> - AU915 permite duty cycle relativamente laxo, pero el **dwell time** (400 ms máx.) inspira el cap SF ≤ 9 del firmware. No lo levantes sin evaluar el impacto regulatorio.
> - El intervalo mínimo es 1 min (≈1440 uplinks/día). Aumentarlo reduce el consumo y el impacto en el espectro.

> ### 🔑 Credenciales
> - `credentials.h` **jamás** debe subirse a un repositorio (ya está en `.gitignore`).
> - Los valores reales de DevEUI/JoinEUI/AppKey/NwkKey solo viven en el NS y en el nodo.
> - En este README y en `credentials.example.h` solo hay placeholders `0x00`.

> ### ⚙️ EEPROM
> - La EEPROM emulada del ASR6502 tiene ciclos de escritura finitos. El firmware ya hace *throttling* (cada 32 uplinks) precisamente para no desgastarla.
> - Si cambias el layout de EEPROM en una versión futura del firmware, **sube los magic bytes** para invalidar bloques viejos (ver `docs/developer.md`).

> ### 📡 Network Server
> - Cambiar el JoinEUI en el NS **requiere re-flashear** el nodo (RadioLib valida el JoinEUI del join-accept).
> - Tras un "rejoin forzado" en el NS (borrar sesión), el nodo re-joineará solo en el siguiente ciclo fallido.

---

## 14. Referencias

- **RadioLib 7.7.1** — https://github.com/jgromes/RadioLib (documentación de `LoRaWANNode`, `beginOTAA`, `activateOTAA`, `sendReceive`).
- **Heltec CubeCell** — https://resource.heltec.cn/download/package_CubeCell_index.json (core 1.4.0).
- **Milesight UG56** — manual oficial del gateway con Network Server embebido (registro de devices, codec).
- **TTN / The Things Stack** — https://www.thethingsnetwork.org (alternativa de NS).
- **ChirpStack v4** — https://www.chirpstack.io (alternativa de NS, formato `decodeUplink`).
- Documentación interna: `docs/datasheet.md` (usuario), `docs/developer.md` (código), `docs/BITACORA.md` (decisiones y lecciones).
