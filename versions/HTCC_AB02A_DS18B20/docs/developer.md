# Documentación de Desarrollo — HTCC_AB02A_DS18B20 (v1.2)

> Guía técnica del código fuente: arquitectura, módulos, bit-banging OneWire, cálculo de
> temperatura, power gating, deep sleep y workarounds de timing del ASR6502.

---

## 1. Visión general

El firmware es un **archivo único `.ino`** (mismo enfoque que el proyecto base
`HTCC_AB02A_LoRaWAN`), estructurado en **bloques estáticos** (sin clases propias) para
minimizar el overhead de RAM. El MCU es un **ASR6502** (ARM Cortex-M0+ con **16 KB de
RAM**), ya ocupado en buena parte por el stack **RadioLib**, por lo que el diseño evita
asignaciones dinámicas y librerías pesadas.

**Orden del archivo:**

```
1. Cabecera de documentación + includes (RadioLib, Arduino, math)
2. Módulo radio (SX1262) — pines del SiP
3. Credenciales OTAA (#define editables)
4. Región/sub-banda + LoRaWANNode
5. Pines del DS18B20 (VDD, DQ) + salida downlink
6. Constantes de operación
7. Buffers estáticos (downlinkData)
8. Low-power nativo (TimerEvent_t + lowPowerHandler)
9. Diagnóstico (debug, stateDecode)
10. OneWire / DS18B20 (bit-banging manual)
11. Lectura de batería
12. Payload / uplink (sendSensorReport)
13. Downlink (handleDownlink)
14. Re-join (ensureJoined)
15. Reinicio controlado (fatalErrorReset)
16. setup() y loop()
```

---

## 2. Arquitectura y flujo de datos

```
DS18B20 (DQ) ──OneWire bit-bang──▶ ds18b20ReadTemperature() ──▶ int16_t tenths
getBatteryVoltage() ─────────────────────────────────────────▶ uint16_t battMv
                     ▼
payload[4] = { temp[1], temp[0], bat[1], bat[0] }   (big-endian)
                     ▼
node.sendReceive(payload, 4, port=1, downlinkBuf, &len, confirmed=false, ...)
                     ▼
handleDownlink() (opcional)  ──▶ GPIO1 ON/OFF/TOGGLE
                     ▼
deepSleepMillis(600000) ──▶ (power gating OFF, radio.sleep())
```

### Bucle principal (`loop()`)

```cpp
void loop() {
    if (!node.isActivated()) {
        if (!ensureJoined()) {
            deepSleepMillis(10000);   // backoff y reintento
            return;
        }
    }
    sendSensorReport();
    deepSleepMillis(SLEEP_INTERVAL_MS);   // 10 min
}
```

---

## 3. Módulo radio y credenciales

### 3.1 SX1262 (pines del SiP)

```cpp
Module mod(35, 38, 47, 39);   // NSS=P4_3, DIO1=P4_6, RST=P5_7, BUSY=P4_7
SX1262 radio(&mod);
```

Estos pines son **internos del SiP ASR6502** y no son reasignables.

### 3.2 Nodo LoRaWAN

```cpp
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;
LoRaWANNode node(&radio, &Region, subBand);
```

En `setup()` se inicializa con `radio.begin()`, `node.beginOTAA(...)`, se fija
`node.scanGuard = 200` (workaround de bug de timers, heredado del base) y se ejecuta
`node.activateOTAA()`.

### 3.3 Credenciales OTAA (editables por tarjeta)

```cpp
#define OTAA_DEVEUI   0x70B3D56F27CEE98BULL
#define OTAA_APPEUI   0x24E124C0002ADD01ULL
#define OTAA_APPKEY   { 0x89,0xF5,...,0x86 }
#define OTAA_NWKKEY   { 0x89,0xF5,...,0x86 }
```

En producción masiva, cada tarjeta se flashea con sus propias credenciales editando estos
`#define`. `NwkKey == AppKey` asume LoRaWAN 1.0.x.

---

## 4. OneWire / DS18B20 (bit-banging manual)

### 4.1 Por qué sin librería

Se implementa OneWire directamente (sin `DallasTemperature`) porque:

- Solo hay **un sensor** y una **resolución fija** (12 bits): no se necesita búsqueda de
  direcciones ROM ni conversión asíncrona.
- Ahorro de Flash y RAM en el ASR6502 (16 KB), ya ocupado por RadioLib.
- Control total del timing para envolver en `noInterrupts()`.

### 4.2 Convención de línea (open-drain)

- Para **bajar** la línea (drive `0`): `pinMode(DQ, OUTPUT)` + `digitalWrite(DQ, LOW)`.
- Para **soltar** la línea (dejar que el pull-up la suba a `1`): `pinMode(DQ, INPUT)`.
  La línea sube a HIGH gracias al pull-up externo 4.7 kΩ (a VDD del sensor).

```cpp
static void ds18b20DQDriveLow(void) {
    pinMode(DS18B20_DQ_PIN, OUTPUT);
    digitalWrite(DS18B20_DQ_PIN, LOW);
}
static void ds18b20DQRelease(void) {
    pinMode(DS18B20_DQ_PIN, INPUT);
}
```

### 4.3 Reset + presencia

```cpp
static bool ds18b20Reset(void) {
    ds18b20DQDriveLow();
    delayMicroseconds(480);      // pulso de reset >= 480 µs
    ds18b20DQRelease();
    delayMicroseconds(70);       // ventana de presencia 15-60 µs
    bool present = (digitalRead(DS18B20_DQ_PIN) == LOW);
    delayMicroseconds(410);      // completar slot (~960 µs total)
    return present;
}
```

### 4.4 Escritura de byte (LSB first)

```cpp
static void ds18b20WriteByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x01) {
            ds18b20DQDriveLow();
            delayMicroseconds(6);      // write 1: LOW corto (1-15 µs)
            ds18b20DQRelease();
            delayMicroseconds(60);
        } else {
            ds18b20DQDriveLow();
            delayMicroseconds(60);     // write 0: LOW sostenido (60-120 µs)
            ds18b20DQRelease();
            delayMicroseconds(6);
        }
        b >>= 1;
    }
}
```

### 4.5 Lectura de byte (LSB first)

```cpp
static uint8_t ds18b20ReadByte(void) {
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {
        b >>= 1;
        ds18b20DQDriveLow();
        delayMicroseconds(3);
        ds18b20DQRelease();
        delayMicroseconds(8);
        if (digitalRead(DS18B20_DQ_PIN) == HIGH) b |= 0x80;  // muestreo ~11 µs
        delayMicroseconds(50);   // completar slot (~61 µs)
    }
    return b;
}
```

### 4.6 CRC-8 Dallas

```cpp
static uint8_t ds18b20Crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t b = *data++;
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;   // polinomio reflejado x^8+x^5+x^4+1
            b >>= 1;
        }
    }
    return crc;
}
```

El polinomio de Dallas es `x^8 + x^5 + x^4 + 1` (0x131, o **0x8C** en forma reflejada).
El CRC se calcula sobre los bytes 0–7 del scratchpad y debe coincidir con el byte 8.

---

## 5. Orquestación de la lectura (`ds18b20ReadTemperature`)

La función central que lee la temperatura es:

```cpp
static bool ds18b20ReadTemperature(int16_t* tenthsOut);
```

**Secuencia detallada:**

1. **Power-gate ON + estabilización**
   ```cpp
   digitalWrite(DS18B20_VDD_PIN, HIGH);
   delay(DS18B20_POWERUP_SETTLE_MS);   // 10 ms
   ```

2. **Reset + Skip ROM + Convert T (bit-bang, interrupciones deshabilitadas)**
   ```cpp
   noInterrupts();
   bool present = ds18b20Reset();
   if (present) {
       ds18b20WriteByte(DS18B20_CMD_SKIP_ROM);   // 0xCC
       ds18b20WriteByte(DS18B20_CMD_CONVERT);    // 0x44 (Convert T, 12 bits)
   }
   interrupts();
   ```

3. Si no hay presencia → `*tenthsOut = DS18B20_INVALID; return false;` (no aborta el ciclo).

4. **Espera de conversión (interrupciones HABILITADAS)**
   ```cpp
   delay(DS18B20_CONV_TIME_MS);   // 750 ms
   ```
   > El RTC sigue corriendo aquí; no se debe bloquear con `noInterrupts()`.

5. **Reset + Skip ROM + Read Scratchpad (bit-bang, interrupciones deshabilitadas)**
   ```cpp
   uint8_t scratch[9];
   bool scratchRead = false;
   noInterrupts();
   if (ds18b20Reset()) {
       scratchRead = true;
       ds18b20WriteByte(DS18B20_CMD_SKIP_ROM);        // 0xCC
       ds18b20WriteByte(DS18B20_CMD_READ_SCRATCH);    // 0xBE
       for (uint8_t i = 0; i < 9; i++) scratch[i] = ds18b20ReadByte();
   }
   interrupts();
   ```

6. **Validación CRC-8** del byte 8 contra bytes 0–7.

7. **Validación de sanidad del scratchpad** (corrección **I-001**):

   ```cpp
   if (!(scratch[5] == 0xFF && scratch[6] == 0x0C && scratch[7] == 0x10) ||
       (scratch[4] & 0x9F) != 0x1F) {
       // scratchpad insano -> DQ corto a GND?
       *tenthsOut = DS18B20_INVALID;
       return false;
   }
   ```

   **Motivo**: si DQ queda en corto a GND, el scratchpad lee todo-ceros y el CRC-8 de 8
   ceros es `0x00` (válido), lo que haría reportar 0.0 °C en vez del sentinel. Un DS18B20
   real devuelve fijos `scratch[5]=0xFF`, `scratch[6]=0x0C`, `scratch[7]=0x10`. La máscara
   `0x9F` sobre el byte 4 conserva el bit 7 (0) y los bits 4..0 (1), enmascarando los bits
   R1/R0 de resolución (bits 6/5), válidos para 9/10/11/12 bits.

8. **Cálculo de temperatura**
   ```cpp
   int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
   float tempC = raw * 0.0625f;
   *tenthsOut = (int16_t)roundf(tempC * 10.0f);
   ```

---

## 6. Cálculo de temperatura (detalle)

El DS18B20 entrega la temperatura en el scratchpad como entero de 12 bits en complemento
a dos:

- **Positiva**: `raw = 0x00EA` = 234 → `234 * 0.0625 = 14.625 °C`.
- **Negativa**: `raw = 0xFFC9` = -55 (extensión de signo de 12 bits) → `-55 * 0.0625 =
  -5.5 °C`.

El firmware lo convierte a **décimas de °C**:

```cpp
int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);  // promoción a int 32 bits
float tempC = raw * 0.0625f;                              // escala °C/LSB
*tenthsOut = (int16_t)roundf(tempC * 10.0f);              // décimas
```

**Consideraciones de corrección:**

- `scratch[1] << 8` se evalúa en `int` (32 bits), por lo que no hay overflow antes del
  cast a `int16_t`.
- El cast `(int16_t)` sobre el OR de 16 bits produce correctamente valores negativos
  (complemento a dos).
- `roundf()` se usa para minimizar el error de redondeo al escalar a décimas. Ejemplo:
  `14.625 °C * 10 = 146.25` → `roundf` → `146` → payload `0x0092` → `14.6 °C`.
- El **sentinel** `DS18B20_INVALID = 0x7FFF` (32767) está fuera del rango físico
  (−550 … +1250), por lo que nunca colisiona con una lectura real.

---

## 7. Power gating

El sensor se alimenta desde `GPIO3/P6_2` (VDD) y los datos van por `GPIO4/P6_4` (DQ).

**Encendido (antes de medir):**
```cpp
digitalWrite(DS18B20_VDD_PIN, HIGH);
delay(DS18B20_POWERUP_SETTLE_MS);   // 10 ms de estabilización
```

**Apagado (después de medir, en `sendSensorReport`):**
```cpp
digitalWrite(DS18B20_VDD_PIN, LOW);
pinMode(DS18B20_DQ_PIN, INPUT);     // DQ en alta impedancia
```

**Detalles importantes:**

- El **pull-up externo de 4.7 kΩ se conecta a VDD del sensor** (no al 3.3 V fijo), de modo
  que al apagar VDD el pull-up queda sin corriente (cero fugas).
- **DQ en alta impedancia** (`INPUT`) durante el sueño para no inyectar corriente por el
  pin con el sensor apagado.
- En `setup()`, el estado inicial es `VDD LOW` y `DQ INPUT` (sin pull-up interno).
- La lectura (`ds18b20ReadTemperature`) **deja VDD en HIGH** y es el llamador
  (`sendSensorReport`) quien apaga el power gating **siempre**, incluso si la lectura
  falló (los `return` tempranos de la lectura no saltan el apagado).

---

## 8. Deep sleep nativo

El MCU no usa `delay()` para los 10 minutos (consumiría mA). En su lugar:

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

    radio.sleep();                       // dormir el SX1262 (~0.6 µA)

    while (!sleepExpired) {
        lowPowerHandler();               // CySysPmDeepSleep hasta IRQ del RTC
    }

    TimerStop(&sleepTimer);
    radio.standby();                     // reactivar radio al despertar
}
```

**Puntos clave:**

- `TimerEvent_t` usa el **RTC** del ASR6502 para despertar al MCU tras `ms` milisegundos.
- `lowPowerHandler()` entra en **`CySysPmDeepSleep`** (deep sleep real del Cortex-M0+).
- El radio se duerme **antes** del deep sleep y se reactiva con `radio.standby()` al
  despertar.
- Las GPIOs conservan su último estado durante el sueño, por eso VDD debe estar LOW y DQ
  en INPUT antes de llamar a `deepSleepMillis()`.

---

## 9. Payload y uplink (`sendSensorReport`)

```cpp
static int sendSensorReport(void) {
    int16_t tenths;
    ds18b20ReadTemperature(&tenths);

    digitalWrite(DS18B20_VDD_PIN, LOW);       // power gating OFF
    pinMode(DS18B20_DQ_PIN, INPUT);

    uint16_t bat = readBatteryMv();           // getBatteryVoltage()

    uint8_t payload[4];
    payload[0] = (uint8_t)((uint16_t)tenths >> 8);  // temp int16 BE (MSB)
    payload[1] = (uint8_t)((uint16_t)tenths & 0xFF); // temp int16 BE (LSB)
    payload[2] = (uint8_t)(bat >> 8);               // batt uint16 BE (MSB)
    payload[3] = (uint8_t)(bat & 0xFF);             // batt uint16 BE (LSB)

    downlinkLen = sizeof(downlinkData);
    int state = node.sendReceive(payload, sizeof(payload), 1,
                                 downlinkData, &downlinkLen,
                                 false, NULL, NULL);

    if (state == RADIOLIB_ERR_NONE) {
        // OK sin downlink
    } else if (state > 0) {
        handleDownlink(downlinkData, downlinkLen);
    } else {
        // error
    }
    return state;
}
```

**Notas sobre `node.sendReceive(...)`:**

- Firma de 8 argumentos: `(data, len, fPort, rxBuffer, rxLen, confirmed, fCntUp, fCntDown)`.
- `confirmed = false` (uplink no confirmado).
- `state > 0` indica que se recibió downlink; `state == RADIOLIB_ERR_NONE` indica uplink
  OK sin downlink.
- El patrón `state == ERR_NONE` / `state > 0` / `else` coincide con RadioLib 7.7.1 y el
  proyecto base.

---

## 10. Downlink (`handleDownlink`)

```cpp
static void handleDownlink(const uint8_t* data, size_t len) {
    if (len < 1) return;
    switch (data[0]) {
        case 0x01: digitalWrite(OUTPUT_PIN, HIGH); break;              // ON
        case 0x00: digitalWrite(OUTPUT_PIN, LOW);  break;              // OFF
        case 0x02: digitalWrite(OUTPUT_PIN, !digitalRead(OUTPUT_PIN)); // TOGGLE
        default: /* comando desconocido */ break;
    }
}
```

La salida es `GPIO1/P6_0` (heredada del base v1.1). Solo el primer byte del downlink se
interpreta; valores no reconocidos se ignoran de forma segura.

---

## 11. Re-join automático (`ensureJoined`)

```cpp
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        return true;
    }
    return false;
}
```

Si la sesión se pierde (p. ej. el gateway estuvo caído), el `loop()` reintenta con backoff
de 10 s de deep sleep.

---

## 12. Reinicio controlado (`fatalErrorReset`) — corrección I-004

Sustituye el `while (true) delay(1)` del base por un reinicio limpio:

```cpp
static void fatalErrorReset(void) {
    Serial.flush();
    delay(1000);
    NVIC_SystemReset();           // soft-reset del Cortex-M0+ (CMSIS)

    // Respaldo si el reset no actuara: deep sleep periódico, no busy-wait.
    while (true) {
        radio.sleep();
        deepSleepMillis(10000);
    }
}
```

Se llama ante fallo de `radio.begin()` o `node.beginOTAA()`. Evita dejar el nodo colgado
consumiendo batería.

> **Nota de portabilidad**: `NVIC_SystemReset()` se asume disponible vía CMSIS
> (`core_cm0plus.h`). Si un toolchain concreto no lo expusiera, usar
> `HAL_NVIC_SystemReset()` o el equivalente del SDK ASR650x.

---

## 13. Workarounds de timing del ASR6502

### 13.1 Contexto

El bit-banging OneWire usa `digitalRead`/`digitalWrite` de la API Arduino, que en el core
CubeCell/ASR6502 pueden tener **latencia variable**. Esto puede producir deriva de timing
y, en consecuencia, **CRC inválido intermitente** en campo.

### 13.2 Workaround primario (ya implementado)

- Las fases de bit-bang (reset, write, read) se envuelven en
  **`noInterrupts()` / `interrupts()`** para que el RTC o el radio no rompan los
  time-slots.
- La espera de conversión (750 ms) queda **fuera** de `noInterrupts()`, con interrupciones
  habilitadas (el RTC sigue corriendo).
- Dentro de `noInterrupts()` solo se usa `delayMicroseconds()` (busy-wait, no depende de
  SysTick).
- Los pares `noInterrupts()`/`interrupts()` están **balanceados** (sin `return` temprano
  entre ambos).

### 13.3 Fallback documentado (si el timing falla en campo)

Si aparecen CRC inválidos intermitentes pese al workaround, migrar el bit-bang a
**`digitalWriteFast`** o a **acceso directo a registros GPIO** del ASR6502, dentro de las
mismas fases `noInterrupts()`. Verificar en campo con **≥10 lecturas consecutivas sin CRC
inválido** antes de dar por bueno el timing.

### 13.4 `node.scanGuard = 200`

Workaround heredado del base para un bug de timers en las esperas del radio (ventanas RX).
No debe removerse sin re-verificar el comportamiento de `sendReceive` en AU915.

---

## 14. Gestión de memoria (RAM 16 KB)

El ASR6502 tiene solo **16 KB de RAM**. Estrategias usadas:

- **Sin asignaciones dinámicas**: `downlinkData[242]` estático; `scratch[9]` y
  `payload[4]` en el stack.
- **Funciones estáticas** y sin clases propias (mínimo overhead).
- **Sin `DallasTemperature`**: solo se implementa la secuencia OneWire mínima necesaria.
- **`stateDecode`** usa `switch` sobre constantes (sin tablas de strings duplicadas).

Al modificar el código, evitar añadir buffers grandes, `String` dinámico o librerías
pesadas.

---

## 15. Guía de modificación y extensión

### 15.1 Cambiar el intervalo de envío

Editar:
```cpp
#define SLEEP_INTERVAL_MS 600000UL
```

### 15.2 Cambiar la resolución del sensor

Por defecto 12 bits (750 ms). Si se quisiera 9–11 bits habría que:
- Enviar el comando `Write Scratchpad` (`0x4E`) con el registro de configuración
  correspondiente, y
- Reducir `DS18B20_CONV_TIME_MS` (93.75 ms / 187.5 ms / 375 ms según bits).
- Actualizar la validación de sanidad (máscara `0x9F`) que hoy asume los bits fijos.

### 15.3 Añadir múltiples sensores

- Reemplazar `Skip ROM (0xCC)` por `Match ROM (0x55)` + dirección de 64 bits, o usar
  búsqueda de ROM.
- Alternativa documentada: migrar a `DallasTemperature` si se necesitan múltiples sensores
  o resoluciones dinámicas.

### 15.4 Eliminar la salida por downlink

Eliminar `OUTPUT_PIN` y `handleDownlink()` (no afecta el resto del diseño).

### 15.5 Separar en módulos

Si el proyecto crece, extraer `ds18b20.cpp/h` como módulo (funciones de bus + cálculo),
manteniendo `setup()`/`loop()` en el `.ino`.

### 15.6 Cambiar de región o puerto

- Región: cambiar `Region` (p. ej. `EU868`, `US915`) y `subBand` según corresponda.
- Puerto: cambiar el argumento `1` en `node.sendReceive(...)` y ajustar el `decoder.js`
  (validación de `fPort`).

---

## 16. API de funciones (resumen)

| Función | Retorno | Descripción |
|---|---|---|
| `onSleepTimerExpired()` | `void` | Callback del timer RTC: marca `sleepExpired` |
| `deepSleepMillis(uint32_t ms)` | `void` | Deep sleep real durante `ms` |
| `stateDecode(int16_t s)` | `const char*` | Convierte código de error RadioLib a texto |
| `ds18b20DQDriveLow()` | `void` | Pone DQ en OUTPUT LOW (drive 0) |
| `ds18b20DQRelease()` | `void` | Pone DQ en INPUT (alta impedancia) |
| `ds18b20Reset()` | `bool` | Reset OneWire + detección de presencia |
| `ds18b20WriteByte(uint8_t b)` | `void` | Escribe 1 byte (LSB first) |
| `ds18b20ReadByte()` | `uint8_t` | Lee 1 byte (LSB first) |
| `ds18b20Crc8(data, len)` | `uint8_t` | CRC-8 Dallas |
| `ds18b20ReadTemperature(int16_t*)` | `bool` | Orquesta la lectura completa; escribe `*tenthsOut` |
| `readBatteryMv()` | `uint16_t` | Lee voltaje de batería (mV) |
| `sendSensorReport()` | `int` | Mide, arma payload y hace uplink |
| `handleDownlink(data, len)` | `void` | Procesa comando de downlink (GPIO1) |
| `ensureJoined()` | `bool` | Re-join OTAA si es necesario |
| `fatalErrorReset()` | `void` | Reinicio controlado ante fallo fatal |

---

*Documentación técnica generada a partir de `HTCC_AB02A_DS18B20.ino` (v1.2).*
