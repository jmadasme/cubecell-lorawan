# Guía de Desarrollo — HTCC-AB02A Nodo Sensor LoRaWAN Low-Power (v3.0, RadioLib)

Documentación técnica para desarrolladores que deseen entender, modificar o extender el firmware del nodo sensor low-power basado en Heltec CubeCell HTCC-AB02A. Esta guía describe la versión **v3.0**, que combina **RadioLib 7.7.1** (MAC LoRaWAN) con el **low-power nativo del core CubeCell** (`lowPowerHandler()` + `TimerEvent_t` RTC) para lograr un ciclo de 10 minutos con deep sleep real.

---

## Tabla de Contenidos

- [Contexto: por qué RadioLib y no el stack nativo](#contexto-por-qué-radiolib-y-no-el-stack-nativo)
- [Arquitectura del sketch](#arquitectura-del-sketch)
- [Máquina de estados y flujo de ejecución](#máquina-de-estados-y-flujo-de-ejecución)
- [Explicación del código fuente](#explicación-del-código-fuente)
- [Deep sleep: lowPowerHandler + TimerEvent RTC](#deep-sleep-lowpowerhandler--timerevent-rtc)
- [API de funciones](#api-de-funciones)
- [Workarounds de timers ASR6502](#workarounds-de-timers-asr6502)
- [Limitaciones de memoria (RAM 16 KB)](#limitaciones-de-memoria-ram-16-kb)
- [Guía de modificación y extensión](#guía-de-modificación-y-extensión)
- [Cómo agregar nuevos sensores/actuadores](#cómo-agregar-nuevos-sensoresactuadores)
- [Depuración y logging](#depuración-y-logging)
- [Pruebas de validación](#pruebas-de-validación)

---

## Contexto: por qué RadioLib y no el stack nativo

El stack nativo del core CubeCell (`LoRaWan_APP.h` / LoRaMac-node de Heltec) **falla el join OTAA en AU915** contra el gateway Milesight UG56 por dos causas combinadas:

1. **Bug de temporizadores ~×100** — el RTC/timers del ASR6502 escalan el tiempo ~100× en el stack nativo; el re-join "de 30 s" real tardaba ~3000 s, por lo que el join nunca progresaba.
2. **Máscara de canales del stack nativo** — no garantizaba el join en la subbanda 2 del UG56 (el gateway no veía ningún paquete).

**Evidencia**: el proyecto hermano *CubeCell-LoRaWAN-Node v1.3* (misma familia ASR6502, misma red UG56 AU915 SB2) **funcionó** con **RadioLib 7.7.1**. RadioLib usa `millis()` (RTC WCO 32.768 kHz) para su timing interno, inmune al bug de escalado, y permite fijar la subbanda AU915 explícitamente (`subBand = 2`).

**Consecuencias de la migración**:

| Aspecto | Stack nativo (v1.0) | RadioLib (v2.1 → v3.0) |
|---------|---------------------|-------------------------|
| Driver radio | LoRaMac-node (CubeCellLib.a) | `SX1262` (RadioLib) |
| Join | `LoRaWAN.join()` asíncrono + callbacks | `activateOTAA()` síncrono |
| Credenciales | arrays little-endian | enteros `uint64_t` big-endian |
| Región | `LORAMAC_REGION_AU915` vía `-D` | `Region = AU915; subBand = 2` |
| Sleep largo | `LoRaWAN.sleep()` | `lowPowerHandler()` + `TimerEvent_t` (nativo) |
| Radio en sleep | — | `radio.sleep()` / `radio.standby()` |

> **Importante (v3.0)**: el `setSleepFunction()` que se usaba en v2.1 era una función de sleep de **esperas de radio** (polling `millis()`), no un mecanismo de bajo consumo. En v3.0 **ya no se usa**: para el sleep largo de 10 min se emplea directamente el patrón nativo de deep sleep (ver sección [Deep sleep](#deep-sleep-lowpowerhandler--timerevent-rtc)). Solo se conserva `scanGuard = 200`.

---

## Arquitectura del sketch

El sketch es **single-file** (`HTCC_AB02A_LoRaWAN.ino`) con un patrón `setup()` + `loop()` simple, donde `loop()` ejecuta **una iteración del ciclo** (medir → transmitir → dormir) y el sleep se hace de forma **bloqueante** dentro de `deepSleepMillis()`.

```
setup()
  ├── Serial.begin(115200) + delay(3000)
  ├── banner [INIT] + credenciales (DevEUI/JoinEUI)
  ├── pinMode: OUTPUT_PIN=OUTPUT(LOW), 6×INPUT_PULLUP, 2×ANALOG
  ├── radio.begin()                 // SX1262
  ├── node.beginOTAA(joinEui, devEui, nwkKey, appKey)
  ├── node.scanGuard = 200          // workaround timers
  └── node.activateOTAA()           // join OTAA inicial → [JOIN]

loop()
  ├── if !node.isActivated() → ensureJoined() → si falla deepSleepMillis(10000) y return
  ├── sendSensorReport()            // lee + construye payload + sendReceive
  └── deepSleepMillis(600000)       // deep sleep real 10 min (bloqueante)
```

**Objetos globales (RadioLib)**:

| Objeto | Tipo | Rol |
|--------|------|-----|
| `mod` | `Module` | Pines físicos del SX1262 (NSS/DIO1/RST/BUSY) |
| `radio` | `SX1262` | Driver de la radio |
| `node` | `LoRaWANNode` | MAC LoRaWAN (join, sendReceive, sesión) |

**Constantes y buffers globales**:

| Variable | Tipo / valor | Rol |
|----------|--------------|-----|
| `OUTPUT_PIN` | `GPIO1` (P6_0) | Salida digital controlable |
| `DIG_PINS[]` | `{GPIO5..GPIO10}` | 6 entradas digitales |
| `ADC2_PIN` / `ADC3_PIN` | `ADC2` / `ADC3` | Entradas analógicas (P2_1/P2_2) |
| `SLEEP_INTERVAL_MS` | `600000UL` | Ciclo de 10 min |
| `downlinkData[]` | `RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE` (242 B) | Buffer de downlink |
| `sleepTimer` | `TimerEvent_t` | Temporizador RTC del deep sleep |
| `sleepExpired` | `static volatile bool` | Bandera compartida ISR↔loop |

---

## Máquina de estados y flujo de ejecución

```
                ┌──────────────────────────────┐
                │            BOOT              │
                └──────────────┬───────────────┘
                               ▼
                   ┌───────────────────────┐
                   │  INIT (GPIO + radio)  │
                   └───────────┬───────────┘
                               ▼
                   ┌───────────────────────┐  fallo (backoff 10 s)
              ┌────┤      JOIN OTAA        ├────────────┐
              │    └───────────┬───────────┘            │
              │                │ OK                     │
              │                ▼                        │
              │    ┌───────────────────────┐            │
              │    │   JOINED (activo)     │            │
              │    └───────────┬───────────┘            │
              │                │ cada 10 min            │
              │                ▼                        │
              │    ┌───────────────────────┐            │
              │    │  MEDIR + UPLINK 7 B   │            │
              │    │  (+ capturar downlink)│            │
              │    └───────────┬───────────┘            │
              │                │                        │
              │                ▼                        │
              │    ┌───────────────────────┐            │
              │    │  DEEP SLEEP 600 000 ms│            │
              │    │  radio.sleep() +      │            │
              │    │  lowPowerHandler()    │            │
              │    └───────────┬───────────┘            │
              │                │ IRQ RTC despierta      │
              │                │ radio.standby()        │
              │                ▼                        │
              │         (volver a MEDIR)                │
              └────────────────────────────────────────┘
```

**Flujo detallado de `loop()`**:

1. Si `!node.isActivated()` → `ensureJoined()` reintenta `activateOTAA()`. Si falla, `deepSleepMillis(10000)` (backoff de 10 s) y `return`.
2. `sendSensorReport()`:
   - `readDigitalBits()` → byte de bits de las 6 entradas.
   - `readAnalogMv(ADC2_PIN)` y `readAnalogMv(ADC3_PIN)` → mV.
   - `readBatteryMv()` → `getBatteryVoltage()`.
   - Construye `payload[7]` big-endian.
   - `node.sendReceive(payload, 7, 1, downlinkData, &downlinkLen, false, NULL, NULL)`.
   - Interpreta el retorno y, si hay downlink, `handleDownlink()`.
3. `deepSleepMillis(SLEEP_INTERVAL_MS)` → duerme 10 min y no retorna hasta despertar.

---

## Explicación del código fuente

### Cabecera y objetos

```cpp
#include <RadioLib.h>
#include "Arduino.h"

Module mod(35, 38, 47, 39);   // NSS, DIO1, RST, BUSY
SX1262 radio(&mod);
```

- `Module mod(35, 38, 47, 39)` — argumentos en orden `NSS`, `DIO1`, `RST`, `BUSY`, correspondientes a los pines internos del SiP (`P4_3`, `P4_6`, `P5_7`, `P4_7`). MOSI/MISO/SCK los gestiona RadioLib por el SPI interno (`P4_0`/`P4_1`/`P4_2`).
- El RF switch se configura con `setDio2AsRfSwitch(true)` (comportamiento por defecto de `SX126x::begin()`).

### Credenciales y región

```cpp
uint64_t joinEui = 0x24E124C0002ADD01;   // JoinEUI/AppEUI (big-endian)
uint64_t devEui  = 0x70B3D56F27CEE98B;   // DevEUI
uint8_t appKey[] = { 0x89, 0xF5, ..., 0x86 };  // 16 bytes MSB-first
uint8_t nwkKey[] = { 0x89, 0xF5, ..., 0x86 };  // = appKey (OTAA 1.0.x)

const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);
```

- RadioLib espera `joinEui`/`devEui` como **`uint64_t` big-endian** (MSB-first), igual que el valor legible.
- `appKey`/`nwkKey` son arrays de 16 bytes en orden natural (MSB-first), a diferencia del stack nativo (little-endian).
- `LoRaWANNode node(&radio, &Region, subBand)` configura **AU915** y **subbanda 2**.

### Configuración de GPIOs

```cpp
const uint8_t OUTPUT_PIN = GPIO1;   // P6_0 — salida controlable
const uint8_t DIG_PINS[] = { GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10 };
const uint8_t ADC2_PIN = ADC2;      // P2_1
const uint8_t ADC3_PIN = ADC3;      // P2_2

#define SLEEP_INTERVAL_MS 600000UL  // 10 min
```

- En `setup()`: `pinMode(OUTPUT_PIN, OUTPUT); digitalWrite(OUTPUT_PIN, LOW);`, `pinMode(DIG_PINS[i], INPUT_PULLUP)` (×6) y `pinMode(ADCn, ANALOG)` (×2).
- El `INPUT_PULLUP` del core se implementa como `pinMode(pin, OUTPUT_PULLUP) + digitalWrite(pin, HIGH)` (pull-up resistivo), válido en todos los GPIO usados.

### Diagnóstico: `debug` y `stateDecode`

```cpp
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

static const char* stateDecode(int16_t s) {
    switch (s) {
        case RADIOLIB_ERR_NONE:              return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:    return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_PACKET_TOO_LONG:   return "PACKET_TOO_LONG";
        case RADIOLIB_ERR_RX_TIMEOUT:        return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:      return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED:return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:    return "NO_JOIN_ACCEPT";
        case RADIOLIB_LORAWAN_NEW_SESSION:   return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
        default:                             return "UNKNOWN";
    }
}
```

- `debug()` usa `Serial.printf` (compatible con newlib-nano del core CubeCell).
- Los códigos negativos son errores; `NEW_SESSION`/`SESSION_RESTORED` son **éxito** del join (valores negativos pero esperados).

### Lecturas: `readBatteryMv`, `readDigitalBits`, `readAnalogMv`

```cpp
static uint16_t readBatteryMv(void) {
    uint16_t mv = getBatteryVoltage();   // mV (core CubeCell)
    debug("[BATT] %u mV\r\n", mv);
    return mv;
}

static uint8_t readDigitalBits(void) {
    uint8_t bits = 0;
    for (uint8_t i = 0; i < DIG_COUNT; i++) {
        if (digitalRead(DIG_PINS[i]) == HIGH) bits |= (1 << i);
    }
    debug("[GPIO] bits=0x%02X\r\n", bits);
    return bits;
}

static uint16_t readAnalogMv(uint8_t pin) {
    float mv = analogReadmV(pin);        // rango 0-2400 mV (ASR650x)
    if (mv < 0) mv = 0;
    uint16_t v = (uint16_t)mv;
    debug("[ADC] pin=%d -> %u mV\r\n", pin, v);
    return v;
}
```

- `getBatteryVoltage()` (core CubeCell): promedia 50 lecturas de `analogReadmV(ADC1=P2_0)` sobre divisor ×2, devolviendo mV. Reconfigura `P3_3` (`VBAT_ADC_CTL`) transitoriamente.
- `readDigitalBits()` empaqueta las 6 entradas en bits (bit0=GPIO5 … bit5=GPIO10), con la convención `1 = HIGH`.
- `readAnalogMv()` usa `analogReadmV()` (función del core, devuelve `float` en mV) y lo trunca a `uint16_t`. El ADC del ASR650x tiene referencia interna de 2.4 V → rango 0–2400 mV. Los pines `ADC2`/`ADC3` (P2_1/P2_2) son canales directos del SAR-ADC, sin necesidad de `VBAT_ADC_CTL` ni `Vext`.

### Envío: `sendSensorReport`

```cpp
static int sendSensorReport(void) {
    uint8_t  dg  = readDigitalBits();
    uint16_t ad2 = readAnalogMv(ADC2_PIN);
    uint16_t ad3 = readAnalogMv(ADC3_PIN);
    uint16_t bat = readBatteryMv();

    uint8_t payload[7];
    payload[0] = dg;
    payload[1] = (uint8_t)(ad2 >> 8);
    payload[2] = (uint8_t)(ad2 & 0xFF);
    payload[3] = (uint8_t)(ad3 >> 8);
    payload[4] = (uint8_t)(ad3 & 0xFF);
    payload[5] = (uint8_t)(bat >> 8);
    payload[6] = (uint8_t)(bat & 0xFF);

    debug("[UPLINK] port=1 payload=");
    for (uint8_t i = 0; i < 7; i++) debug("%02X", payload[i]);
    debug(" (7 B)\r\n");

    downlinkLen = sizeof(downlinkData);
    int state = node.sendReceive(payload, sizeof(payload), 1,
                                 downlinkData, &downlinkLen,
                                 false, NULL, NULL);

    if (state == RADIOLIB_ERR_NONE) {
        debug("[UPLINK] OK (sin downlink)\r\n");
    } else if (state > 0) {
        debug("[RX] %u bytes: ", (unsigned)downlinkLen);
        for (size_t i = 0; i < downlinkLen; i++) debug("%02X", downlinkData[i]);
        debug("\r\n");
        handleDownlink(downlinkData, downlinkLen);
    } else {
        debug("[UPLINK] ERROR %d (%s)\r\n", state, stateDecode(state));
    }
    return state;
}
```

- Construye el payload de 7 bytes **big-endian** por campos de 16 bits.
- `node.sendReceive(dataUp, lenUp, fPort, dataDown, &lenDown, isConfirmed, eventUp, eventDown)`:
  - `fPort = 1`, `isConfirmed = false` (no confirmado), sin callbacks de eventos.
  - Retorna `RADIOLIB_ERR_NONE` (0) si el uplink se envió sin downlink; **valor positivo** = longitud del downlink recibido; **valor negativo** = error.
- `handleDownlink()` solo se invoca cuando hay downlink (estado > 0).

### Downlink: `handleDownlink`

```cpp
static void handleDownlink(const uint8_t* data, size_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];
    switch (cmd) {
        case 0x01: digitalWrite(OUTPUT_PIN, HIGH);                 debug("[RX] GPIO1 -> ON\r\n"); break;
        case 0x00: digitalWrite(OUTPUT_PIN, LOW);                  debug("[RX] GPIO1 -> OFF\r\n"); break;
        case 0x02: digitalWrite(OUTPUT_PIN, !digitalRead(OUTPUT_PIN)); debug("[RX] GPIO1 -> TOGGLE\r\n"); break;
        default:   debug("[RX] comando desconocido: 0x%02X\r\n", cmd); break;
    }
}
```

- Solo actúa sobre el **primer byte** del downlink. Los MAC commands (puerto 0) los procesa RadioLib y **no** llegan aquí.
- No expone comandos peligrosos (no hay re-join forzado, reset ni escritura de credenciales).

### Re-join: `ensureJoined`

```cpp
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    debug("[REJOIN] Intentando re-join...\r\n");
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[REJOIN] +++ EXITOSO +++\r\n");
        return true;
    }
    debug("[REJOIN] Fallo: %d (%s)\r\n", state, stateDecode(state));
    return false;
}
```

- `isActivated()` indica si hay sesión válida (sobrevive al deep sleep porque la SRAM se retiene).
- `activateOTAA()` reintenta el join; compara contra `NEW_SESSION` (-1118) y `SESSION_RESTORED` (-1117) como éxito.

### `setup()` y `loop()`

```cpp
void setup() {
    Serial.begin(115200);
    delay(3000);
    // banner + credenciales + config GPIO + radio.begin() + node.beginOTAA()
    // node.scanGuard = 200
    // node.activateOTAA() → [JOIN]
    Serial.flush();
}

void loop() {
    if (!node.isActivated()) {
        if (!ensureJoined()) { deepSleepMillis(10000); return; }
    }
    sendSensorReport();
    debug("[SLEEP] Durmiendo %lu ms...\r\n", (unsigned long)SLEEP_INTERVAL_MS);
    Serial.flush();
    deepSleepMillis(SLEEP_INTERVAL_MS);
}
```

- `Serial.flush()` antes de dormir evita perder bytes del log.
- En `loop()` **no** hay `delay()` largo: el único sleep es `deepSleepMillis()` (deep sleep real). El único `delay(3000)` está en `setup()` (una vez).
- Si `radio.begin()` o `node.beginOTAA()` fallan, el sketch entra en `while (true) delay(1);` (bucle ocupado). Ver observaciones de la auditoría (`AUDIT_REPORT.md` OBS-001) para una mejora recomendada con backoff en deep sleep.

---

## Deep sleep: lowPowerHandler + TimerEvent RTC

El núcleo del bajo consumo de v3.0 es `deepSleepMillis()`:

```cpp
TimerEvent_t sleepTimer;
static volatile bool sleepExpired = false;

static void onSleepTimerExpired(void) {
    sleepExpired = true;
}

static void deepSleepMillis(uint32_t ms) {
    sleepExpired = false;
    TimerInit(&sleepTimer, onSleepTimerExpired);
    TimerSetValue(&sleepTimer, ms);
    TimerStart(&sleepTimer);

    radio.sleep();                    // SX1262 → sleep (~0.6 µA)

    while (!sleepExpired) {
        lowPowerHandler();            // CySysPmDeepSleep() hasta IRQ del RTC
    }

    TimerStop(&sleepTimer);
    radio.standby();                  // SX1262 → standby operativo
}
```

### Cómo funciona

1. **`TimerEvent_t`** (estructura del *timeServer* de Semtech, incluida vía `Arduino.h` → `timer.h`) + `TimerInit`/`TimerSetValue`/`TimerStart` programan una **alarma del RTC** (32.768 kHz) a `ms` milisegundos.
2. **`lowPowerHandler()`** (de `low_power.c` del core) ejecuta `CySysPmDeepSleep()` — deep sleep real del ASR6502/PSoC4 — y, al despertar, **re-sincroniza** `systime = RtcGetTimerValue()`.
3. Cuando expira el RTC, `TimerIrqHandler` invoca `onSleepTimerExpired()` **en contexto de IRQ**, que fija la bandera `volatile` `sleepExpired`. El bucle `while (!sleepExpired)` sale y se llama `TimerStop`.
4. `radio.sleep()` duerme el SX1262 (WARM START, RTC OFF, retiene configuración, ~0.6 µA) **antes** del deep sleep del MCU; `radio.standby()` lo reactiva **después** (envía `NOP` + `SET_STANDBY`).

### Por qué la sesión LoRaWAN sobrevive al deep sleep

`LoRaWANNode node` es un objeto **global**; todo su estado de sesión (claves de sesión, `devNonce`, `joinNonce`, `fCntUp/fCntDown`, `sessionStatus`, `band`) vive en **RAM**. `CySysPmDeepSleep` del ASR6502/PSoC4 **retiene la SRAM** (solo detiene el núcleo; no es *hibernate*). Por tanto, al despertar `node.isActivated()` sigue devolviendo `true` y **no se re-hace join** ni se pierden los frame counters.

### Timing correcto (sin el bug ~×100)

Por desensamblado de `CubeCellLib.a` (ver `AUDIT_REPORT.md`):
- `RtcGetTimerValue()` multiplica el contador crudo del CySysTimer (32.768 kHz) por 1000 y divide entre 32768 → **devuelve milisegundos reales**.
- `Asr_SetTimeout()` hace la conversión inversa (ms → ticks). Por tanto `TimerSetValue(600000)` equivale **exactamente a 10 minutos**. El bug ~×100 era del stack LoRaMac nativo, **no** de este camino RTC.

### Consideraciones

- El SPI del MCU permanece configurado a través del deep sleep (registros retenidos). `low_power.c` solo reconfigura MISO (P4_1) y UART_TX (P3_1) para reducir fugas, y los restaura al volver — por eso el nodo **no se cuelga** al despertar.
- El callback `onSleepTimerExpired()` es breve (una asignación), como exige una buena práctica de ISR.
- `volatile` es el calificador correcto para la bandera compartida ISR↔loop (bool de 1 byte, operación atómica en Cortex-M0+).

---

## API de funciones

| Función | Retorno | Descripción |
|---------|---------|-------------|
| `onSleepTimerExpired()` | `void` | Callback ISR del RTC; fija `sleepExpired = true` |
| `deepSleepMillis(uint32_t ms)` | `void` | Deep sleep del MCU + radio hasta expirar `ms` |
| `stateDecode(int16_t s)` | `const char*` | Traduce código de retorno de RadioLib a texto |
| `readBatteryMv()` | `uint16_t` | Voltaje de batería en mV + log `[BATT]` |
| `readDigitalBits()` | `uint8_t` | Byte de bits de las 6 entradas + log `[GPIO]` |
| `readAnalogMv(uint8_t pin)` | `uint16_t` | Voltaje analógico en mV + log `[ADC]` |
| `sendSensorReport()` | `int` | Lee, construye y envía el uplink de 7 bytes; maneja downlink |
| `handleDownlink(const uint8_t*, size_t)` | `void` | Ejecuta comando sobre GPIO1 (ON/OFF/TOGGLE) |
| `ensureJoined()` | `bool` | Re-join OTAA si la sesión se perdió |

### APIs de RadioLib / core usadas

| API | Uso |
|-----|-----|
| `SX1262::begin()` | Inicializa la radio (incluye `setDio2AsRfSwitch(true)` por defecto) |
| `SX1262::sleep()` / `SX1262::standby()` | Duerme/reactiva la radio en el ciclo |
| `LoRaWANNode::beginOTAA(joinEui, devEui, nwkKey, appKey)` | Configura OTAA |
| `LoRaWANNode::activateOTAA()` | Ejecuta el join (retorna `NEW_SESSION`/`SESSION_RESTORED` o error) |
| `LoRaWANNode::isActivated()` | `true` si hay sesión válida |
| `LoRaWANNode::sendReceive(...)` | Envía uplink y recibe downlink (clase A) |
| `LoRaWANNode::scanGuard` | Guard time de escaneo de canales (ms) |
| `TimerInit` / `TimerSetValue` / `TimerStart` / `TimerStop` | Programación del temporizador RTC (timeServer) |
| `lowPowerHandler()` | Ejecuta `CySysPmDeepSleep()` (deep sleep nativo) |
| `getBatteryVoltage()` | Voltaje de batería en mV (core CubeCell) |
| `analogReadmV(pin)` | Voltaje analógico en mV (core CubeCell) |

---

## Workarounds de timers ASR6502

### El problema

El ASR6502 + core CubeCell presentan un **bug de temporizadores ~×100** (documentado en proyectos previos). En el stack nativo esto hacía que un re-join "de 30 s" real tardara ~3000 s.

### Qué se hace en v3.0

1. **RadioLib usa `millis()`** (`RtcGetTimerValue`, en ms reales) para su timing interno (escaneo de canales del join, ventanas RX1/RX2, `delay()` = `CyDelay`). Esto es inmune al bug.
2. **`scanGuard = 200`** — aumenta el guard time de escaneo de canales de RadioLib de 10 ms (por defecto) a 200 ms para compensar la deriva de timing del ASR6502:

   ```cpp
   node.scanGuard = 200;
   ```

3. **Deep sleep por camino RTC nativo** — `TimerEvent_t`/`lowPowerHandler()` están en milisegundos reales (verificado), de modo que el ciclo de 10 min es exacto.

> **Nota v2.1 → v3.0**: el `setSleepFunction(radioSleepDelay)` de v2.1 (polling `millis()` para las esperas de radio) **ya no se usa** en v3.0. Se eliminó al migrar al patrón de deep sleep nativo; solo queda `scanGuard = 200`.

---

## Limitaciones de memoria (RAM 16 KB)

El ASR6502 tiene **solo 16 KB de RAM**, lo que impone restricciones estrictas.

### Presupuesto de RAM (estimado, auditoría v3.0)

| Componente | Uso estimado |
|------------|--------------|
| `LoRaWANNode node` (global, dominante) | ~1.2–1.5 KB |
| `SX1262 radio` + `Module mod` | ~150–200 B |
| AES-128 por software (estática de RadioLib) | ~250 B |
| Credenciales del sketch | ~48 B |
| `downlinkData[242]` + contadores | ~252 B |
| Stack + heap transitorio de RadioLib | ~1.3 KB |
| **Total** | **~4.5 KB (~27 %)** |

Margen amplio (~11 KB libres); **sin riesgo de overflow ni fragmentación** (no hay `String` ni `malloc`).

### Buenas prácticas obligatorias

- **Evitar `String`** — fragmenta el heap en RAM 16 KB. Usar `uint8_t[]`/`char[]` y `printf`.
- **Buffers globales fijos** — el sketch no usa `malloc`/`new` propio; `payload[7]` va en pila (7 B, liberado al salir).
- **No usar `delay()` largo** en `loop()` — el patrón correcto es `deepSleepMillis()` (deep sleep real).
- **Sin recursión**.
- **Flash**: ~55–70 KB / 128 KB, margen amplio.

---

## Guía de modificación y extensión

### Cambiar el ciclo de operación

```cpp
#define SLEEP_INTERVAL_MS 600000UL   // 10 min → p. ej. 300000UL para 5 min
```

> El valor se pasa a `TimerSetValue()`, en milisegundos reales.

### Cambiar la región/subbanda

```cpp
const LoRaWANBand_t Region = AU915;   // p. ej. EU868, US915
const uint8_t subBand = 2;            // p. ej. 1 para subbanda 1
```

> AU915 y US915 tienen subbandas; EU868 no usa `subBand` (ignorado). Verificar que el NS/gateway coincide.

### Cambiar credenciales

```cpp
uint64_t joinEui = 0x...;  // big-endian
uint64_t devEui  = 0x...;
uint8_t appKey[] = { ... };
uint8_t nwkKey[] = { ... };
```

> **OJO con la endianidad**: RadioLib espera enteros big-endian. No copiar los arrays little-endian del stack nativo.

### Cambiar a confirmed uplinks

```cpp
// sendReceive(..., isConfirmed=true, ...)
int state = node.sendReceive(payload, sizeof(payload), 1,
                             downlinkData, &downlinkLen,
                             true, NULL, NULL);
```

### Persistir la sesión (opcional)

Añadir en `setup()` tras el join:

```cpp
// Guardar sesión para no re-hacer join en cada reset
node.beginPersistentSession(...);
```

> En v3.0 se evita a propósito (cada reset = join OTAA limpio) para no restaurar sesiones corruptas. Al no persistir, no se usa flash para la sesión.

### Añadir watchdog / recuperación ante fallo de init

La auditoría (`AUDIT_REPORT.md` OBS-001) recomienda sustituir los bucles `while (true) delay(1);` por reintentos con backoff en deep sleep:

```cpp
if (state != RADIOLIB_ERR_NONE) {
    debug("[INIT] Radio FAILED: %d (%s)\r\n", state, stateDecode(state));
    deepSleepMillis(SLEEP_INTERVAL_MS);   // dormir y reintentar al despertar
    // (requiere reestructurar setup en una máquina de estados)
}
```

---

## Cómo agregar nuevos sensores/actuadores

### 1. Ampliar el payload (ej.: añadir un `uint16` extra)

```cpp
uint8_t payload[9];                 // 7 + 2 nuevos bytes
payload[0] = dg;
payload[1] = (uint8_t)(ad2 >> 8);
payload[2] = (uint8_t)(ad2 & 0xFF);
payload[3] = (uint8_t)(ad3 >> 8);
payload[4] = (uint8_t)(ad3 & 0xFF);
payload[5] = (uint8_t)(bat >> 8);
payload[6] = (uint8_t)(bat & 0xFF);
payload[7] = (uint8_t)(extra >> 8);   // nuevo campo
payload[8] = (uint8_t)(extra & 0xFF);

node.sendReceive(payload, sizeof(payload), 1, ...);
```

**Actualizar** el decoder (README/datasheet) y el `printf` de `[UPLINK]` (bucle `i < 9`).

### 2. Leer un sensor I2C

- Usar `P0_0` (SCL) / `P0_1` (SDA) con pull-ups de 4.7 kΩ.
- Incluir `Wire.h` e inicializar `Wire.begin()` en `setup()`.
- Evitar `String` y buffers grandes.

### 3. Leer un sensor analógico (ADC) adicional

- Pines libres del SAR-ADC: `P2_3`–`P2_7` (si están expuestos) u otros canales analógicos. Usar `analogReadmV(pin)`.
- **No usar** `P2_0` (batería). Respetar el límite de 2.4 V (referencia interna).

### 4. Actuador digital adicional

- Usar un GPIO libre: `P6_2` (GPIO3/PWM1), `P6_4` (GPIO4/PWM2), `P0_2` (GPIO11), `P0_3` (GPIO12), `P0_7` (GPIO14).
- **No usar** `P6_1` (RF switch), `P3_0`/`P3_1` (UART), `P3_3` (batería), `P2_0` (ADC batería).
- Para alimentar periféricos externos de forma controlada, usar `P3_2` (`Vext`, LOW = ON) — actualmente en HIGH (periféricos externos apagados).

### 5. Nuevos comandos de downlink

Ampliar `handleDownlink()` con más códigos (p. ej. `0x03` = reset de contadores, `0x10` = forzar uplink inmediato). Mantener el primer byte como selector de comando y documentar el protocolo.

### 6. Reducir aún más el consumo

- Verificar el quiescent real del regulador/LDO (domina el consumo en reposo).
- Si los sensores externos se alimentan de `Vext`, ponerlo a LOW solo durante la medición y volverlo a HIGH antes de dormir.
- Confirmar que todos los pines no usados están configurados para minimizar fugas (alta impedancia o pull fijo).

---

## Depuración y logging

### Etiquetas de log

| Etiqueta | Momento | Significado |
|----------|---------|-------------|
| `[INIT]` | Boot | Estado de GPIO/radio/nodo/workaround |
| `[JOIN]` | Join | Inicio y resultado del join OTAA |
| `[GPIO]` | Lectura | Byte de bits de las 6 entradas |
| `[ADC]` | Lectura | Voltaje analógico en mV (por pin) |
| `[BATT]` | Lectura | Voltaje de batería en mV |
| `[UPLINK]` | Envío | Payload hex + resultado |
| `[RX]` | Downlink | Bytes recibidos + comando ejecutado |
| `[SLEEP]` | Sleep | Entrada en deep sleep y duración |
| `[REJOIN]` | Re-join | Reintento de join |

### Interpretación de códigos de error (RadioLib)

| Código | Constante | Significado |
|--------|-----------|-------------|
| 0 | `RADIOLIB_ERR_NONE` | Éxito |
| -701 | `RADIOLIB_ERR_RX_TIMEOUT` | Timeout RX (sin downlink en ventana) |
| -705 | `RADIOLIB_ERR_CHIP_NOT_FOUND` | Radio no detectada (SPI/pines) |
| -1101 | `RADIOLIB_ERR_NETWORK_NOT_JOINED` | Intento de envío sin sesión |
| -1113 | `RADIOLIB_ERR_MIC_MISMATCH` | MIC inválido (claves/sesión) |
| -1116 | `RADIOLIB_ERR_NO_JOIN_ACCEPT` | Join rechazado (credenciales/endianidad/región) |
| -1117 | `RADIOLIB_LORAWAN_SESSION_RESTORED` | ✅ Join OK (sesión restaurada) |
| -1118 | `RADIOLIB_LORAWAN_NEW_SESSION` | ✅ Join OK (sesión nueva) |

---

## Pruebas de validación

| # | Prueba | Criterio de éxito |
|---|--------|-------------------|
| 1 | Compilar con arduino-cli | `arduino-cli compile --fqbn CubeCell:CubeCell:CubeCell_AB02A` → `EXIT=0`, flash < 85 % |
| 2 | Subir firmware | `arduino-cli upload -p /dev/ttyUSB0` sin errores |
| 3 | Banner de boot | `[INIT] HTCC-AB02A nodo sensor v3.0 (RadioLib) — AU915 SB2` |
| 4 | Join exitoso | `[JOIN] +++ JOINED +++` |
| 5 | Uplink en UG56 | `Unconfirmed Data Up` del DevEUI `70B3D56F27CEE98B` |
| 6 | Payload correcto (7 B) | `digital` refleja entradas; `adc2/adc3` en 0–2400 mV; `battery` coherente |
| 7 | Ciclo de 10 min | Timestamps entre `[UPLINK]` ≈ 600 s; serial mudo entre ciclos (deep sleep real) |
| 8 | Downlink | Enviar `01`/`00`/`02` → `[RX] GPIO1 -> ON/OFF/TOGGLE` y salida P6_0 conmuta |
| 9 | Re-join | Apagar gateway → `[REJOIN]` reintentos con backoff; encender → `[REJOIN] +++ EXITOSO +++` |
| 10 | Sesión sobrevive al deep sleep | Tras varios ciclos, **no** aparece re-join (la sesión persiste en SRAM) |

> Consumo: verificar en campo con un µA-meter que el consumo en deep sleep está en el rango de decenas de µA (confirmar quiescent del regulador). Ver estimación en `docs/datasheet.md`.
