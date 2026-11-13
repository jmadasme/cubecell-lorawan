# Heltec CubeCell HTCC-AB02A — Nodo Sensor LoRaWAN Low-Power (v3.0)

Firmware **v3.0** para la tarjeta **Heltec CubeCell HTCC-AB02A** ("CubeCell 1/2AA Node", MCU **ASR6502** Cortex-M0+ @ 48 MHz con radio **SX1262** integrada) que actúa como un **nodo sensor de bajo consumo LoRaWAN Clase A**. Realiza **join OTAA** contra la red desplegada (gateway **Milesight UG56** con servidor de red embebido, región **AU915 subbanda 2**) y ejecuta un ciclo de operación de **10 minutos**: dormir en *deep sleep* real → despertar por RTC → capturar **6 entradas digitales + 2 entradas analógicas + batería** → enviar un **uplink no confirmado** de 7 bytes → volver a dormir. Incluye además **1 salida digital controlable por downlink**.

> **Stack**: este firmware usa **RadioLib 7.7.1** (`LoRaWANNode` + `SX1262`), **NO** el stack nativo `LoRaWan_APP.h`. El stack nativo fue descartado (v2.x) por un bug de temporizadores ~×100 que impedía el join OTAA en AU915 contra el UG56. La migración a RadioLib es la combinación que ya funcionó en el proyecto hermano *CubeCell-LoRaWAN-Node v1.3*.

> **Cambios en v3.0 respecto a v2.1** — v2.1 era un nodo de telemetría (ciclo 60 s, payload 6 B con contador+batería+temperatura, sin dormir entre uplinks). v3.0 es un **nodo sensor low-power**:
> - Ciclo de **10 minutos** con **deep sleep real** (`lowPowerHandler()` → `CySysPmDeepSleep` + `TimerEvent_t` RTC; radio SX1262 en `sleep()` ~0.6 µA).
> - Payload **7 bytes**: bits digitales + ADC2 + ADC3 + batería (sin contador ni temperatura).
> - **6 entradas digitales** con pull-up, **2 entradas analógicas** (0–2400 mV), **1 salida digital** comandada por downlink.
> - El workaround `setSleepFunction()` de v2.1 **ya no se usa** (era para esperas de radio, no para el sleep largo); solo se conserva `scanGuard = 200`.

---

## 🎯 Funcionalidades

| Funcionalidad | Detalle |
|---------------|---------|
| **Stack LoRaWAN** | RadioLib 7.7.1 (`LoRaWANNode` + `SX1262`) — no el stack nativo CubeCell |
| **Join OTAA** | AU915 subbanda 2 (canales 8–15), LoRaWAN 1.0.x, LoRaWAN 1.0.2/1.0.4 |
| **Ciclo de operación** | 10 min (600 000 ms): deep sleep → despertar → medir → uplink → dormir |
| **Bajo consumo** | Deep sleep del MCU (`CySysPmDeepSleep`) + radio en sleep (~0.6 µA) |
| **6 entradas digitales** | Pull-up interno; estado alto/bajo empaquetado en 1 byte de bits |
| **2 entradas analógicas** | ADC2 (P2_1) y ADC3 (P2_2), voltaje 0–2400 mV |
| **1 salida digital** | GPIO1 (P6_0), comandada por downlink (ON/OFF/TOGGLE) |
| **Batería** | Voltaje en mV vía `getBatteryVoltage()` del core (ADC 12-bit, divisor ×2, promedio ×50) |
| **Uplink no confirmado** | `isConfirmed=false`, puerto 1, payload 7 bytes big-endian |
| **Re-join automático** | `ensureJoined()` reintenta `activateOTAA()` con backoff de 10 s |
| **Diagnóstico serial** | Etiquetas `[INIT]` `[JOIN]` `[UPLINK]` `[GPIO]` `[ADC]` `[BATT]` `[RX]` `[SLEEP]` `[REJOIN]` |

---

## 📋 Especificaciones técnicas de la placa

| Parámetro | Valor |
|-----------|-------|
| **Placa** | Heltec CubeCell HTCC-AB02A ("CubeCell 1/2AA Node") |
| **Microcontrolador** | ASR6502 (ARM Cortex-M0+ @ 48 MHz, integra radio LoRa SX1262) |
| **Radio** | SX1262 (LoRa, 915 MHz) |
| **RAM** | 16 384 bytes (16 KB) |
| **Flash** | 131 072 bytes (128 KB) |
| **Voltaje de operación** | 3.3 V (USB 5 V o batería 1/2AA LiSOCl₂/LiPo) |
| **Rango del ADC (entradas analógicas)** | 0 – 2400 mV (referencia interna 2.4 V) |
| **Frecuencia** | 915 MHz — AU915 subbanda 2 |
| **Baud rate Serial** | 115200 |
| **Chip USB-Serial** | CP210x (puerto `/dev/ttyUSB0` en Linux) |
| **Core Arduino** | `CubeCell:CubeCell` 1.4.0 |
| **Librería RF** | RadioLib 7.7.1 |
| **arduino-cli** | 1.5.1 |
| **FQBN (alias local)** | `CubeCell:CubeCell:CubeCell_AB02A` |

---

## ⚙️ Principio de funcionamiento

El nodo opera con una **máquina de estados cíclica** cuyo núcleo es el ahorro de energía: la mayor parte del tiempo el MCU y la radio están **dormidos**, y solo despiertan brevemente para medir y transmitir.

### Ciclo de bajo consumo (10 minutos)

```
┌────────────────────────────────────────────────────────────────┐
│                        BOOT / SETUP                            │
│  Serial 115200 · banner [INIT] · config GPIO/ADC · radio.begin │
│  node.beginOTAA(credenciales) · scanGuard=200 · activateOTAA() │
└───────────────────────────────┬────────────────────────────────┘
                                ▼
                      ┌──────────────────┐     no activo
                      │  ¿Sesión OTAA?   │────────────────────────┐
                      │ node.isActivated │                        │
                      └───────┬──────────┘                        │
                              │ activo                            │
                              ▼                                   │
        ┌─────────────────────────────────────────┐               │
        │  1. Leer 6 entradas digitales (bits)    │               │
        │  2. Leer ADC2 (P2_1) en mV              │               │
        │  3. Leer ADC3 (P2_2) en mV              │               │
        │  4. Leer batería interna en mV          │               │
        │  5. Construir payload de 7 bytes        │               │
        │  6. sendReceive() no confirmado, puerto1│               │
        │     + capturar downlink (comanda GPIO1) │               │
        └────────────────────┬────────────────────┘               │
                             ▼                                    │
              ┌──────────────────────────────┐                    │
              │  DORMIR 10 min (deep sleep)  │◄───────────────────┘
              │  radio.sleep()               │   (re-join con      │
              │  lowPowerHandler() en bucle  │    backoff 10 s)    │
              │  hasta que expire el RTC     │                    │
              └────────────────┬─────────────┘                    │
                               │ IRQ del RTC (600 000 ms)         │
                               ▼                                  │
                        radio.standby()                           │
                        (volver arriba)                           │
```

### Detalle del mecanismo de bajo consumo

1. **Deep sleep del MCU** — La función `deepSleepMillis(ms)` usa el *timeServer* del core CubeCell:
   - `TimerInit(&sleepTimer, onSleepTimerExpired)` + `TimerSetValue(ms)` + `TimerStart()` → programa una alarma del **RTC** (32.768 kHz).
   - El bucle `while (!sleepExpired) lowPowerHandler();` ejecuta `CySysPmDeepSleep()` (deep sleep real del ASR6502/PSoC4) hasta que la IRQ del RTC dispara el callback y fija la bandera `volatile` `sleepExpired`.
   - **La SRAM se retiene** durante el sueño (no es *hibernate*), de modo que la **sesión LoRaWAN (claves, `devNonce`, `fCntUp/Down`) sobrevive intacta**: al despertar `node.isActivated()` sigue siendo `true` y **no se re-hace join**.
2. **Radio en sleep** — Antes de dormir se llama `radio.sleep()` (WARM START, RTC OFF, ~0.6 µA). Al despertar, `radio.standby()` devuelve el SX1262 a standby operativo.
3. **Timing correcto** — El camino RTC (`RtcGetTimerValue()`) devuelve **milisegundos reales** (verificado por desensamblado en la auditoría); el bug ~×100 era del stack LoRaMac nativo, **no** de este camino. Por tanto `TimerSetValue(600000)` equivale **exactamente a 10 minutos**.

### Consumo esperado

| Fase | Duración | Corriente típica |
|------|----------|------------------|
| Deep sleep (MCU + radio) | ~600 s | ~10–20 µA (dominado por quiescent del regulador + RTC) |
| Activa (medir + TX + RX1/RX2) | ~1–3 s | ~15–25 mA (pico de TX ~90 mA durante ~50 ms) |

Con una batería 1/2AA **ER14250 LiSOCl₂ (1200 mAh)** el consumo medio estimado es de unos **2 mAh/día** (~1.5–2 años; ver cálculo detallado en [`docs/datasheet.md`](docs/datasheet.md)).

---

## 🚀 Forma de operar (instrucciones de uso)

### Puesta en marcha

1. **Conectar la antena LoRa de 915 MHz** al conector U.FL/IPEX **antes** de energizar (operar sin antena daña la etapa RF).
2. **Registrar el dispositivo** en el gateway Milesight UG56 (LoRaWAN → Device) con las credenciales de la sección [Credenciales OTAA](#credenciales-otaa).
3. **Conectar la placa por USB** al ordenador (aparece `/dev/ttyUSB0` en Linux) o alimentarla con una batería 1/2AA.
4. **Compilar y cargar** el firmware (ver [Compilación y carga](#compilación-y-carga)).
5. **Abrir el monitor serial a 115200 baud** y observar el banner `[INIT]` y el `[JOIN]`.

### Operación normal

- Tras el join, el nodo **transmite automáticamente cada 10 minutos** y duerme en deep sleep el resto del tiempo. **El serial queda mudo entre uplinks** (el MCU está dormido) — esto es comportamiento esperado.
- Para verificar la recepción: interfaz web del UG56 → **LoRaWAN → Incoming Packets** (buscar `Unconfirmed Data Up` del DevEUI `70B3D56F27CEE98B`).
- **Comandar la salida GPIO1**: enviar un downlink en el puerto 1 desde el NS/UG56 (ver [Comandos de downlink](#comandos-de-downlink)). El downlink se entrega en la ventana RX1/RX2 que se abre tras cada uplink (clase A).
- El botón `USER_KEY` (P3_3) **no** se usa en esta versión; cada reset hace un join OTAA limpio (no hay sesión persistida en flash).

### Interpretación del diagnóstico serial

| Etiqueta | Significado |
|----------|-------------|
| `[INIT]` | Estado de arranque (GPIOs, radio, nodo, workaround) |
| `[JOIN]` | Progreso y resultado del join OTAA |
| `[GPIO]` | Byte de bits de las 6 entradas digitales (hex) |
| `[ADC]` | Voltaje leído de cada canal analógico (mV) |
| `[BATT]` | Voltaje de batería interna (mV) |
| `[UPLINK]` | Payload enviado (hex) + resultado |
| `[RX]` | Downlink recibido y comando ejecutado en GPIO1 |
| `[SLEEP]` | Entrada en deep sleep y duración |
| `[REJOIN]` | Re-join automático tras pérdida de sesión |

---

## 🔌 Tabla de pines (pinout)

### Pines del SX1262 (conexión fija en el SiP ASR6502 — **NO reasignables**)

| Pin Arduino | Pin ASR6502 | Señal SX1262 | Modo | Notas |
|-------------|-------------|--------------|------|-------|
| `35` | `P4_3` | NSS (CS) | OUTPUT | SPI Chip Select |
| `38` | `P4_6` | DIO1 | INPUT | IRQ de la radio (eventos TX/RX) |
| `47` | `P5_7` | RST | OUTPUT | Reset (activo bajo) |
| `39` | `P4_7` | BUSY | INPUT | Indicador de ocupado |
| — (interno) | `P4_0` | MOSI | SPI | Gestionado internamente por RadioLib |
| — (interno) | `P4_1` | MISO | SPI | Gestionado internamente por RadioLib |
| — (interno) | `P4_2` | SCK | SPI | Gestionado internamente por RadioLib |
| — (interno) | `P6_1` | ANT_SWITCH_POWER | OUTPUT | Control del RF switch/PA — **reservado** |

```cpp
Module mod(35, 38, 47, 39);  // NSS=35, DIO1=38, RST=47, BUSY=39
SX1262 radio(&mod);
```

### Pines de aplicación (v3.0)

| Pin Arduino | Pin ASR6502 | Alias en core | Modo | Componente conectado | Notas |
|-------------|-------------|---------------|------|----------------------|-------|
| `48` | `P6_0` | `GPIO1` / `MOSI1` | OUTPUT | **Salida digital controlable** (relé/actuador) | Comandada por downlink (0x01/0x00/0x02). Estado inicial: LOW |
| `28` | `P3_4` | `GPIO5` | INPUT_PULLUP | Entrada digital 1 | bit 0 del payload |
| `30` | `P3_6` | `GPIO6` | INPUT_PULLUP | Entrada digital 2 | bit 1 del payload |
| `31` | `P3_7` | `GPIO7` | INPUT_PULLUP | Entrada digital 3 | bit 2 del payload |
| `56` | `P7_0` | `GPIO8` / `SCL1` | INPUT_PULLUP | Entrada digital 4 | bit 3 del payload |
| `57` | `P7_1` | `GPIO9` / `SDA1` | INPUT_PULLUP | Entrada digital 5 | bit 4 del payload |
| `58` | `P7_2` | `GPIO10` | INPUT_PULLUP | Entrada digital 6 | bit 5 del payload |
| `17` | `P2_1` | `ADC2` | ANALOG | Entrada analógica 1 | 0–2400 mV (referencia interna 2.4 V) |
| `18` | `P2_2` | `ADC3` | ANALOG | Entrada analógica 2 | 0–2400 mV (referencia interna 2.4 V) |
| `16` | `P2_0` | `ADC1` / `ADC` | ANALOG (interno) | Divisor de batería (×2) | Usado por `getBatteryVoltage()` |

### Pines reservados / con función interna (no usar como GPIO)

| Pin Arduino | Pin ASR6502 | Alias | Motivo |
|-------------|-------------|-------|--------|
| `49` | `P6_1` | `GPIO2` / `MISO1` | **ANT_SWITCH_POWER** (RF switch/PA) — reservado |
| `27` | `P3_3` | `USER_KEY` / `VBAT_ADC_CTL` | `getBatteryVoltage()` lo reconfigura transitoriamente (`OUTPUT`→`LOW`→`INPUT`) |
| `26` | `P3_2` | `Vext` | Habilitación de alimentación externa (LOW = ON); `boardInitMcu()` lo deja en HIGH |
| `24` / `25` | `P3_0` / `P3_1` | `UART_RX` / `UART_TX` | Monitor serial USB (CP210x) |
| `6` | `P0_6` | `LED` / `GPIO13` | LED onboard (activo-bajo en la mayoría de Heltec) — no usado |

> **GPIO libres para futuras extensiones**: `P6_2` (GPIO3/PWM1), `P6_4` (GPIO4/PWM2), `P0_2` (GPIO11), `P0_3` (GPIO12), `P0_7` (GPIO14), y los I2C `P0_0` (SCL) / `P0_1` (SDA).

---

## 🧩 Diagrama de conexión

```
                       Heltec CubeCell HTCC-AB02A
                      ┌────────────────────────────────┐
                      │                                │
      Antena LoRa ────┤ U.FL/IPEX (SX1262, 915 MHz)    │
      (915 MHz)       │                                │
                      │  ASR6502 (Cortex-M0+ @ 48 MHz) │
                      │  ┌──────────────────────────┐  │
                      │  │ SX1262 (SPI interno)     │  │
                      │  │ NSS=P4_3(35) DIO1=P4_6(38)│ │
                      │  │ RST=P5_7(47) BUSY=P4_7(39)│ │
                      │  │ ANT_SW=P6_1 (reservado)  │  │
                      │  └──────────────────────────┘  │
                      │                                │
   ┌─ Salida ────────┤ P6_0 (GPIO1) ──► relé/actuador  │
   │  (downlink)     │                                │
   │                 │ P3_4 (GPIO5) ◄── entrada dig 1 │
   │                 │ P3_6 (GPIO6) ◄── entrada dig 2 │
   │                 │ P3_7 (GPIO7) ◄── entrada dig 3 │
   │  Entradas       │ P7_0 (GPIO8) ◄── entrada dig 4 │
   │  digitales      │ P7_1 (GPIO9) ◄── entrada dig 5 │
   │  (pull-up)      │ P7_2 (GPIO10)◄── entrada dig 6 │
   │                 │                                │
   │                 │ P2_1 (ADC2)  ◄── analógica 1   │
   │  Entradas       │ P2_2 (ADC3)  ◄── analógica 2   │
   │  analógicas     │   (0–2400 mV)                  │
   │                 │                                │
   │  Batería        │ P2_0 (ADC1) ◄── divisor ×2     │
   └─────────────────┤    └── LiPo/LiSOCl₂ 1/2AA      │
                     │                                │
   USB (5 V/datos) ──┤ conector (CP210x → UART P3_0/1)│
   → /dev/ttyUSB0    │                                │
                     └────────────────────────────────┘
```

**Detalle de conexiones (paso a paso):**

1. **Antena** — Conectar una antena LoRa de **915 MHz** (banda AU915) al conector **U.FL/IPEX**. No energizar sin antena.
2. **Alimentación** — Por **USB** (5 V) o con una **batería 1/2AA** (LiSOCl₂ ER14250 o LiPo) en el portapilas JST de la placa. La placa regula a 3.3 V internamente.
3. **Salida digital (GPIO1 / P6_0)** — Conectar el positivo de control del **relé/actuador** a `P6_0` y el retorno a GND. OJO: el pin entrega 3.3 V a corriente limitada (pocos mA); para cargas mayores usar un transistor/MOSFET o un módulo de relé con su propia alimentación. La salida arranca en **LOW**.
4. **Entradas digitales (GPIO5–GPIO10)** — Conectar cada sensor/contacto entre el pin y **GND** (lógica activa-baja por el pull-up interno): pin flotante o a 3.3 V = lectura **1** (alto), pin a GND = lectura **0** (bajo). No es necesario resistor externo (pull-up interno ya activado).
5. **Entradas analógicas (ADC2/ADC3)** — Conectar la señal a medir (0–2.4 V máx.) entre `P2_1`/`P2_2` y **GND**. Para medir tensiones superiores a 2.4 V, usar un **divisor resistivo externo** y escalar en el decoder. **No exceder 2.4 V** en estos pines.
6. **Monitor serial** — Conectar el cable USB al ordenador; el bridge CP210x expone el UART (`P3_0`/`P3_1`) como `/dev/ttyUSB0`.

> La medición de batería **no requiere cableado**: `getBatteryVoltage()` lee el divisor interno por `P2_0` (ADC1) y lo controla por `P3_3` (`VBAT_ADC_CTL`), todo interno a la placa.

---

## 📦 Formato del payload (byte a byte)

**Longitud: 7 bytes · Puerto: 1 · Orden: big-endian (MSB primero) · Uplink no confirmado**

| Byte(s) | Campo | Tipo | Descripción |
|---------|-------|------|-------------|
| 0 | `digitales` | `uint8` (máscara de bits) | bit0=GPIO5, bit1=GPIO6, bit2=GPIO7, bit3=GPIO8, bit4=GPIO9, bit5=GPIO10 (1 = alto). Bits 6–7 siempre 0 |
| 1–2 | `adc2_mv` | `uint16` BE | Voltaje de ADC2 (P2_1) en mV (0–2400) |
| 3–4 | `adc3_mv` | `uint16` BE | Voltaje de ADC3 (P2_2) en mV (0–2400) |
| 5–6 | `battery_mv` | `uint16` BE | Voltaje de batería interna en mV (`getBatteryVoltage()`) |

### Convenciones

- **`digitales`**: cada bit corresponde a una entrada. Por ejemplo `0x05` = `0b00000101` → GPIO5 y GPIO7 altos, resto bajos.
- **`adc2_mv` / `adc3_mv`**: `analogReadmV()` devuelve un `float` en mV sobre el rango 0–2400 mV del ASR650x (referencia interna 2.4 V). Valores `0x0000` = 0 V.
- **`battery_mv`**: rango típico 3000–4200 mV con LiPo/LiSOCl₂ (o ~VUSB alimentado por USB).

### Ejemplo decodificado

```
Payload hex:  05   04 B4   03 3E   0D 00
              │    │       │       └──────┘
              │    │       │          batería = 0x0D00 = 3328 mV
              │    │       └────────── ADC3 = 0x033E = 830 mV
              │    └────────────────── ADC2 = 0x04B4 = 1204 mV
              └─────────────────────── digitales = 0x05 (GPIO5 y GPIO7 altos)
```

Otro ejemplo capturado en campo (todas las entradas altas, ADC en 0, batería 3328 mV):

```
3F 00 00 00 00 0D 00  →  digitales=0x3F (6 altos), ADC2=0, ADC3=0, batería=3328 mV
```

> Payload de 7 bytes, muy por debajo de los límites AU915 (`MaxPayloadOfDatarateAU915[]`: DR0–DR2 = 51 B, DR3 = 115 B).

---

## 📡 Comandos de downlink (puerto 1)

El **primer byte** del downlink recibido en el **puerto 1** comanda la salida `GPIO1` (P6_0). Los MAC commands (puerto 0) los procesa RadioLib internamente y **no** llegan a la aplicación.

| Byte | Comando | Efecto en GPIO1 (P6_0) |
|------|---------|------------------------|
| `0x01` | ON | `digitalWrite(GPIO1, HIGH)` — enciende el relé/actuador |
| `0x00` | OFF | `digitalWrite(GPIO1, LOW)` — apaga el relé/actuador |
| `0x02` | TOGGLE | Invierte el estado actual (`!digitalRead(GPIO1)`) |
| *(otro)* | — | Comando desconocido, ignorado y registrado `[RX] comando desconocido` |

> El downlink se entrega **solo tras un uplink** (clase A, ventanas RX1/RX2). Para comandar la salida desde el UG56, configurar un downlink en el puerto 1 con el byte correspondiente; se ejecutará en el siguiente uplink del nodo.

---

## 🔐 Credenciales OTAA

| Parámetro | Valor (hex, MSB-first) |
|-----------|------------------------|
| **DevEUI** | `70B3D56F27CEE98B` |
| **JoinEUI / AppEUI** | `24E124C0002ADD01` |
| **AppKey** | `89F5A1A24D63475791F8BF33EA031486` |
| **NwkKey** | Igual a AppKey (OTAA 1.0.x) |
| **Región** | AU915, subbanda 2 |

> En RadioLib las credenciales se pasan como enteros big-endian (`uint64_t joinEui = 0x24E124C0002ADD01`), a diferencia del stack nativo CubeCell que esperaba arrays little-endian. Esta diferencia de endianidad es una causa típica de `join failed` si se copian credenciales del formato antiguo.

---

## 💻 Ejemplos de uso (salida real del monitor serial)

### Arranque + join exitoso (115200 baud)

```
[INIT] HTCC-AB02A nodo sensor v3.0 (RadioLib) — AU915 SB2
[INIT] DevEUI=70B3D56F27CEE98B JoinEUI=24E124C0002ADD01
[INIT] GPIOs: out=P6_0, in=P3_4/P3_6/P3_7/P7_0/P7_1/P7_2, adc=P2_1/P2_2
[INIT] Radio OK
[INIT] Node OK
[INIT] scanGuard=200 ms
[JOIN] Iniciando OTAA (AU915 SB2)...
[JOIN] +++ JOINED +++
[INIT] Ciclo de 600000 ms (10 min). Listo.
```

### Primer ciclo: medición + uplink + deep sleep

```
[GPIO] bits=0x05
[ADC] pin=17 -> 1204 mV
[ADC] pin=18 -> 830 mV
[BATT] 3328 mV
[UPLINK] port=1 payload=0504B4033E0D00 (7 B)
[UPLINK] OK (sin downlink)
[SLEEP] Durmiendo 600000 ms...
```

> Tras `[SLEEP]`, el serial queda **mudo ~10 minutos** (MCU en deep sleep). El siguiente bloque de log aparece al despertar.

### Uplink con downlink recibido (ON del GPIO1)

```
[GPIO] bits=0x3F
[ADC] pin=17 -> 0 mV
[ADC] pin=18 -> 0 mV
[BATT] 3311 mV
[UPLINK] port=1 payload=3F000000000D00 (7 B)
[RX] 1 bytes: 01
[RX] GPIO1 -> ON
[SLEEP] Durmiendo 600000 ms...
```

### Comando TOGGLE y comando desconocido

```
[RX] 1 bytes: 02
[RX] GPIO1 -> TOGGLE
...
[RX] 1 bytes: FF
[RX] comando desconocido: 0xFF
```

### Re-join automático tras pérdida de sesión

```
[REJOIN] Intentando re-join...
[REJOIN] +++ EXITOSO +++
```

### Fallo de join (credenciales/endianidad/región incorrectas)

```
[JOIN] Iniciando OTAA (AU915 SB2)...
[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)
```

---

## 🛠️ Compilación y carga (arduino-cli)

### 0. Prerrequisitos

1. **arduino-cli 1.5.1** instalado.
2. **Core CubeCell** instalado (`CubeCell:CubeCell` 1.4.0).
3. **RadioLib 7.7.1** instalada (Library Manager o en `~/Arduino/libraries/RadioLib`).
4. **Alias de placa `CubeCell_AB02A`** creado (ver §3 abajo) — prerrequisito ya desplegado.

### 1. Instalar arduino-cli (si no está instalado)

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

### 2. Instalar el core CubeCell y RadioLib

```bash
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell

# RadioLib (Library Manager)
arduino-cli lib install RadioLib
```

### 3. ¿Por qué el alias `CubeCell_AB02A`?

El FQBN oficial de la placa es `CubeCell:CubeCell:CubeCell-1/2AA`, que contiene el carácter `/`. El parser de FQBN de `arduino-cli 1.5.1` **rechaza** ese carácter:

```
Error: Invalid FQBN: fqbn's field CubeCell-1/2AA contains an invalid character
```

Para compilar con arduino-cli se creó un **alias local sin `/`** en:

```
/home/jma/.arduino15/packages/CubeCell/hardware/CubeCell/1.4.0/boards.local.txt
```

con nombre `CubeCell_AB02A` → mismo `build.board=CubeCell_HalfAA`, mismo variant y menús que `CubeCell-1/2AA`. Su contenido íntegro está en `PLAN.md` §12.4. **Este archivo es un prerrequisito de build ya desplegado**: si se actualiza/reinstala el core, hay que recrearlo.

> Con el Arduino IDE 2.x esta restricción no existe (acepta el `/`) y se puede usar el FQBN oficial.

### 4. Compilar

```bash
PROJ=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/HTCC_AB02A_LoRaWAN
FQBN="CubeCell:CubeCell:CubeCell_AB02A"

arduino-cli compile --fqbn "$FQBN" "$PROJ"
```

Salida esperada (uso de Flash estimado ~45–55 %):

```
Sketch uses 61000 bytes (46%) of program storage space. Maximum is 131072 bytes.
```

### 5. Transferir a la placa

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn "$FQBN" "$PROJ"
```

### 6. Monitor serial

```bash
arduino-cli monitor -p /dev/ttyUSB0 --config baudrate=115200
```

### Variables resumidas

| Variable | Valor |
|----------|-------|
| `PROJ` | ruta absoluta del proyecto |
| `FQBN` | `CubeCell:CubeCell:CubeCell_AB02A` (alias local) |
| `PORT` | `/dev/ttyUSB0` |

> **Nota sobre menús del FQBN**: con RadioLib **no** se usan los menús `LORAWAN_REGION/CLASS/...` del core (esos definen flags `-D` para el stack nativo, que ya no se compila). Basta el FQBN base del alias; la región y subbanda se fijan en el sketch (`Region = AU915; subBand = 2`).

---

## 🧪 Calibración

### Entradas analógicas (ADC 0–2400 mV)

- El SAR-ADC del ASR650x usa una **referencia interna de 2.4 V**; `analogReadmV(pin)` ya convierte la lectura cruda (12-bit, 0–4095) a mV en el rango **0–2400 mV**. **No hay offset ni ganancia configurables por software** en el sketch.
- **Verificación**: aplicar una tensión conocida (fuente de laboratorio o un divisor exacto desde 3.3 V) a `P2_1`/`P2_2` y comprobar que `[ADC] pin=17 -> <mV>` y `[ADC] pin=18 -> <mV>` coinciden con el multímetro de referencia (dentro de la tolerancia del ADC, típicamente ±1–2 %).
- **Medir > 2.4 V**: usar un divisor resistivo externo y multiplicar en el decoder. Ejemplo: divisor 1:2 (10 kΩ + 10 kΩ) → `voltaje_real = adc_mv * 2`. Aplicar la escala en la plataforma, no en el firmware.
- **`0x0000`** en `adc2_mv`/`adc3_mv` = 0 V (pin a GND o sin señal).

### Voltaje de batería

- `getBatteryVoltage()` del core CubeCell promedia **50 lecturas** de `analogReadmV(ADC1=P2_0)` sobre un **divisor ×2**. No hay offset configurable.
- **Verificación**: con LiPo cargada (≈4.2 V) el serial debe reportar ≈4200 mV; con batería baja ≈3600 mV. Alimentado por **USB**, el valor puede corresponder a VUSB (~5 V regulada) en lugar de la batería.
- Si difiere de un multímetro de referencia, corregir en el decoder (aplicación): el factor ×2 ya viene aplicado por el core; añadir solo un offset de calibración si es necesario.

### Intervalo de ciclo (10 min)

- El intervalo viene de `SLEEP_INTERVAL_MS` (600 000). El camino RTC está en milisegundos reales (sin el bug ~×100), por lo que el ciclo es de **~10 min exactos**.
- **Verificar**: marcar timestamps del serial; la diferencia entre `[UPLINK]` consecutivos debe ser ≈600 s.

---

## ⚠️ Advertencias y precauciones

1. **Antena obligatoria** — Nunca energizar la placa sin la antena LoRa de 915 MHz conectada: la etapa RF puede dañarse.
2. **Voltaje de alimentación** — 3.3 V (regulado) / 5 V por USB / LiPo-LiSOCl₂ 3.0–4.2 V. No superar los límites del regulador.
3. **Polaridad de la batería** — Respetar la polaridad del portapilas/conector JST; invertir la polaridad daña la placa.
4. **Entradas analógicas ≤ 2.4 V** — `P2_1`/`P2_2` no deben recibir más de 2400 mV (referencia interna). Tensiones mayores requieren divisor externo.
5. **Pines reservados** — **`P6_1` (GPIO2)** = ANT_SWITCH_POWER (RF switch), **no usar**. **`P3_3`** = `VBAT_ADC_CTL` (se reconfigura al leer batería), **no usar** como GPIO permanente. **`P3_0`/`P3_1`** = UART del monitor serial, **no reasignar**. **`P2_0`** = ADC de batería, **no usar** para otra señal.
6. **Endianidad de credenciales** — RadioLib espera enteros big-endian (`uint64_t`). Copiar arrays little-endian del stack nativo provoca `NO_JOIN_ACCEPT`.
7. **Región/subbanda** — Asegurar que el UG56 está en **AU915 subbanda 2** (canales 8–15) y que la subbanda del sketch (`subBand = 2`) coincide.
8. **Salida GPIO1** — La salida entrega 3.3 V a corriente limitada (pocos mA). Para relés/cargas, usar transistor, MOSFET o módulo de relé con alimentación propia y diodo de protección (flyback).
9. **Sin watchdog** — No hay watchdog habilitado; ante un cuelgue, el reset es manual (power-cycle). (Ver observaciones de la auditoría en `AUDIT_REPORT.md`.)
10. **Sesión no persistida** — No se usa `beginPersistentSession()`: cada reset hace un join OTAA completo. Es intencional para evitar sesiones corruptas.
11. **Serial mudo entre uplinks** — Es normal: el MCU está en deep sleep ~10 min. Para depurar de forma interactiva, reducir temporalmente `SLEEP_INTERVAL_MS`.

---

## 📁 Estructura del proyecto

```
HTCC_AB02A_LoRaWAN/
├── HTCC_AB02A_LoRaWAN.ino   # Sketch principal (v3.0, RadioLib + deep sleep)
├── README.md                # Este documento
├── decoder.js               # Payload decoder/encoder para ChirpStack v4 (NS embebido Milesight)
├── docs/
│   ├── datasheet.md         # Documentación de usuario (payload, decoder JS, consumo, troubleshooting)
│   └── developer.md         # Guía de desarrollo (arquitectura, deep sleep, workarounds, extensión)
├── PLAN.md                  # Plan de implementación (historial v1.0 → v3.0)
└── AUDIT_REPORT.md          # Informe de auditoría estática (APROBADO)
```

---

## 🔗 Documentación adicional

- **Documentación de usuario** (especificaciones, decoder JavaScript para UG56, estimación de consumo/duración de batería, solución de problemas): [`docs/datasheet.md`](docs/datasheet.md)
- **Payload decoder/encoder** (ChirpStack v4, listo para pegar en el Device Profile del NS embebido del gateway Milesight): [`decoder.js`](decoder.js)
- **Guía de desarrollo** (arquitectura RadioLib, deep sleep con `lowPowerHandler` + RTC, workarounds, RAM 16 KB, extensión): [`docs/developer.md`](docs/developer.md)
- **Plan de implementación** (decisiones v1.0 → v3.0, migración a RadioLib, correcciones de campo): `PLAN.md`
- **Informe de auditoría**: `AUDIT_REPORT.md`
