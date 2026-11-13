# Documentación de desarrollador — Cubecell-htcc-ab01-2

Guía técnica del firmware `Cubecell-htcc-ab01-2.ino` (584 líneas, RadioLib 7.7.1, core CubeCell 1.4.0).

---

## Índice

1. [Visión general de la arquitectura](#1-visión-general-de-la-arquitectura)
2. [Estructura del archivo fuente](#2-estructura-del-archivo-fuente)
3. [Máquina de estados](#3-máquina-de-estados)
4. [setup() paso a paso](#4-setup-paso-a-paso)
5. [loop() paso a paso](#5-loop-paso-a-paso)
6. [API de funciones](#6-api-de-funciones)
7. [Persistencia en EEPROM](#7-persistencia-en-eeprom)
8. [Flujo detallado del ciclo](#8-flujo-detallado-del-ciclo)
9. [Guía de modificación y extensión](#9-guía-de-modificación-y-extensión)
10. [Notas de RadioLib 7.7.1 relevantes](#10-notas-de-radiolib-771-relevantes)
11. [Compilación y verificación](#11-compilación-y-verificación)

---

## 1. Visión general de la arquitectura

El firmware es **monolítico** (un solo `.ino`) sin capas artificiales. La separación es por responsabilidad funcional:

```text
┌─────────────────────────────────────────────────────────────┐
│                   Cubecell-htcc-ab01-2.ino                  │
│                                                             │
│  ┌────────────┐  ┌────────────┐  ┌───────────┐  ┌────────┐  │
│  │ Constantes │  │ Credenciales│  │ EEPROM    │  │ Radio  │  │
│  │ y tipos    │  │ (credentials.h)│ (config + │  │ + stack│  │
│  │            │  │            │  │  sesión)  │  │ LoRaWAN│  │
│  └────────────┘  └────────────┘  └───────────┘  └────────┘  │
│        │              │               │             │       │
│        └──────────────┴───────┬───────┴─────────────┘       │
│                               ▼                             │
│              ┌─────────────────────────────────┐            │
│              │  Flujo de aplicación            │            │
│              │  setup(): init + join/restore   │            │
│              │  loop():  uplink → downlink →   │            │
│              │           deep sleep            │            │
│              └─────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────┘
```

Dependencias externas:

- `RadioLib.h` — radio SX1262 + pila LoRaWAN (`LoRaWANNode`).
- `EEPROM.h` — persistencia (emulada sobre flash del ASR6502).
- `Arduino.h` — core CubeCell (`lowPowerHandler()`, `TimerEvent_t`, `NVIC_SystemReset()`).
- `credentials.h` — claves reales (fuera del repo; ver `credentials.example.h`).

---

## 2. Estructura del archivo fuente

Orden de secciones en el `.ino`:

```text
 1. Cabecera y comentario de proyecto
 2. Includes: RadioLib.h, EEPROM.h, Arduino.h, credentials.h
 3. debug() — macro/serial printf
 4. Constantes de configuración (SF, intervalos, comandos, direcciones EEPROM)
 5. Objetos de hardware:
      Module mod(35, 38, 47, 39);          // NSS, DIO1, BUSY, NRST
      SX1262 radio(&mod);
      Region Region = AU915;               // instanciado como objeto global
      const uint8_t subBand = 2;
      LoRaWANNode node(&radio, &Region, subBand);
 6. Estado global:
      NodeConfig gCfg;            // config persistente
      bool gCfgValid;
      int16_t gLastRssi;          // RSSI del último downlink
      uint8_t gUplinksSinceSave;  // contador para saveSessionThrottled
      uint8_t downlinkData[256];  // buffer global (¡no stack! deep sleep)
      size_t  downlinkLen;
 7. Persistencia: loadConfig/saveConfig, saveNoncesToEeprom,
      saveSessionToEeprom, saveSessionThrottled, loadSession
 8. Utilidades: stateDecode(), drFromSf(), applyDrFromConfig()
 9. Deep sleep: TimerEvent_t sleepTimer, onSleepTimerExpired(), deepSleepMillis()
10. Recuperación: fatalErrorReset()
11. Join: ensureJoined()
12. Payload: buildPayload()
13. Downlink: handleDownlink()
14. Uplink: sendReport()
15. setup() y loop()
```

### Comentario de cabecera (resumen)

```cpp
/*
 * Cubecell-htcc-ab01-2
 * Nodo LoRaWAN v1.0 - Heltec CubeCell HTCC-AB01
 * AU915 sub-banda 2 - OTAA - Clase A - RadioLib 7.7.1
 * Gateway: Milesight UG56 (NS embebido)
 * ...
 */
```

---

## 3. Máquina de estados

El nodo no implementa una FSM explícita; su comportamiento emerge del par `setup()` + `loop()` + deep sleep:

```text
         ┌────────────────────────────────────────────────┐
         │                                                │
         ▼                                                │
   ┌───────────┐   join OK    ┌───────────┐  uplink OK   │
   │  ARRANQUE │─────────────►│ OPERANDO  │──────────────┤
   │ (setup)   │              │ (loop)    │              │
   └─────┬─────┘              └─────┬─────┘              │
         │ sesión restaurada        │                    │
         │ (SESSION_RESTORED)       │ downlink válido    │
         │                          ▼                    │
         │                   ┌────────────┐              │
         │                   │ APLICAR    │              │
         │                   │ comando    │              │
         │                   └─────┬──────┘              │
         │                         │                     │
         ▼                         ▼                     │
   ┌───────────┐   fallo join ┌──────────────┐           │
   │  RETRY    │◄─────────────│ DEEP SLEEP   │───────────┘
   │ 10 s      │              │ (intervalo)  │
   └───────────┘              └──────────────┘
```

Estados lógicos:

| Estado | Cómo se detecta | Transición |
|---|---|---|
| `ARRANQUE` | `setup()` en curso | → `OPERANDO` si `isActivated()`, → `RETRY` si no |
| `OPERANDO` | `loop()` con `isActivated()==true` | → `DEEP_SLEEP` siempre |
| `RETRY` | join falló en `loop()` | → `DEEP_SLEEP(10 s)` → nuevo intento |
| `DEEP_SLEEP` | `deepSleepMillis()` | → `ARRANQUE` (wakeup) |

> En la práctica, cada despertar **re-ejecuta `setup()`** (el ASR6502 reinicia tras deep sleep). Por eso `setup()` hace toda la restauración de sesión, y el estado "vivo" entre ciclos solo puede residir en EEPROM o en SRAM retenida (`gCfg`, `gLastRssi`).

---

## 4. setup() paso a paso

```cpp
void setup() {
    Serial.begin(115200);
    delay(3000);                          // margen para abrir el monitor
    // banner [INIT] ...
    // seed del PRNG: micros ^ (millis<<16) ^ (ADC<<8) ^ ADC
    randomSeed(seed);
    loadConfig();                         // EEPROM → gCfg (defaults si inválida)
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);              // AB01: LOW = Vext ON (v1.0 sin periféricos: inocuo)
    if (!radio.begin())              fatalErrorReset();
    if (!node.beginOTAA(joinEui, devEui, nwkKey, appKey)) fatalErrorReset();
    node.scanGuard = 200;                 // padding de ventana RX (default 10)
    loadSession();                        // restaura nonces+sesión de EEPROM
    int16_t st = node.activateOTAA();     // aquí ocurre el join o el restore
    if (st == RADIOLIB_LORAWAN_SESSION_RESTORED)  { /* log restore */ }
    else if (st == RADIOLIB_LORAWAN_NEW_SESSION)  { saveSessionToEeprom(); }
    else                                          { saveNoncesToEeprom(); }
    if (node.isActivated()) {
        node.setADR(false);
        node.setDwellTime(false);
        applyDrFromConfig();
    }
    // log del ciclo: [INIT] Ciclo de N min ...
}
```

### Puntos críticos

1. **Orden obligatorio RadioLib 7.7.1**: `beginOTAA()` → `loadSession()` → `activateOTAA()`.
   - `setBufferNonces()`/`setBufferSession()` (dentro de `loadSession()`) deben llamarse **después** de `beginOTAA()`.
   - `activateOTAA()` es quien decide: nueva sesión (`NEW_SESSION`), sesión restaurada (`SESSION_RESTORED`) o error.
2. **`scanGuard = 200`**: RadioLib usa esta propiedad para extender la ventana de escucha del downlink. El valor por defecto (10 ms) es demasiado corto con gateways que responden lento (UG56). 200 ms es el valor verificado en campo de este proyecto.
3. **Nonces antes de cada intento**: `saveNoncesToEeprom()` se llama **siempre** tras un fallo de join, y también desde `ensureJoined()` en `loop()`. Esto es lo que impide repetir DevNonces entre power-cycles.
4. **`setADR(false)` + `setDwellTime(false)`**: tras cada join/rejoin, porque el estado del nodo se reconstruye en cada arranque.
5. **`fatalErrorReset()`**: cualquier fallo de radio o de configuración OTAA reinicia el sistema limpio (ver §6).

---

## 5. loop() paso a paso

```cpp
void loop() {
    if (!node.isActivated()) {
        if (!ensureJoined()) {
            deepSleepMillis(10000);   // reintento ~12 s
            return;
        }
        saveSessionToEeprom();        // sesión nueva tras rejoin
    }
    uint8_t payload[PAYLOAD_LEN];
    buildPayload(payload);
    sendReport(payload, PAYLOAD_LEN);
    deepSleepMillis((uint32_t)gCfg.intervalMin * 60000UL);
    // -- nunca se llega aquí: el deep sleep reinicia el chip --
}
```

### Decisiones clave

- El **uplink se intenta una sola vez** por ciclo. Si falla, se duerme; el Clase A tolera pérdidas (el frame counter se recupera del NS en el próximo uplink).
- El **downlink se procesa dentro de `sendReport()`** (en la ventana RX del propio uplink), nunca en un ciclo aparte.
- El **intervalo leído en `gCfg.intervalMin` al final del ciclo** implica que un downlink de cambio de intervalo recibido en este ciclo ya afecta al sleep que sigue.
- `loop()` **no retorna normalmente**: `deepSleepMillis()` no regresa (reset por hardware al expirar el timer).

---

## 6. API de funciones

### 6.1 Utilidades

#### `stateDecode(int16_t state) → const char*`

Traduce códigos de estado de RadioLib a texto legible para los logs. Mapeos:

| Código | Texto |
|---|---|
| 0 | `OK` |
| -2 | `CHIP_NOT_FOUND` |
| -4 | `PACKET_TOO_LONG` |
| -6 | `RX_TIMEOUT` |
| -7 | `MIC_MISMATCH` |
| -1101 | `NOT_JOINED` |
| -1116 | `NO_JOIN_ACCEPT` |
| -1117 | `CHECKSUM_MISMATCH` |
| -1118 | `NONCES_DISCARDED` |
| -1119 | `SESSION_DISCARDED` |
| 2 (positivo) | `NEW_SESSION` |
| 1 (positivo) | `SESSION_RESTORED` |
| — | `UNKNOWN` |

> Los valores positivos corresponden a los estados de sesión de RadioLib; los negativos a errores.

#### `drFromSf(uint8_t sf) → uint8_t`

Convierte Spreading Factor a Data Rate AU915:

```
SF12 → DR0    SF11 → DR1    SF10 → DR2
SF9  → DR3    SF8  → DR4    SF7  → DR5
(otro SF → 5 = DR5, fallback seguro)
```

#### `applyDrFromConfig()`

Aplica el SF guardado en `gCfg.sf` a la radio:

```cpp
node.setDatarate(drFromSf(gCfg.sf));  // efectivo tras el próximo uplink
```

### 6.2 Persistencia

#### `loadConfig()` / `saveConfig()`

- `loadConfig()`: lee `NodeConfig` de `CFG_EEPROM_ADDR` (0). Valida magic (`0xC0 0xC1`) y hace *clamps*:
  - `sf` fuera de 7..9 → 7.
  - `intervalMin` fuera de 1..1440 → 1.
  - Magic inválido → defaults (SF7, 1 min).
- `saveConfig()`: escribe el struct completo + `EEPROM.commit()`.

#### `saveNoncesToEeprom()`

Copia el buffer de nonces del nodo al bloque de sesión EEPROM (bytes 2..16 del bloque en `0x040`). Se llama **inmediatamente después de cualquier intento de join** (éxito o fallo). Es la defensa contra `-1116` por nonce repetido.

#### `saveSessionToEeprom()`

Vuelca el buffer de sesión completo (DevAddr, claves, counters — 302 B) al bloque de sesión y **resetea `gUplinksSinceSave = 0`**. Se llama: tras `NEW_SESSION` en `setup()`, tras un rejoin en `loop()`, y vía `saveSessionThrottled()`.

#### `saveSessionThrottled()`

Incrementa `gUplinksSinceSave`; cuando alcanza `SESS_SAVE_EVERY_UPLINKS` (32) llama a `saveSessionToEeprom()`. Se invoca tras **cada uplink** (con o sin downlink). Objetivo: mantener la EEPROM "fresca" sin desgastarla (limitación de ciclos de escritura).

#### `loadSession()`

```cpp
static uint8_t noncesBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];   // 14 B
static uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE]; // 302 B
// lee EEPROM, valida SESS_MAGIC0/1
// node.setBufferNonces(...);
// node.setBufferSession(...);
```

**Requisito**: llamarla **después de `node.beginOTAA()` y antes de `node.activateOTAA()`**. Si se invoca en otro orden, RadioLib ignora los buffers o el estado de restauración no se arma (`SESSION_PENDING`).

### 6.3 Deep sleep

#### `deepSleepMillis(uint32_t ms)`

```cpp
void deepSleepMillis(uint32_t ms) {
    radio.sleep();                          // SX1262 a sleep
    TimerInit(&sleepTimer, onSleepTimerExpired);
    TimerSetValue(&sleepTimer, ms);
    TimerStart(&sleepTimer);
    while (true) {
        lowPowerHandler();                  // core CubeCell: MCU a low-power
    }
    // (no retorna; al expirar el timer el chip despierta y re-ejecuta setup())
}
```

- `onSleepTimerExpired()` detiene el timer (`TimerStop`) y llama a `radio.standby()` (la radio despierta lista — aunque tras el wake el chip se reinicia igualmente).
- El bucle `lowPowerHandler()` mantiene la MCU en el estado de mínimo consumo soportado por el core.
- No se usa `setSleepFunction()` de RadioLib (ver §10).

#### `fatalErrorReset()`

```cpp
void fatalErrorReset() {
    delay(1000);
    NVIC_SystemReset();                     // reset limpio del Cortex-M0+
    // fallback si el reset no se ejecuta de inmediato:
    radio.sleep();
    deepSleepMillis(10000);
}
```

### 6.4 Join

#### `ensureJoined() → bool`

Usado por `loop()` cuando la sesión no está activa (p. ej. `SESSION_DISCARDED`):

1. Loguea `[REJOIN] Iniciando join OTAA...`.
2. `node.activateOTAA()`.
3. `saveNoncesToEeprom()` **siempre**.
4. Si el retorno es `NEW_SESSION` o `SESSION_RESTORED`: log OK, `setADR(false)`, `setDwellTime(false)`, `applyDrFromConfig()` → `true`.
5. Si no: log del error y `false`.

### 6.5 Payload

#### `buildPayload(uint8_t* out)`

```cpp
uint32_t r = ((uint32_t)random() << 16) ^ random();  // 32 bits de entropía
out[0] = PAYLOAD_VERSION_OK;                          // 0x01
out[1] = (r >> 24) & 0xFF;                            // big-endian
out[2] = (r >> 16) & 0xFF;
out[3] = (r >>  8) & 0xFF;
out[4] =  r        & 0xFF;
```

### 6.6 Downlink

#### `handleDownlink(uint8_t* data, size_t len)`

Whitelist estricta:

| cmd | Validación | Acción |
|---|---|---|
| `0x01` | `len >= 2` y `data[1]` en 7..9 | `gCfg.sf = data[1]`; `setADR(false)`; `applyDrFromConfig()`; `saveConfig()`; log `[DL] SF cambiado a N (DRx)` |
| `0x02` | `len >= 3` y valor en 1..1440 | `gCfg.intervalMin = (data[1]<<8) \| data[2]`; `saveConfig()`; log |
| `0x03` | `len >= 2` | log del `gLastRssi` |
| otro | — | log `[DL] Comando 0x.. desconocido; ignorado` |

- **Nunca** se ejecuta nada fuera del whitelist.
- Los comandos aplican inmediatamente (el intervalo afecta al deep sleep del ciclo en curso, porque `loop()` lee `gCfg.intervalMin` después).

### 6.7 Uplink

#### `sendReport(uint8_t* payload, size_t len)`

```cpp
uint8_t  downlinkData[256];     // global
size_t   downlinkLen = 0;
LoRaWANEvent_t evtUp, evtDown;  // metadatos de RadioLib

int16_t st = node.sendReceive(
    payload, len,
    1,                          // fPort = 1
    downlinkData, &downlinkLen,
    false,                      // no confirmado
    &evtUp, &evtDown
);
```

Interpretación del retorno (contrato de RadioLib 7.7.1):

| Retorno | Significado | Acción del firmware |
|---|---|---|
| `RADIOLIB_LORAWAN_NONE` (0) | Uplink OK, sin downlink | log `[UPLINK] OK (sin downlink)`; `saveSessionThrottled()` |
| `> 0` (nº de ventana) | Downlink recibido | `gLastRssi = radio.getRSSI()` **antes** del sleep; log `[RX]`; `handleDownlink()`; `saveSessionThrottled()` |
| `< 0` | Error | log de error con `stateDecode()` |

Detalle importante: el RSSI se captura **inmediatamente después de `sendReceive()`** (antes de dormir la radio), porque tras `radio.sleep()` la lectura ya no es significativa.

---

## 7. Persistencia en EEPROM

### 7.1 Mapa de memoria

```text
Dirección   Tamaño   Contenido
─────────────────────────────────────────────────────────────
0x000       1 B      CFG_MAGIC0      = 0xC0
0x001       1 B      CFG_MAGIC1      = 0xC1
0x002       1 B      sf              (7..9)
0x003       1 B      intervalMin     (1..1440)
...         (reservado hasta 0x040)
─────────────────────────────────────────────────────────────
0x040       1 B      SESS_MAGIC0     = 0x5A
0x041       1 B      SESS_MAGIC1     = 0x5B
0x042      14 B      nonces          (RADIOLIB_LORAWAN_NONCES_BUF_SIZE)
0x050     302 B      session         (RADIOLIB_LORAWAN_SESSION_BUF_SIZE)
─────────────────────────────────────────────────────────────
Fin bloque sesión: 0x040 + 2 + 14 + 302 = 0x040 + 318 = 382 B < 512 B
```

### 7.2 Invariantes

1. Los buffers EEPROM (`noncesBuf`, `sessionBuf`) son `static` para **no ocupar stack** (la sesión son 302 B).
2. Existe un `#error` en tiempo de compilación si el layout no cabe en `EEPROM_ARENA_SIZE` (512 B).
3. Los magic bytes permiten invalidar bloques al cambiar de versión de firmware: si cambias el layout, incrementa `CFG_MAGIC0/1` y `SESS_MAGIC0/1`.
4. `EEPROM.begin(512)` se invoca una vez en cada camino de acceso (el core lo tolera).

### 7.3 Política de escritura

| Evento | Qué se guarda | Frecuencia |
|---|---|---|
| Intento de join (cualquiera) | Nonces | Cada intento |
| Join nuevo / rejoin | Sesión completa | Raro |
| Downlink de config | `NodeConfig` | Por comando |
| Uplink normal | Sesión (throttled) | Cada 32 uplinks |

---

## 8. Flujo detallado del ciclo

```text
[T0] Wakeup por TimerEvent (el chip se reinicia)
[T1] setup():
       - Serial, seed, loadConfig
       - radio.begin + beginOTAA
       - loadSession (restaura si hay)
       - activateOTAA:
            SESSION_RESTORED  → sigue (sin re-join)
            NEW_SESSION       → saveSessionToEeprom
            error             → saveNoncesToEeprom
       - Si activo: setADR(false), setDwellTime(false), applyDrFromConfig
[T2] loop():
       - ¿No activo? ensureJoined() → ¿fallo? sleep 10 s → FIN
       - buildPayload
[T3] sendReport:
       - sendReceive (uplink fPort 1, unconfirmed)
       - Abre RX1/RX2 (~1 s y ~2 s post-TX, Clase A)
       - ¿Downlink? → getRSSI → handleDownlink → (opcional) saveConfig
       - saveSessionThrottled
[T4] deepSleepMillis(intervalo × 60000)
       - radio.sleep(), TimerEvent, lowPowerHandler()
[T5] Vuelve a [T0]
```

---

## 9. Guía de modificación y extensión

### 9.1 Añadir un sensor I²C (p. ej. AMG8833, como el proyecto hermano)

1. Incluir `Wire.h` y la librería del sensor.
2. En `setup()`, **antes del join**, inicializar:

   ```cpp
   pinMode(Vext, OUTPUT);
   digitalWrite(Vext, LOW);   // AB01: LOW = Vext ON
   delay(50);                 // margen de encendido del regulador
   Wire.begin(SDA, SCL);      // P0_1 = 1, P0_0 = 0
   ```

3. Leer el sensor en `buildPayload()` (o en `loop()` antes de llamarla) y añadir los bytes:

   ```cpp
   void buildPayload(uint8_t* out) {
       out[0] = PAYLOAD_VERSION_OK;
       // ... bytes del sensor ...
       out[1] = ...;
   }
   ```

4. **Actualizar `PAYLOAD_LEN`** y el `decoder.js` (nuevos campos).
5. **Invalidar la EEPROM** si cambia el layout (magic bytes).
6. Importante: apagar el sensor con `digitalWrite(Vext, HIGH)` antes de `deepSleepMillis()` para no gastar batería.

### 9.2 Añadir la lectura de batería al payload

El core CubeCell ya trae `getBatteryVoltage()` (mV, `uint16_t`) y `getBatteryLevel()` (%). La medición usa `ADC` (P2_3) y `VBAT_ADC_CTL` (P3_3) internamente:

```cpp
uint16_t mv = getBatteryVoltage();       // p. ej. 3987 mV
out[5] = (mv >> 8) & 0xFF;               // big-endian
out[6] =  mv       & 0xFF;
```

Actualizar `PAYLOAD_LEN` a 7 y extender `decoder.js` con el campo `battery_mv`.

> Ojo: `VBAT_ADC_CTL` comparte GPIO con `USER_KEY` (P3_3). Si usas el botón, revisa `getBatteryVoltage()` del core.

### 9.3 Cambiar el payload (versión nueva)

1. Incrementa `PAYLOAD_VERSION_OK` a `0x02` y añade un `case` (o rama) en el decoder para distintas versiones.
2. Mantén el principio: **byte 0 = versión**, resto de campos big-endian.
3. Recompila el decoder con los nuevos campos (el warning de versión desconocida ya está implementado).

### 9.4 Cambiar la región

```cpp
// Region es un objeto instanciado por macro en RadioLib:
Region Region = AU915;      // → cambiar por tu región, p. ej. US915 o EU868
const uint8_t subBand = 2;  // AU915: 2. US915: 1 o 2. EU868: no aplica igual
```

Checklist:

- [ ] Región y sub-banda correctas para tu país (EU868 no usa sub-bandas, US915 sí, AU915 sí).
- [ ] Downlink `0x01` SF 7..9 sigue siendo válido para tu región (en EU868 el rango típico es SF7..SF12; ajusta `SF_MIN`/`SF_MAX` si cambias de región).
- [ ] Los DR cambian por región: `drFromSf()` es específico de AU915. Para US915 los DR coinciden (SF10→DR0...) pero **US915.2 requiere DR/offset distinto**. Revisar la tabla de RadioLib.
- [ ] El gateway y el NS deben estar en la misma región/sub-banda.

### 9.5 Cambiar el intervalo mínimo por defecto

En el `.ino`:

```cpp
static const uint8_t TX_INTERVAL_DEFAULT_MIN = 1;   // ← valor por defecto (solo aplica si EEPROM vacía)
```

> Recuerda: si ya hay una config válida en EEPROM, este default **no se aplica**, el valor real es el persistido. Para volver al default: cambiar el magic byte o usar el downlink `0x02`.

### 9.6 Añadir un comando downlink

1. Define la constante: `static const uint8_t DL_CMD_NUEVO = 0x04;`
2. Añade el caso en `handleDownlink()` **con validación de longitud y rango** (nunca ejecutar sin validar).
3. Extiende `encodeDownlink()` del `decoder.js`.
4. Documenta en README §7 y aquí.

---

## 10. Notas de RadioLib 7.7.1 relevantes

### 10.1 `setSleepFunction()` — por qué NO se usa

RadioLib 7.7.1 expone `node.setSleepFunction(cb)` para dormir la radio **en las esperas internas del stack** (retardos entre comandos MAC, `RADIOLIB_LORAWAN_DELAY_SLEEP_THRESHOLD`). **No** está pensada para el deep sleep de la aplicación. Además, la familia CubeCell/ASR650x core no trae la callback que espera esa API en versiones recientes. La estrategia de este proyecto (verificado en campo):

- `scanGuard = 200` (padding de ventana RX),
- `TimerEvent_t` + `lowPowerHandler()` para el sleep de la aplicación.

### 10.2 Contrato de `sendReceive()`

```text
Window number > 0  → downlink recibido
0 (RADIOLIB_LORAWAN_NONE) → no hubo downlink
< 0 → status_code de error
```

(Confirmado en `LoRaWANNode.h` de 7.7.1.) El firmware solo distingue estos tres casos.

### 10.3 Orden de inicialización obligatorio

```text
node.beginOTAA(...)     ← 1º
node.setBufferNonces()  ← 2º (dentro de loadSession)
node.setBufferSession() ← 2º (dentro de loadSession)
node.activateOTAA()     ← 3º
```

Si `setBufferSession()` se llama en un orden distinto, `activateOTAA()` puede no devolver `SESSION_RESTORED` aunque la sesión sea válida (el nodo re-joineará — funcional, pero gasta un join y un DevNonce).

### 10.4 `getMaxPayloadLen()` y el log `max=.. B`

`node.getMaxPayloadLen()` devuelve el máximo de bytes del payload para el DR actual, **incluyendo** restricciones de dwell time y ADR. Con SF7/DR5 en AU915 SB2 son ~242 B; con SF9/DR3 ~170 B. El firmware lo loguea en cada uplink.

### 10.5 Estados de sesión positivos

`NEW_SESSION` y `SESSION_RESTORED` son **valores positivos** (≠ 0) que `activateOTAA()` puede devolver; no son errores. `stateDecode()` los mapea a texto para evitar confusión en los logs.

---

## 11. Compilación y verificación

```bash
# Compilar
arduino-cli compile --fqbn CubeCell:CubeCell:CubeCell-Board .

# Grabar
arduino-cli upload -p /dev/ttyUSB0 --fqbn CubeCell:CubeCell:CubeCell-Board .

# Monitor (quirk: necesita TTY; usar el workaround)
timeout 30 sh -c 'sleep 60 | arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200'
```

Resultado de la última build verificada:

| Métrica | Valor |
|---|---|
| Flash | 89.364 B / 131.072 B (68%) |
| RAM | ~1,3 KiB / ~16 KiB (~8%) |
| Upload | OK — 14,67 s, Silicon ID `0x256a11b5` (ASR6502) |

### Depuración

- Todos los mensajes usan `Serial.printf` a 115200. No hay flags de compilación condicionales; el firmware siempre loguea (útil para campo, irrelevante para consumo: el UART se apaga con el deep sleep).
- Si añades código y el binario crece: el margen de flash es ~40 KB; el margen de RAM es amplio (la sesión LoRaWAN es lo más pesado y ya contabilizado).

---

## Apéndice A — Pines usados por el firmware

| Recurso | Pin | Uso |
|---|---|---|
| SX1262 NSS | P4_3 (35) | `Module mod(35,...)` |
| SX1262 DIO1 | P4_6 (38) | IRQ |
| SX1262 BUSY | P5_7 (47) | Busy |
| SX1262 NRST | P4_7 (39) | Reset |
| Vext | P3_2 (26) | Alimentación de periféricos (AB01: `LOW` = ON). En v1.0 queda ON, sin periféricos conectados |
| UART | P3_0/P3_1 | Monitor serie / programación |
| I²C SDA/SCL | P0_1/P0_0 | Libres (Wire) |
| ADC | P2_3 | Libre (batería en extensiones) |

## Apéndice B — Glosario

| Término | Significado |
|---|---|
| OTAA | Over-The-Air Activation (join con DevEUI/JoinEUI/AppKey) |
| ABP | Activation By Personalization (no usado aquí) |
| DevNonce | Contador anti-replay del join; se rechaza si se repite |
| DR | Data Rate (índice de SF + BW) |
| SF | Spreading Factor (7..12); más alto = más alcance y más tiempo de aire |
| Dwell time | Límite regulatorio de tiempo en aire por canal (AU915) |
| scanGuard | Padding de RadioLib para la ventana de recepción del downlink |
| `-1116` | Código de error de RadioLib: no se recibió join-accept |
