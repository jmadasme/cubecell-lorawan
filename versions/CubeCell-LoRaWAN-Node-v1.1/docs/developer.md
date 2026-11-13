# Developer Guide — CubeCell LoRaWAN Node v1.1

## Arquitectura del firmware

El firmware es un sketch unico de Arduino (`CubeCell-LoRaWAN-Node.ino`) estructurado en las siguientes secciones:

```
 1. Includes y definiciones globales (RadioLib, pines)
 2. Credenciales OTAA (devEui, joinEui, appKey)
 3. Variables de configuracion (Region, subBand, payload, intervalo)
 4. Funcion workaround: radioSleepDelay()
 5. Funciones de diagnostico: debug(), stateDecode()
 6. setup() — inicializacion y join
 7. ensureJoined() — rejoin si se pierde sesion
 8. loop() — envio ciclico de payload
```

### Diagrama de flujo

```
                         ┌──────────────────┐
                         │   RESET / POWER   │
                         └────────┬─────────┘
                                  │
                                  v
                         ┌──────────────────┐
                         │     setup()       │
                         │                  │
                         │ 1. Serial.begin  │
                         │ 2. radio.begin() │
                         │ 3. beginOTAA()   │
                         │ 4. scanGuard=200 │
                         │ 5. activateOTAA()│
                         │ 6. Mostrar       │
                         │    payload       │
                         └────────┬─────────┘
                                  │
                                  v
                  ┌───────────────────────────────┐
                  │          loop()               │
                  │                               │
                  │  ┌─────────────────────────┐  │
                  │  │  ensureJoined()         │  │
                  │  │  Si no joined → rejoin  │  │
                  │  └───────────┬─────────────┘  │
                  │              │                │
                  │              v                │
                  │  ┌─────────────────────────┐  │
                  │  │  node.sendReceive()     │  │
                  │  │  Envia payload[]        │  │
                  │  └───────────┬─────────────┘  │
                  │              │                │
                  │              v                │
                  │  ┌─────────────────────────┐  │
                  │  │  radioSleepDelay(60s)   │  │
                  │  └───────────┬─────────────┘  │
                  │              │                │
                  └──────────────┘                │
                                                 │
                                                 v
                                        (se repite)
```

---

## Descripcion de funciones

### `void setup()`

Funcion principal de inicializacion. Se ejecuta una vez al encender o resetear la placa.

**Flujo interno:**
1. Inicializa Serial a 115200 baud.
2. Espera 3 segundos (para que el monitor serie se conecte).
3. Inicializa el modulo SX1262 via `radio.begin()`.
   - Si falla, entra en bucle infinito con mensaje de error.
4. Configura el nodo LoRaWAN en modo OTAA via `node.beginOTAA()`.
   - Pasa joinEui, devEui, nwkKey, appKey.
   - Si falla, entra en bucle infinito.
5. Aplica workaround:
   - `node.setSleepFunction(radioSleepDelay)` — reemplaza delay() interno por millis().
   - `node.scanGuard = 200` — aumenta ventana de escaneo de Join Accept.
6. Ejecuta `node.activateOTAA()` para iniciar el join.
   - El resultado se muestra en el monitor serie.
7. Muestra el payload que se enviara en la consola.

**No retorna valor.**

---

### `void loop()`

Funcion principal que se ejecuta ciclicamente.

**Flujo interno:**
1. Llama a `ensureJoined()` para verificar que la sesion LoRaWAN esta activa.
   - Si no hay sesion, espera 60 segundos y reintenta.
2. Envia el payload via `node.sendReceive(payload, payloadLen)`.
   - `payload[]` contiene los bytes definidos por el usuario.
   - `sendReceive` envia un uplink no confirmado y escucha downlink en RX1/RX2.
3. Procesa el resultado:
   - `RADIOLIB_ERR_NONE (0)`: uplink enviado, sin downlink.
   - `> 0`: se recibieron N bytes de downlink.
   - `< 0`: error (se muestra codigo y descripcion).
4. Espera `INTERVALO_MS` milisegundos (60s por defecto) usando `radioSleepDelay()`.

**No retorna valor.**

---

### `static bool ensureJoined(void)`

Verifica que el nodo tenga una sesion LoRaWAN activa y la restaura si es necesario.

**Retorna:**
- `true`: sesion activa (o re-join exitoso).
- `false`: no se pudo establecer sesion.

**Logica:**
1. Si `node.isActivated()` retorna `true`, la sesion esta activa → retorna `true`.
2. Si no, intenta `node.activateOTAA()` para re-join.
3. Si el re-join es exitoso (`RADIOLIB_LORAWAN_NEW_SESSION`), retorna `true`.
4. Si falla, retorna `false`.

---

### `static void radioSleepDelay(RadioLibTime_t ms)`

Workaround para reemplazar el `delay()` interno de RadioLib. En CubeCell, `delay()` usa el timer HF que escala incorrectamente. `millis()` usa el RTC WCO de 32.768 kHz y es preciso.

**Parametros:**
- `ms`: tiempo de espera en milisegundos.

**Funcionamiento:**
1. Si `ms == 0`, retorna inmediatamente.
2. Toma un timestamp inicial con `millis()`.
3. Hace un spin-loop hasta que `millis() - start >= ms`.
4. Durante el spin-loop, el CPU esta activo (no deep sleep).

**Nota:** Este metodo bloquea el CPU. No se usa deep sleep. Para bajo consumo, reemplazar por deep sleep con RTC.

---

### `static const char* stateDecode(int16_t s)`

Traduce codigos de estado de RadioLib a cadenas legibles.

**Parametros:**
- `s`: codigo de estado int16_t devuelto por RadioLib.

**Retorna:**
- Cadena de texto con el nombre del estado.

**Codigos manejados:**

| Codigo | Cadena | Significado |
|--------|--------|-------------|
| `RADIOLIB_ERR_NONE` | `"OK"` | Operacion exitosa |
| `RADIOLIB_ERR_CHIP_NOT_FOUND` | `"CHIP_NOT_FOUND"` | No se detecta el SX1262 |
| `RADIOLIB_ERR_PACKET_TOO_LONG` | `"PACKET_TOO_LONG"` | Payload excede maximo |
| `RADIOLIB_ERR_RX_TIMEOUT` | `"RX_TIMEOUT"` | Timeout en ventana de recepcion |
| `RADIOLIB_ERR_MIC_MISMATCH` | `"MIC_MISMATCH"` | MIC de downlink no coincide |
| `RADIOLIB_ERR_NETWORK_NOT_JOINED` | `"NOT_JOINED"` | Nodo no unido a la red |
| `RADIOLIB_ERR_NO_JOIN_ACCEPT` | `"NO_JOIN_ACCEPT"` | No se recibio Join Accept |
| `RADIOLIB_LORAWAN_NEW_SESSION` | `"NEW_SESSION"` | Nueva sesion establecida |
| `RADIOLIB_LORAWAN_SESSION_RESTORED` | `"SESSION_RESTORED"` | Sesion restaurada de persistencia |

---

### Macro `debug(fmt, ...)`

Macro de depuracion que envuelve `Serial.printf`.

**Uso:**
```cpp
debug("Valor del sensor: %d\n", lectura);
```

---

## Variables globales clave

| Variable | Tipo | Descripcion |
|----------|------|-------------|
| `mod` | `Module` | Configuracion de pines SPI del SX1262 (NSS=35, DIO1=38, RST=47, BUSY=39) |
| `radio` | `SX1262` | Instancia del driver SX1262 |
| `node` | `LoRaWANNode` | Nodo LoRaWAN (maneja sesion, join, uplink/downlink) |
| `joinEui` | `uint64_t` | Join EUI (AppEUI) para OTAA |
| `devEui` | `uint64_t` | Device EUI unico |
| `appKey` | `uint8_t[16]` | Application Key |
| `nwkKey` | `uint8_t[16]` | Network Key (igual a appKey en modo 1.0.x) |
| `Region` | `LoRaWANBand_t` | Banda regional (`AU915`) |
| `subBand` | `uint8_t` | Sub-banda (2: canales 8-15) |
| `payload[]` | `const uint8_t[]` | Bytes del payload a enviar |
| `payloadLen` | `const size_t` | Longitud del payload (bytes) |
| `INTERVALO_MS` | `const unsigned long` | Intervalo entre envios (ms) |

---

## Guia de modificacion y extension

### Cambiar el payload

Editar el arreglo `payload[]` en la parte superior del archivo:

```cpp
// Para enviar 2 bytes
const uint8_t payload[] = { 0xAA, 0xBB };
// Para enviar 8 bytes
const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
// Para enviar 1 byte
const uint8_t payload[] = { 0xFF };
```

`payloadLen` se calcula automaticamente con `sizeof(payload)`.

### Cambiar el intervalo de envio

Editar `INTERVALO_MS`:

```cpp
const unsigned long INTERVALO_MS = 300000; // 5 minutos
// Valores comunes:
//   60000   = 1 minuto
//   300000  = 5 minutos
//   600000  = 10 minutos
//   3600000 = 1 hora
```

### Cambiar la banda o sub-banda

```cpp
const LoRaWANBand_t Region = US915;  // Cambiar a US915, EU868, etc.
const uint8_t subBand = 1;           // Sub-banda segun corresponda
```

### Cambiar a modo confirmado

El envio actual es no confirmado. Para envio confirmado, usar `sendReceive` con el parametro `isConfirmed = true`:

```cpp
int state = node.sendReceive(payload, payloadLen, 1, true);
```

### Cambiar el puerto LoRaWAN (FPort)

Agregar el puerto como segundo parametro de `sendReceive`:

```cpp
int state = node.sendReceive(payload, payloadLen, 2); // FPort = 2
```

---

## Como agregar nuevos sensores/actuadores

### Sensor analogico (ADC)

Ejemplo: agregar lectura de un potenciometro en GPIO A0.

```cpp
// En la seccion de variables globales
const uint8_t SENSOR_PIN = 10;  // A0 = GPIO 10

// En setup(), configurar pin
pinMode(SENSOR_PIN, INPUT);

// Modificar payload en loop()
uint16_t lectura = analogRead(SENSOR_PIN); // 0-4095 (12 bits)
uint8_t customPayload[2];
customPayload[0] = (lectura >> 8) & 0xFF;
customPayload[1] = lectura & 0xFF;

int state = node.sendReceive(customPayload, sizeof(customPayload));
```

### Sensor digital (I2C)

Ejemplo: agregar sensor BMP280 (presion/temperatura).

```cpp
#include <Wire.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 bmp;

void setup() {
    // ... inicializacion existente ...
    Wire.begin();
    if (!bmp.begin(0x76)) {
        debug("BMP280 no detectado\n");
    }
}

void loop() {
    float temp = bmp.readTemperature();
    float pres = bmp.readPressure() / 100.0F;

    uint8_t customPayload[4];
    int16_t t = (int16_t)(temp * 10);    // °C × 10
    uint16_t p = (uint16_t)(pres * 10);  // hPa × 10

    customPayload[0] = (t >> 8) & 0xFF;
    customPayload[1] = t & 0xFF;
    customPayload[2] = (p >> 8) & 0xFF;
    customPayload[3] = p & 0xFF;

    debug("Temp: %.1f C, Pres: %.1f hPa\n", temp, pres);
    int state = node.sendReceive(customPayload, sizeof(customPayload));
    // ...
}
```

### Actuador (salida digital)

Ejemplo: recibir comando por downlink para encender un LED externo.

```cpp
void loop() {
    uint8_t downlinkData[16];
    size_t downlinkLen = 0;

    int state = node.sendReceive(payload, payloadLen, 1, false, NULL, NULL,
                                  downlinkData, &downlinkLen);
    if (state > 0) {
        // Se recibio downlink con 'state' bytes
        if (downlinkLen >= 1) {
            if (downlinkData[0] == 0x01) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
        }
    }
    // ...
}
```

**Nota:** La firma de `sendReceive` con buffer de downlink requiere pasar los punteros adicionales. Ver API de RadioLib.

---

## API de funciones (RadioLib utilizadas)

### `int SX1262::begin()`

Inicializa el modulo SX1262.

**Retorna:** `RADIOLIB_ERR_NONE` en exito, otro valor en error.

---

### `int LoRaWANNode::beginOTAA(uint64_t joinEui, uint64_t devEui, uint8_t* nwkKey, uint8_t* appKey)`

Configura el nodo LoRaWAN en modo OTAA.

**Parametros:**
- `joinEui`: Join EUI (AppEUI) de 8 bytes.
- `devEui`: Device EUI de 8 bytes.
- `nwkKey`: Network Key de 16 bytes.
- `appKey`: Application Key de 16 bytes.

**Retorna:** `RADIOLIB_ERR_NONE` en exito.

---

### `int LoRaWANNode::activateOTAA()`

Ejecuta el procedimiento de join OTAA: envia Join Request y espera Join Accept.

**Retorna:**
- `RADIOLIB_LORAWAN_NEW_SESSION`: join exitoso, nueva sesion.
- `RADIOLIB_ERR_NO_JOIN_ACCEPT`: no se recibio Join Accept.
- Otros codigos de error.

---

### `bool LoRaWANNode::isActivated()`

Verifica si el nodo tiene una sesion LoRaWAN activa.

**Retorna:** `true` si la sesion esta activa.

---

### `int LoRaWANNode::sendReceive(const uint8_t* dataUp, size_t lenUp, uint8_t fPort, bool isConfirmed)`

Envia un uplink y escucha downlink en las ventanas RX1/RX2.

**Parametros:**
- `dataUp`: puntero a los datos del payload.
- `lenUp`: longitud del payload.
- `fPort`: puerto LoRaWAN (1-223, por defecto 1).
- `isConfirmed`: si es `true`, el uplink es confirmado (el NS debe enviar ack).

**Retorna:**
- `RADIOLIB_ERR_NONE`: uplink enviado, sin downlink.
- `> 0`: numero de bytes recibidos en downlink.
- `< 0`: codigo de error.

---

### `void LoRaWANNode::setSleepFunction(RadioLibTime_t (*f)(RadioLibTime_t))`

Reemplaza la funcion de delay interna de RadioLib por una personalizada.

**Uso en este firmware:**
```cpp
node.setSleepFunction(radioSleepDelay);
```

---

### `uint32_t LoRaWANNode::getDevAddr()`

Retorna la direccion DevAddr asignada al nodo (despues del join).

---

## Estructura de archivos del proyecto

```
CubeCell-LoRaWAN-Node/
├── CubeCell-LoRaWAN-Node.ino    # Firmware principal (v1.1)
├── README.md                     # Documentacion de usuario
├── .gitignore                    # Ignora PLAN.md, AUDIT_REPORT.md
└── docs/
    ├── datasheet.md              # Especificaciones tecnicas y compatibilidad
    └── developer.md              # Esta guia para desarrolladores
```

---

## Notas sobre el workaround del timer

### El problema

El core CubeCell v1.4.0 incluye `CubeCellLib.a`, una libreria precompilada para **ASR6501** que escala los timeouts de LoRaWAN aproximadamente 100× cuando se ejecuta en **ASR6502**. Esto causa que:

- `LoRaWAN.join()` lance Join Request pero cierre RX antes de que llegue el Join Accept.
- Los timers internos (TxTimer, RxTimer, etc.) expiren prematuramente.

### La solucion (aplicada en v1.1)

1. **Usar RadioLib 7.7.1** — stack LoRaWAN independiente que no usa CubeCellLib.a.
2. **Reemplazar delay() por millis()** — `node.setSleepFunction(radioSleepDelay)`.
3. **Aumentar scanGuard** — `node.scanGuard = 200` (por defecto es 0 en AU915).
   - `scanGuard` extiende la ventana de escaneo del Join Accept para compensar pequenas diferencias en la sincronizacion del clock.

### Precision de millis() en CubeCell

En el ASR6502, `millis()` se basa en el **RTC WCO** (32.768 kHz), no en el clock HF interno. Esto lo hace inherentemente preciso, a diferencia de `delay()` que usa SysTick basado en el clock HF de 48 MHz que escala mal.

---

## Version 1.2 (planificado)

Mejoras previstas para la proxima version:

- [ ] Deep sleep con RTC para consumo < 10 µA entre envios
- [ ] Lectura real de bateria (ADC interno)
- [ ] Soporte para downlink con comandos
- [ ] Reconexion automatica si el NS cambia de canal
- [ ] Almacenamiento de sesion en NVRAM para evitar re-join en cada reset
- [ ] Ejemplo con sensor BMP280/BME280 integrado
- [ ] Soporte para FPort multiple
