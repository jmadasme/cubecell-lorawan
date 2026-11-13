# Developer Guide — CubeCell Serial-to-LoRaWAN Bridge v1.2

## Arquitectura del firmware

```
 ┌──────────────────────────────────────────────────────────┐
 │                      CubeCell-LoRaWAN-Node.ino          │
 │                                                          │
 │  ┌────────────────────────────────────────────────────┐  │
 │  │               Seccion 1: Configuracion             │  │
 │  │  - Includes (RadioLib.h)                          │  │
 │  │  - Pines SX1262 (NSS=35, DIO1=38, RST=47, BUSY=39)│  │
 │  │  - Credenciales OTAA (devEui, joinEui, appKey)    │  │
 │  │  - Buffer serial (256 bytes)                       │  │
 │  │  - Payload maximo (51 bytes)                       │  │
 │  └────────────────────────────────────────────────────┘  │
 │                                                          │
 │  ┌────────────────────────────────────────────────────┐  │
 │  │           Seccion 2: Funciones de soporte          │  │
 │  │  - radioSleepDelay()   Workaround millis sleep    │  │
 │  │  - stateDecode()       Traduce codigos de error    │  │
 │  │  - parseHexString()    Convierte texto a bytes     │  │
 │  │  - ensureJoined()      Verifica/rejoin sesion      │  │
 │  │  - sendUplink()        Envia payload por LoRaWAN   │  │
 │  └────────────────────────────────────────────────────┘  │
 │                                                          │
 │  ┌────────────────────────────────────────────────────┐  │
 │  │              Seccion 3: setup()                    │  │
 │  │  1. Serial.begin(115200)                          │  │
 │  │  2. radio.begin() (SX1262)                        │  │
 │  │  3. node.beginOTAA()                               │  │
 │  │  4. node.setSleepFunction(radioSleepDelay)         │  │
 │  │  5. node.scanGuard = 200                           │  │
 │  │  6. node.activateOTAA() (join inicial)             │  │
 │  │  7. Muestra prompt "> "                            │  │
 │  └────────────────────────────────────────────────────┘  │
 │                                                          │
 │  ┌────────────────────────────────────────────────────┐  │
 │  │              Seccion 4: loop()                     │  │
 │  │                                                   │  │
 │  │  while (Serial.available()) {                     │  │
 │  │    leer caracter                                   │  │
 │  │    si es newline:                                  │  │
 │  │      procesar linea (parseHex → sendUplink)        │  │
 │  │    si no: acumular en buffer                       │  │
 │  │  }                                                 │  │
 │  │  if (buffer parcial y >500ms sin datos):           │  │
 │  │    descartar buffer (stale flush)                  │  │
 │  │  delay(10)                                         │  │
 │  └────────────────────────────────────────────────────┘  │
 └──────────────────────────────────────────────────────────┘
```

---

## Diagrama de flujo completo

```
                     ┌──────────────────────┐
                     │    POWER ON / RESET   │
                     └──────────┬───────────┘
                                │
                                v
                     ┌──────────────────────┐
                     │      setup()         │
                     │  1. Serial.begin     │
                     │  2. radio.begin()    │
                     │  3. beginOTAA()      │
                     │  4. setSleepFunction │
                     │  5. scanGuard=200    │
                     └──────────┬───────────┘
                                │
                                v
                     ┌──────────────────────┐
                     │  activateOTAA()      │
                     │  (Join Request)       │
                     └──────────┬───────────┘
                                │
                     ┌──────────┴──────────┐
                     │                     │
                  EXITO                  FALLA
                     │                     │
                     v                     v
              "+++ JOINED +++"      "Join FAILED"
                     │                     │
                     └──────────┬──────────┘
                                │
                                v
              ┌─────────────────────────────┐
              │   Prompt "> "               │
              │   Esperando comando serial  │
              └──────────┬──────────────────┘
                         │
              ┌──────────┴──────────┐
              │                     │
         No hay datos          Llega newline
              │                     │
              v                     v
   ┌──────────────────┐   ┌──────────────────────┐
   │ Stale check       │   │ parseHexString()     │
   │ >500ms sin datos? │   │ Convierte texto→bytes│
   │ Si: descartar buf │   └──────────┬───────────┘
   └──────────────────┘              │
                           ┌─────────┴─────────┐
                           │                   │
                        Hex OK            Hex invalido
                           │                   │
                           v                   v
                  ┌──────────────┐    ┌──────────────────┐
                  │ ensureJoined │    │ "?? Error: ..."   │
                  │ Esta activa? │    │ Prompt "> "      │
                  └──────┬───────┘    └──────────────────┘
                         │
              ┌──────────┴──────────┐
              │                     │
           Sesion OK           Sesion perdida
              │                     │
              v                     v
     ┌──────────────────┐  ┌──────────────────┐
     │ sendUplink()     │  │ activateOTAA()   │
     │ node.sendReceive │  │ (rejoin)         │
     └────────┬─────────┘  └────────┬─────────┘
              │                     │
              v                     v
     ">> Enviado: OK"      ┌───────┴───────┐
     Prompt "> "           │              │
              │         Exito          Falla
              │           │              │
              └───────────┘              v
                                   "Re-Join FAILED"
                                   "reintentando..."
                                        │
                                        v
                                   Prompt "> "
```

---

## Descripcion detallada de cada funcion

### `void setup()`

Funcion de inicializacion que se ejecuta una vez al encender o resetear.

**Secuencia:**
1. `Serial.begin(115200)` — inicializa puerto serial a 115200 baud.
2. Espera 3 segundos (`delay(3000)`) para que la terminal serial se conecte.
3. `radio.begin()` — inicializa el modulo SX1262 via SPI.
   - Si falla: mensaje de error y bucle infinito (`while(true) delay(1)`).
4. `node.beginOTAA(joinEui, devEui, nwkKey, appKey)` — configura el nodo LoRaWAN con credenciales OTAA.
   - Si falla: mensaje de error y bucle infinito.
5. `node.setSleepFunction(radioSleepDelay)` — reemplaza delay interno de RadioLib por millis().
6. `node.scanGuard = 200` — extiende ventana de escaneo de Join Accept.
7. `node.activateOTAA()` — envia Join Request y espera Join Accept.
   - Muestra `+++ JOINED +++` o `Join FAILED: <codigo>`.
8. Muestra `=== Listo. Escribe HEX y presiona Enter ===\n> `.

**No retorna valor.**

---

### `void loop()`

Funcion principal que se ejecuta ciclicamente.

**Comportamiento:**
1. Lee todos los caracteres disponibles del puerto serial:
   - Si el caracter es `\n` o `\r` (newline): procesa la linea acumulada.
   - Si no: agrega el caracter al buffer `serialLine[]` (max 255 caracteres).
2. Actualiza `lastCharMs` con el timestamp de cada caracter recibido.
3. Si hay datos en el buffer pero pasaron >500ms desde el ultimo caracter: descarta el buffer (stale flush).
4. `delay(10)` — pequena pausa para no saturar el CPU.

**No retorna valor.**

---

### `static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut)`

Convierte una cadena de texto hexadecimal en un arreglo de bytes.

**Parametros:**
- `input`: cadena de texto (ej: `"DE AD BE EF"` o `"DEADBEEF"`).
- `output`: buffer de salida para los bytes convertidos.
- `maxOut`: capacidad maxima del buffer de salida (`MAX_PAYLOAD = 51`).

**Proceso interno:**
1. Filtra la entrada: solo conserva caracteres `0-9`, `A-F`, `a-f`. Descarta espacios, guiones, etc.
2. Verifica que la cantidad de caracteres hex sea par.
3. Itera de a pares, convirtiendo cada par a un byte via `strtoul(par, NULL, 16)`.

**Retorna:**
- Numero de bytes convertidos (> 0) en exito.
- `0` si error (entrada vacia, cantidad impar de caracteres hex, o maximo excedido).

---

### `static bool ensureJoined(void)`

Verifica que la sesion LoRaWAN este activa y la restaura si es necesario.

**Retorna:**
- `true`: sesion activa o rejoin exitoso.
- `false`: no se pudo establecer sesion.

**Logica:**
1. Si `node.isActivated()` retorna `true` → la sesion esta activa → `return true`.
2. Si no, intenta `node.activateOTAA()` para rejoin.
3. Si el rejoin es exitoso (`RADIOLIB_LORAWAN_NEW_SESSION`) → `return true`.
4. Si falla → `return false`.

---

### `static bool sendUplink(const uint8_t* data, size_t len)`

Envia un payload por LoRaWAN inmediatamente.

**Parametros:**
- `data`: puntero a los bytes a enviar.
- `len`: longitud en bytes (maximo 51).

**Retorna:**
- `true`: envio exitoso (con o sin downlink).
- `false`: error de envio.

**Logica:**
1. Muestra `>> Enviando: XX YY ZZ ...` (previsualizacion del payload).
2. `node.sendReceive(data, len)` — envia uplink no confirmado, escucha RX1/RX2.
3. Si el resultado es 0 (RADIOLIB_ERR_NONE): muestra `→ OK (sin downlink)`.
4. Si el resultado es > 0: muestra `→ OK + downlink (N bytes)`.
5. Si el resultado es < 0: muestra `→ ERROR <codigo> (<texto>)`.

---

### `static void radioSleepDelay(RadioLibTime_t ms)`

Workaround que reemplaza el `delay()` interno de RadioLib. En CubeCell, `delay()` usa el timer HF que escala incorrectamente por el bug del ASR6502. `millis()` usa el RTC WCO de 32.768 kHz y es preciso.

**Parametros:**
- `ms`: tiempo de espera en milisegundos.

**Funcionamiento:**
1. Si `ms == 0`, retorna inmediatamente.
2. Toma timestamp inicial con `millis()`.
3. Hace spin-loop hasta que `millis() - start >= ms`.

**Nota:** Es bloqueante. El CPU queda activo durante toda la espera.

---

### `static const char* stateDecode(int16_t s)`

Traduce codigos de estado de RadioLib a cadenas legibles.

**Parametros:**
- `s`: codigo de estado int16_t.

**Retorna:** cadena de texto con el nombre del estado.

| Codigo | Cadena |
|--------|--------|
| `0` | `"OK"` |
| `-1004` | `"CHIP_NOT_FOUND"` |
| `-1005` | `"PACKET_TOO_LONG"` |
| `-1003` | `"RX_TIMEOUT"` |
| `-1007` | `"MIC_MISMATCH"` |
| `-1101` | `"NOT_JOINED"` |
| `-1116` | `"NO_JOIN_ACCEPT"` |
| `10` | `"NEW_SESSION"` |
| `11` | `"SESSION_RESTORED"` |

---

## Variables globales clave

| Variable | Tipo | Descripcion |
|----------|------|-------------|
| `mod` | `Module` | Pines SPI del SX1262: NSS=35, DIO1=38, RST=47, BUSY=39 |
| `radio` | `SX1262` | Driver del modulo LoRa |
| `node` | `LoRaWANNode` | Nodo LoRaWAN (sesion, join, uplink/downlink) |
| `joinEui` | `uint64_t` | Join EUI (AppEUI) OTAA |
| `devEui` | `uint64_t` | Device EUI unico |
| `appKey` | `uint8_t[16]` | Application Key |
| `nwkKey` | `uint8_t[16]` | Network Key (igual a appKey en modo 1.0.x) |
| `Region` | `LoRaWANBand_t` | `AU915` |
| `subBand` | `uint8_t` | `2` (canales 8-15) |
| `txBuffer` | `uint8_t[51]` | Buffer de transmision |
| `txLen` | `size_t` | Longitud del payload actual |
| `serialLine` | `char[256]` | Buffer de linea serial |
| `serialPos` | `size_t` | Posicion actual en el buffer serial |
| `lastCharMs` | `uint32_t` | Timestamp del ultimo caracter recibido |
| `STALE_MS` | `#define` | `500` — tiempo para considerar datos como obsoletos |
| `MAX_PAYLOAD` | `#define` | `51` — maximo de bytes por uplink |
| `SERIAL_BUF` | `#define` | `256` — tamano del buffer serial |

---

## Guia de modificacion y extension

### Cambiar las credenciales OTAA

```cpp
uint64_t joinEui = 0x24E124C0002ADD01;
uint64_t devEui  = 0x24F124725F486022;
uint8_t appKey[] = { 0xA7, 0xF1, 0x9C, 0x4E, 0x82, 0xD5, 0xC0, 0x0F,
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 };
```

### Cambiar la banda o sub-banda

```cpp
const LoRaWANBand_t Region = US915;  // US915, EU868, AU915, etc.
const uint8_t subBand = 1;           // Sub-banda de 1 a 8
```

### Cambiar el baudrate serial

```cpp
Serial.begin(9600);   // Cambiar 115200 por el baudrate deseado
```

### Cambiar el tiempo de stale flush

```cpp
#define STALE_MS 1000  // 1 segundo en lugar de 500ms
```

### Cambiar el modo confirmado

```cpp
// En sendUplink(), cambiar:
int state = node.sendReceive(data, len, 1, true);  // true = confirmado
// El NS debe enviar ack, y el firmware espera por el.
```

---

## Como agregar procesamiento antes del envio

Si se quiere modificar el payload antes de enviarlo (ej: agregar checksum, encabezado, etc.), se edita la funcion `processSerialLine()`:

```cpp
static void processSerialLine(const char* line) {
    // ... parseo existente ...

    // Ejemplo: agregar checksum al final
    uint8_t checksum = 0;
    for (size_t i = 0; i < txLen; i++) checksum ^= txBuffer[i];
    txBuffer[txLen] = checksum;
    txLen++;

    sendUplink(txBuffer, txLen);
}
```

---

## Como agregar un sensor analogico

Ejemplo: leer temperatura de un LM35 en A0 y enviar el valor:

```cpp
static void processSerialLine(const char* line) {
    // Si el usuario escribe "TEMP", enviar lectura de sensor
    if (strcmp(line, "TEMP") == 0) {
        int adc = analogRead(10);  // A0 = GPIO 10
        int tempC = (adc * 3300) / 4095 / 10;  // LM35: 10mV/°C

        txBuffer[0] = (tempC >> 8) & 0xFF;
        txBuffer[1] = tempC & 0xFF;
        txLen = 2;

        sendUplink(txBuffer, txLen);
        return;
    }

    // Si no, comportamiento normal (parseo hex)
    txLen = parseHexString(line, txBuffer, MAX_PAYLOAD);
    // ...
}
```

---

## Como agregar downlink (recibir comandos)

```cpp
static bool sendUplink(const uint8_t* data, size_t len) {
    uint8_t downBuf[64];
    size_t downLen = 0;

    int state = node.sendReceive(data, len, 1, false, NULL, NULL, downBuf, &downLen);

    if (state > 0) {
        // Hay downlink! Procesar comando
        if (downBuf[0] == 0x01) {
            digitalWrite(LED_BUILTIN, HIGH);  // Encender LED
        } else if (downBuf[0] == 0x00) {
            digitalWrite(LED_BUILTIN, LOW);   // Apagar LED
        }
    }
    // ...
}
```

---

## API de RadioLib utilizada

### `int SX1262::begin()`

Inicializa el modulo SX1262. Retorna `RADIOLIB_ERR_NONE` en exito.

### `int LoRaWANNode::beginOTAA(uint64_t joinEui, uint64_t devEui, uint8_t* nwkKey, uint8_t* appKey)`

Configura el nodo en modo OTAA.

### `int LoRaWANNode::activateOTAA()`

Ejecuta join OTAA. Retorna `RADIOLIB_LORAWAN_NEW_SESSION` en exito.

### `bool LoRaWANNode::isActivated()`

Retorna `true` si la sesion LoRaWAN esta activa.

### `int LoRaWANNode::sendReceive(const uint8_t* dataUp, size_t lenUp, uint8_t fPort, bool isConfirmed, LoRaWANEvent_t*, LoRaWANEvent_t*, uint8_t* dataDown, size_t* lenDown)`

Envia uplink y escucha downlink. Retorna 0 si solo uplink, >0 si hay downlink, <0 si error.

### `void LoRaWANNode::setSleepFunction(RadioLibTime_t (*f)(RadioLibTime_t))`

Reemplaza la funcion de delay interna de RadioLib.

### `uint32_t LoRaWANNode::getDevAddr()`

Retorna el DevAddr asignado por el NS.

---

## Estructura de archivos del proyecto

```
CubeCell-LoRaWAN-Node/
├── CubeCell-LoRaWAN-Node.ino    # Firmware principal (v1.2)
├── README.md                     # Documentacion de usuario
├── .gitignore
└── docs/
    ├── datasheet.md              # Especificaciones tecnicas
    └── developer.md              # Esta guia para desarrolladores
```

---

## Workaround del timer ASR6502

### El problema

El core CubeCell v1.4.0 incluye `CubeCellLib.a` precompilada para **ASR6501**. En el **ASR6502**, los timers RTC escalan ~100× para valores cortos, causando:
- Join Request cerrado antes de recibir Join Accept.
- Timers internos (TxTimer, RxTimer) expiran prematuramente.

### La solucion (aplicada en v1.1 y v1.2)

1. **RadioLib 7.7.1** — stack LoRaWAN independiente que no usa CubeCellLib.a.
2. **`node.setSleepFunction(radioSleepDelay)`** — reemplaza delay() por millis().
3. **`node.scanGuard = 200`** — extiende ventana de escaneo del Join Accept.

### Precision de millis() en CubeCell

En el ASR6502, `millis()` se basa en el **RTC WCO de 32.768 kHz**, no en el clock HF de 48 MHz. Esto lo hace inherentemente preciso (±20 ppm), mientras que `delay()` usa SysTick que escala mal.

---

## Version 1.3 (planificado)

Mejoras previstas para futuras versiones:

- [ ] Deep sleep con RTC para consumo < 10 µA entre comandos
- [ ] Cache de sesion en NVRAM para evitar rejoin tras reset
- [ ] Soporte para comandos especiales (ej: `/bateria`, `/temp`, `/help`)
- [ ] Puerto serie configurable por comando
- [ ] Indicacion LED de estado (join, envio, error)
- [ ] Soporte para ABP como respaldo
- [ ] FPort configurable por comando
