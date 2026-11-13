# Documentacion para Desarrolladores — CubeCell DHT11 LoRaWAN Sensor

Guia tecnica interna del firmware del nodo sensor CubeCell DHT11 LoRaWAN. Explica la arquitectura del codigo, la maquina de estados, cada funcion y como modificar o extender el proyecto.

---

## Tabla de Contenidos

1. [Arquitectura del Codigo](#arquitectura-del-codigo)
2. [Estructura de Archivos](#estructura-de-archivos)
3. [Maquina de Estados LoRaWAN](#maquina-de-estados-lorawan)
4. [Diagrama de Flujo de la Logica Principal](#diagrama-de-flujo-de-la-logica-principal)
5. [Funciones API — Descripcion Exhaustiva](#funciones-api--descripcion-exhaustiva)
6. [Protocolo DHT11 Bit-Bang — Explicacion Detallada](#protocolo-dht11-bit-bang--explicacion-detallada)
7. [Configuracion del Framework LoRaWAN](#configuracion-del-framework-lorawan)
8. [Guia de Modificacion y Extension](#guia-de-modificacion-y-extension)
9. [Como Agregar Nuevos Sensores/Actuadores](#como-agregar-nuevos-sensoresactuadores)
10. [Optimizacion de Consumo Energetico](#optimizacion-de-consumo-energetico)
11. [Directrices de Contribucion](#directrices-de-contribucion)
12. [Referencias para Desarrolladores](#referencias-para-desarrolladores)

---

## Arquitectura del Codigo

### Paradigma y patron de diseno

El firmware sigue el **patron de Maquina de Estados** integrado en el framework `LoRaWan_APP.h` de Heltec CubeCell. El framework proporciona una maquina de estados predefinida con 6 estados, y el sketch del usuario implementa las acciones para cada transicion.

```
┌─────────────────────────────────────────────────────────────────┐
│                     Framework LoRaWan_APP                        │
│                                                                  │
│  ┌────────────┐   ┌────────────┐   ┌────────────┐               │
│  │  setup()   │──▶│  loop()    │◄──│ Interrups  │               │
│  │  (usuario) │   │  (usuario) │   │ (internas) │               │
│  └────────────┘   └─────┬──────┘   └────────────┘               │
│                         │                                        │
│                         ▼                                        │
│  ┌──────────────────────────────────────────────────────┐       │
│  │              Maquina de Estados                       │       │
│  │                                                       │       │
│  │  INIT ──▶ JOIN ──▶ SEND ──▶ CYCLE ──▶ SLEEP          │       │
│  │    ▲                                    │             │       │
│  │    └────────────────────────────────────┘             │       │
│  │              (wakeup del RTC)                         │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
│  ┌──────────────────────────────────────────────────────┐       │
│  │       Funciones Auxiliares (usuario)                  │       │
│  │                                                       │       │
│  │  prepareTxFrame()   <- llamado por SEND              │       │
│  │  leerDHT11()        <- lectura del sensor             │       │
│  │  leerDHT11_raw()    <- protocolo bit-bang             │       │
│  │  dht11ReadByte()    <- lectura de 8 bits              │       │
│  │  dht11WaitFor()     <- espera de flanco con timeout   │       │
│  │  generarRandom()    <- numero pseudoaleatorio         │       │
│  └──────────────────────────────────────────────────────┘       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Flujo de ejecucion

1. **Cold boot / Power-on reset:**
   - `setup()` se ejecuta UNA sola vez.
   - Configura GPIOs, semilla aleatoria, verifica sesion LoRaWAN guardada.
   - Inicializa `deviceState = DEVICE_STATE_INIT`.

2. **Ciclo principal (loop):**
   - `loop()` es llamado repetidamente por el framework.
   - Un `switch(deviceState)` dirige la ejecucion al estado actual.
   - La maquina avanza: `INIT -> JOIN -> SEND -> CYCLE -> SLEEP`.
   - Tras `SLEEP`, el RTC despierta al MCU y `loop()` se reanuda en el estado `SEND` (el framework preserva `deviceState` en RTC memory).

3. **Deep sleep:**
   - `LoRaWAN.sleep()` apaga el MCU (modo STOP) y configura el RTC para wakeup.
   - Al despertar, la ejecucion se reanuda en `loop()`.
   - El framework preserva las variables clave (`deviceState`, contextos de sesion LoRaWAN) en RTC memory o Flash emulada.

---

## Estructura de Archivos

```
PROYECTO_DIR/
│
├── cubecell-dht11-lora.ino    # Sketch principal (407 lineas)
│   ├── Includes y configuracion (lineas 1-89)
│   │   ├── #include "LoRaWan_APP.h"
│   │   ├── #include "Arduino.h"
│   │   ├── #include "credentials.h"
│   │   ├── Configuracion LoRaWAN (canales, region, clase, etc.)
│   │   ├── Constantes del DHT11 (pines, timeouts, debug)
│   │   └── Prototipos de funciones
│   │
│   ├── prepareTxFrame() (lineas 95-144)
│   │   └── Construye payload de 4 bytes para transmision
│   │
│   ├── setup() (lineas 150-185)
│   │   └── Inicializacion unica: GPIOs, semilla random, LoRaWAN
│   │
│   ├── loop() (lineas 191-254)
│   │   └── Maquina de estados: INIT, JOIN, SEND, CYCLE, SLEEP
│   │
│   ├── leerDHT11() y auxiliares (lineas 256-393)
│   │   ├── leerDHT11_raw()    — protocolo bit-bang completo
│   │   ├── leerDHT11()        — wrapper con power gating y checksum
│   │   ├── dht11ReadByte()    — lectura de 8 bits del DHT11
│   │   └── dht11WaitFor()     — espera de flanco con timeout
│   │
│   └── generarRandom() (lineas 395-407)
│       └── Genera uint16_t pseudoaleatorio
│
├── credentials.h              # Credenciales LoRaWAN OTAA (placeholders)
│   ├── devEui[8]
│   ├── appEui[8]
│   ├── appKey[16]
│   └── Claves ABP (no usadas; placeholders)
│
├── .gitignore                 # Exclusiones de versionado
│   ├── build/, .arduino/
│   ├── *.elf, *.hex, *.bin
│   ├── __pycache__/, *.pyc
│   ├── credentials.h (NO versionar con valores reales)
│   └── PLAN.md, AUDIT_REPORT.md
│
├── README.md                  # Documentacion de usuario
│
└── docs/
    ├── datasheet.md           # Hoja de datos del producto
    └── developer.md           # Este archivo — docs para developers
```

---

## Maquina de Estados LoRaWAN

### Estados definidos por el framework

El framework `LoRaWan_APP.h` define 6 estados en el enum `DeviceState_t`:

| Estado | Constante | Proposito |
|---|---|---|
| 0 | `DEVICE_STATE_INIT` | Inicializacion del dispositivo y del stack LoRaWAN |
| 1 | `DEVICE_STATE_JOIN` | Proceso de autenticacion OTAA (o ABP) |
| 2 | `DEVICE_STATE_SEND` | Construccion y transmision del payload |
| 3 | `DEVICE_STATE_CYCLE` | Programacion del siguiente ciclo de transmision |
| 4 | `DEVICE_STATE_SLEEP` | Entrada en deep sleep con RTC wakeup |
| 5 | `DEVICE_STATE_RX` | Recepcion de downlink (no usado en este firmware) |

### Transiciones de estado

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
  ┌────────────────────────────────────────────────────┐  │
  │                                                    │  │
  │  ┌──────┐    ┌──────┐    ┌──────┐    ┌───────┐    │  │
  │  │ INIT │───▶│ JOIN │───▶│ SEND │───▶│ CYCLE │    │  │
  │  └──────┘    └──────┘    └──────┘    └───┬───┘    │  │
  │      ▲                                   │        │  │
  │      │                                   ▼        │  │
  │      │                              ┌────────┐    │  │
  │      │           RTC wakeup         │ SLEEP  │    │  │
  │      └──────────────────────────────│(10 min)│────┘  │
  │         (si session perdida)        └────────┘       │
  │                                                      │
  └──────────────────────────────────────────────────────┘
```

### Descripcion de cada estado

#### DEVICE_STATE_INIT (linea 195-207)

Solo se ejecuta en el primer arranque (cold boot). Inicializa el stack LoRaWAN.

```cpp
case DEVICE_STATE_INIT:
    // 1. Si LORAWAN_DEVEUI_AUTO esta habilitado, genera DevEUI
    //    a partir del Chip ID unico del ASR6501
    LoRaWAN.generateDeveuiByChipID();

    // 2. Si AT_SUPPORT esta habilitado, lee parametros de
    //    configuracion via comandos AT (no usado aqui)
    getDevParam();

    // 3. Imprime parametros de dispositivo (debug)
    printDevParam();

    // 4. Inicializa el stack LoRaWAN con clase y region
    LoRaWAN.init(loraWanClass, loraWanRegion);

    // 5. Transiciona a JOIN
    deviceState = DEVICE_STATE_JOIN;
    break;
```

#### DEVICE_STATE_JOIN (linea 209-213)

Gestiona el proceso de autenticacion OTAA. El framework maneja los reintentos internamente.

```cpp
case DEVICE_STATE_JOIN:
    // Intenta unirse a la red LoRaWAN.
    // Si ya hay sesion guardada (ifskipjoin en setup),
    // el framework salta este estado automaticamente.
    LoRaWAN.join();
    // El framework cambia deviceState a SEND cuando JOIN es exitoso,
    // o reintenta si falla (gestionado internamente).
    break;
```

> **Nota:** `LoRaWAN.ifskipjoin()` en `setup()` verifica si hay una sesion OTAA guardada en flash/RTC. Si existe, el framework salta directamente a `DEVICE_STATE_SEND`, evitando el re-join. Esto es crucial para la autonomia: un OTAA join puede tomar 5-15 segundos con la radio encendida.

#### DEVICE_STATE_SEND (linea 215-221)

Construye el payload y lo transmite.

```cpp
case DEVICE_STATE_SEND:
    // 1. Construir payload (lectura DHT11 + random)
    prepareTxFrame(appPort);

    // 2. Transmitir via LoRaWAN (unconfirmed uplink, puerto 1)
    LoRaWAN.send();

    // 3. Transicionar a CYCLE
    deviceState = DEVICE_STATE_CYCLE;
    break;
```

#### DEVICE_STATE_CYCLE (linea 223-230)

Programa el siguiente ciclo de transmision.

```cpp
case DEVICE_STATE_CYCLE:
    // Calcular tiempo hasta el siguiente TX:
    //   appTxDutyCycle (600,000 ms) + jitter aleatorio
    txDutyCycleTime = appTxDutyCycle + randr(0, APP_TX_DUTYCYCLE_RND);

    // Programar el ciclo en el framework
    LoRaWAN.cycle(txDutyCycleTime);

    // Transicionar a SLEEP
    deviceState = DEVICE_STATE_SLEEP;
    break;
```

El jitter aleatorio (`randr()`) es importante para evitar colisiones sistematicas entre multiples nodos que arrancaron al mismo tiempo. Sin jitter, dos nodos con intervalo de 10 minutos que arrancaron simultaneamente transmitirian siempre a la vez, colisionando en cada ciclo.

#### DEVICE_STATE_SLEEP (linea 232-246)

Prepara el dispositivo para deep sleep y entra en modo de ultra-bajo consumo.

```cpp
case DEVICE_STATE_SLEEP:
    // 1. Debug: indicar duracion del sleep
    Serial.print("Deep sleep ");
    Serial.print(txDutyCycleTime / 1000);
    Serial.println(" segundos...");

    // 2. Apagar DHT11 explicitamente (redundante, pero seguro)
    digitalWrite(DHT11_POWER_PIN, LOW);

    // 3. Entrar en deep sleep.
    //    LoRaWAN.sleep() internamente:
    //      - Apaga la radio SX1262 (Radio.Sleep())
    //      - Configura RTC para wakeup en txDutyCycleTime ms
    //      - Apaga perifericos no esenciales
    //      - Entra en modo STOP del ASR6501 (~4 uA)
    LoRaWAN.sleep();
    break;
```

Tras `LoRaWAN.sleep()`, el MCU entra en deep sleep. Al expirar el timer RTC, el MCU despierta y la ejecucion se reanuda en `loop()`. El framework restaura `deviceState` a `DEVICE_STATE_SEND` (el estado siguiente a CYCLE es SEND, no INIT).

---

## Diagrama de Flujo de la Logica Principal

### Flujo completo de un ciclo

```
                        ┌─────────────────┐
                        │   POWER ON /    │
                        │   RESET         │
                        └────────┬────────┘
                                 │
                                 ▼
                        ┌─────────────────┐
                        │    setup()      │
                        │                 │
                        │ .pinMode(GPIOs) │
                        │ .randomSeed()   │
                        │ .ifskipjoin()   │
                        │ deviceState =   │
                        │   INIT          │
                        └────────┬────────┘
                                 │
                                 ▼
              ╔══════════════════════════════════╗
              ║         loop()                   ║
              ║  ┌───────────────────────────┐   ║
              ║  │ switch(deviceState)       │   ║
              ║  └───────────────────────────┘   ║
              ╚══════════════════════════════════╝
                                 │
                ┌────────────────┼────────────────┐
                ▼                ▼                ▼
        ┌──────────┐    ┌──────────────┐   ┌──────────────┐
        │  INIT    │    │  JOIN        │   │  SEND        │
        │          │    │              │   │              │
        │.init()   │───▶│.join()       │──▶│prepareTxFrame│
        │          │    │              │   │              │
        └──────────┘    └──────────────┘   │.send()       │
                                           └──────┬───────┘
                                                  │
                                                  ▼
                                           ┌──────────────┐
                                           │  CYCLE       │
                                           │              │
                                           │txDutyCycleTime
                                           │= 600s + randr│
                                           │              │
                                           │.cycle(time)  │
                                           └──────┬───────┘
                                                  │
                                                  ▼
                                           ┌──────────────┐
                                           │  SLEEP       │
                                           │              │
                                           │GPIO3 = LOW   │
                                           │.sleep()      │
                                           │              │
                                           │  ~4 uA x     │
                                           │  600 seg     │
                                           │              │
                                           │ RTC wakeup   │
                                           └──────┬───────┘
                                                  │
                                                  │ (deviceState preservado
                                                  │  en RTC memory)
                                                  │
                                                  ▼
                                           ┌──────────────┐
                                           │  SEND        │
                                           │  (siguiente  │
                                           │   ciclo)     │
                                           └──────────────┘
```

### Flujo detallado de prepareTxFrame()

```
prepareTxFrame(port=1)
│
├── 1. leerDHT11() — primera lectura
│   │
│   ├── leerDHT11_raw(data[5])
│   │   │
│   │   ├── GPIO3 = HIGH (enciende DHT11)
│   │   ├── delay(1000 ms) (estabilizacion)
│   │   ├── START: GPIO2 OUTPUT LOW 20ms, HIGH 40us
│   │   ├── GPIO2 = INPUT
│   │   ├── dht11WaitFor(LOW, 200us)  — espera ACK LOW
│   │   ├── dht11WaitFor(HIGH, 200us) — espera ACK HIGH
│   │   ├── for i=0..4: dht11ReadByte(&data[i])
│   │   └── return true/false
│   │
│   ├── Si falso:
│   │   ├── GPIO3 = LOW (apaga DHT11 aunque haya error)
│   │   └── return TEMP_ERROR_VALUE (0xFFFF)
│   │
│   ├── GPIO3 = LOW (apaga DHT11 tras lectura exitosa)
│   ├── Verificar checksum: data[0]+data[1]+data[2]+data[3] == data[4]
│   ├── Si checksum invalido: return TEMP_ERROR_VALUE
│   └── Extraer temp: data[2] * 10 + data[3] (decimas de C)
│       return tempDecimas
│
├── 2. Si temperatura == TEMP_ERROR_VALUE:
│   └── leerDHT11() — reintento (una vez mas)
│
├── 3. generarRandom()
│   └── return random(65536) — uint16_t
│
├── 4. Construir payload (4 bytes):
│   appData[0] = random LSB
│   appData[1] = random MSB
│   appData[2] = temp LSB
│   appData[3] = temp MSB
│   appDataSize = 4
│
└── 5. Debug Serial (si SERIAL_DEBUG=1):
    Imprime random, temperatura, y payload en HEX
```

### Flujo detallado de dht11ReadByte()

```
dht11ReadByte(&data)
│
├── data = 0
├── for i = 0 to 7 (MSB primero):
│   │
│   ├── dht11WaitFor(LOW, 200us)
│   │   └── Espera el flanco de bajada que inicia cada bit
│   │       Si timeout -> return false
│   │
│   ├── dht11WaitFor(HIGH, 200us)
│   │   └── Espera el flanco de subida que inicia el pulso de datos
│   │       Si timeout -> return false
│   │
│   ├── delayMicroseconds(40)
│   │   └── Espera 40 us. En este punto:
│   │       - Bit 0: la linea ya ha vuelto a LOW (pulso de 26-28 us)
│   │       - Bit 1: la linea sigue HIGH (pulso de 70 us)
│   │
│   ├── data <<= 1
│   ├── if digitalRead(GPIO2) == HIGH:
│   │   └── data |= 1  (es un bit 1)
│   │
│   └── dht11WaitFor(LOW, 200us)
│       └── Espera a que el pulso termine (bit 0 ya termino;
│           bit 1 aun no). Si ya esta LOW, continua.
│
└── return true
```

---

## Funciones API — Descripcion Exhaustiva

### `void setup()`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 150-185

**Proposito:** Inicializacion unica al arranque o tras reset. Solo se ejecuta una vez.

**Flujo:**
1. Inicializa Serial a 115200 baud (si `SERIAL_DEBUG=1`).
2. Imprime banner de inicio.
3. Configura GPIO2 como INPUT (DHT11 DATA) y GPIO3 como OUTPUT LOW (DHT11 VCC apagado).
4. Inicializa semilla aleatoria con `randomSeed(analogRead(ADC))`.
5. Configura GPIO1, GPIO4, GPIO5 como `INPUT_PULLDOWN` para evitar fugas.
6. Configura Vext como OUTPUT LOW (apaga perifericos externos).
7. Establece `deviceState = DEVICE_STATE_INIT`.
8. Verifica sesion LoRaWAN guardada con `LoRaWAN.ifskipjoin()`.

**Llamada a:** Ninguna (invocada por el framework de Arduino).

**Efectos secundarios:** Configura GPIOs, inicializa PRNG, configura estado inicial.

---

### `void loop()`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 191-254

**Proposito:** Bucle principal. El framework llama a `loop()` repetidamente. Contiene la maquina de estados.

**Flujo:** `switch(deviceState)` con casos `INIT`, `JOIN`, `SEND`, `CYCLE`, `SLEEP`.

**Llamada a:** Ninguna (invocada por el framework de Arduino).

**Efectos secundarios:** Avanza la maquina de estados, transmite datos, entra en deep sleep.

---

### `static void prepareTxFrame(uint8_t port)`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 103-144

**Proposito:** Construye el payload de 4 bytes para transmision LoRaWAN. Es llamada por el framework automaticamente durante `DEVICE_STATE_SEND`, justo antes de `LoRaWAN.send()`.

**Parametros:**
- `port` (uint8_t): Puerto LoRaWAN de aplicacion (siempre 1 en este firmware).

**Variables globales modificadas:**
- `appData[]`: Buffer de payload del framework (max 242 bytes para US915).
- `appDataSize`: Tamano del payload (establecido a 4).

**Flujo:**
1. Llama a `leerDHT11()` para obtener temperatura en decimas de C.
2. Si falla (`TEMP_ERROR_VALUE`), reintenta una vez mas.
3. Llama a `generarRandom()` para obtener uint16_t pseudoaleatorio.
4. Construye `appData[]`:
   - `appData[0-1]`: random en little-endian.
   - `appData[2-3]`: temperatura en decimas de C en little-endian.
5. Establece `appDataSize = 4`.
6. Si `SERIAL_DEBUG=1`, imprime el payload en formato legible y hexadecimal.

**Llamada por:** `loop()` en estado `DEVICE_STATE_SEND`.

**Llamadas a:** `leerDHT11()`, `generarRandom()`.

**Ejemplo de payload generado:**
```
randomValue = 42789 (0xA725)
temperatura = 245 (24.5 C)
appData = {0x25, 0xA7, 0xF5, 0x00}
```

---

### `int16_t leerDHT11(void)`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 300-343

**Proposito:** Lee el sensor DHT11 con power gating automatico y verificacion de checksum. Retorna la temperatura en decimas de C.

**Retorno:**
- `int16_t`: Temperatura en decimas de C (ej: 245 = 24.5 C).
- `TEMP_ERROR_VALUE` (0xFFFF) si el sensor no responde o checksum invalido.

**Flujo:**
1. Llama a `leerDHT11_raw(data)` para obtener 5 bytes crudos del sensor.
2. Si `leerDHT11_raw()` retorna `false`:
   - Apaga DHT11 (`GPIO3 = LOW`).
   - Restaura GPIO2 a INPUT.
   - Imprime mensaje de error si `SERIAL_DEBUG=1`.
   - Retorna `TEMP_ERROR_VALUE`.
3. Apaga DHT11 (`GPIO3 = LOW`).
4. Restaura GPIO2 a INPUT.
5. Verifica checksum: `data[0]+data[1]+data[2]+data[3] == data[4]`.
6. Si checksum invalido: retorna `TEMP_ERROR_VALUE`.
7. Extrae temperatura: `data[2] * 10 + data[3]`.
   - `data[2]`: parte entera de la temperatura (0-50 para DHT11).
   - `data[3]`: parte decimal de la temperatura (0-9, aunque DHT11 solo tiene resolucion de 1 C, este byte es siempre 0 en teoria; en la practica puede variar).
8. Imprime resultado si `SERIAL_DEBUG=1`.
9. Retorna temperatura en decimas.

**Llamada por:** `prepareTxFrame()`.

**Llamadas a:** `leerDHT11_raw()`.

**Manejo de errores:**
- Timeout del sensor -> `TEMP_ERROR_VALUE`.
- Checksum invalido -> `TEMP_ERROR_VALUE`.
- En ambos casos, GPIO3 se apaga para evitar consumo residual.

---

### `static bool leerDHT11_raw(uint8_t data[5])`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 274-298

**Proposito:** Implementa el protocolo one-wire completo del DHT11 a bajo nivel (bit-banging). Lee 5 bytes crudos del sensor.

**Parametros:**
- `data[5]` (uint8_t*): Buffer de salida para los 5 bytes leidos.
  - `data[0]`: Humedad parte entera.
  - `data[1]`: Humedad parte decimal.
  - `data[2]`: Temperatura parte entera.
  - `data[3]`: Temperatura parte decimal.
  - `data[4]`: Checksum.

**Retorno:**
- `true`: Lectura exitosa, `data[]` contiene los 5 bytes.
- `false`: Timeout en cualquier fase del protocolo.

**Flujo:**
1. Activa alimentacion del DHT11: `GPIO3 = HIGH`.
2. Espera `DHT11_STABILIZE_MS` (1000 ms) para estabilizacion.
3. Envia senal START:
   - Configura GPIO2 como OUTPUT.
   - `LOW` durante 20 ms (el DHT11 requiere > 18 ms).
   - `HIGH` durante 40 us.
   - Cambia GPIO2 a INPUT para leer la respuesta.
4. Lee ACK del DHT11:
   - Espera flanco LOW (80 us tipico).
   - Espera flanco HIGH (80 us tipico).
   - Si timeout en cualquiera -> `return false`.
5. Lee 5 bytes (40 bits) llamando a `dht11ReadByte()` para cada byte.
6. Si alguna lectura de byte falla -> `return false`.
7. `return true`.

**Llamada por:** `leerDHT11()`.

**Llamadas a:** `dht11WaitFor()`, `dht11ReadByte()`.

**Detalles del protocolo START:**
```
MCU (Host)                          DHT11 (Sensor)
    │                                     │
    │ GPIO2=LOW (20 ms)                   │
    │─────────────────────────────────────│
    │                                     │
    │ GPIO2=HIGH (40 us)                  │
    │────────────────                     │
    │                ┌─ GPIO2=INPUT       │
    │                │                    │
    │                │    ACK: LOW ~80 us │
    │                │◄───────────────────│
    │                │                    │
    │                │    ACK: HIGH ~80 us│
    │                │◄───────────────────│
    │                │                    │
    │                │    40 bits de datos│
    │                │◄───────────────────│
    │                │    (5 bytes)       │
```

---

### `bool dht11ReadByte(uint8_t *data)`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 351-376

**Proposito:** Lee un byte (8 bits) del DHT11 usando el protocolo one-wire.

**Parametros:**
- `data` (uint8_t*): Puntero al byte de salida.

**Retorno:**
- `true`: Byte leido correctamente.
- `false`: Timeout durante la lectura.

**Algoritmo de lectura de bits:**
```
Para cada bit (i = 0 a 7, MSB primero):

  1. Esperar flanco LOW (inicio del bit)
     dht11WaitFor(LOW, 200us)

  2. Esperar flanco HIGH (inicio del pulso de datos)
     dht11WaitFor(HIGH, 200us)

  3. Esperar 40 us
     delayMicroseconds(40)

  4. Leer el pin:
     - Si HIGH -> bit 1 (pulso de ~70 us, aun HIGH a los 40 us)
     - Si LOW  -> bit 0 (pulso de ~26-28 us, ya ha vuelto a LOW)

  5. Desplazar y acumular:
     data <<= 1
     if HIGH: data |= 1

  6. Esperar a que termine el pulso:
     dht11WaitFor(LOW, 200us)
```

**Diagrama de temporizacion de bits:**

```
Bit 0 (pulso corto):
    ┌──┐
    │  │ 26-28 us
────┘  └────────────────────  (flanco LOW de inicio)

    ^               ^
    |               |
    Inicio          Muestreo a 40 us: LOW -> bit 0
    del pulso

Bit 1 (pulso largo):
    ┌──────────────────┐
    │                  │ 70 us
────┘                  └────  (flanco LOW de inicio)

    ^               ^
    |               |
    Inicio          Muestreo a 40 us: HIGH -> bit 1
    del pulso
```

**Llamada por:** `leerDHT11_raw()`.

**Llamadas a:** `dht11WaitFor()`.

---

### `bool dht11WaitFor(uint8_t expected, uint32_t timeout_us)`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 382-393

**Proposito:** Bloquea la ejecucion hasta que el pin GPIO2 alcance el estado esperado, con un timeout en microsegundos.

**Parametros:**
- `expected` (uint8_t): Estado esperado (`LOW` = 0 o `HIGH` = 1).
- `timeout_us` (uint32_t): Tiempo maximo de espera en microsegundos.

**Retorno:**
- `true`: El pin alcanzo el estado esperado antes del timeout.
- `false`: Timeout expirado sin alcanzar el estado esperado.

**Implementacion:**
```cpp
bool dht11WaitFor(uint8_t expected, uint32_t timeout_us)
{
    uint32_t start = micros();

    while (digitalRead(DHT11_DATA_PIN) != expected) {
        if ((micros() - start) > timeout_us) {
            return false;
        }
    }

    return true;
}
```

**Consideraciones de robustez:**
- Usa `micros() - start` en lugar de `micros()` comparado con `start + timeout`. Esta forma es inmune al overflow de `micros()` (que ocurre cada ~71 minutos en Arduino/ARM).
- El timeout de 200 us es suficiente para el protocolo DHT11 (el pulso mas largo es ~80 us). Proporciona 120 us de margen para variaciones por bateria baja o temperatura.

**Llamada por:** `leerDHT11_raw()`, `dht11ReadByte()`.

---

### `uint16_t generarRandom(void)`

**Archivo:** `cubecell-dht11-lora.ino`, lineas 404-407

**Proposito:** Genera un numero pseudoaleatorio de 16 bits.

**Retorno:** `uint16_t` entre 0 y 65535.

**Implementacion:**
```cpp
uint16_t generarRandom(void)
{
    return (uint16_t)random(65536);
}
```

**Detalles de inicializacion:**
- La semilla se inicializa UNA sola vez en `setup()` con `randomSeed(analogRead(ADC))`.
- `analogRead(ADC)` lee un pin ADC no conectado, que captura ruido termico y electromagnetico, proporcionando entropia suficiente para la semilla.
- NO se re-semilla en cada ciclo. Esto permite que la secuencia PRNG avance naturalmente a traves de los ciclos de deep sleep, mejorando las propiedades estadisticas.
- El generador `random()` de Arduino es un LCG (Linear Congruential Generator) que es suficiente para este proposito (no se requiere aleatoriedad criptografica).

**Llamada por:** `prepareTxFrame()`.

---

## Protocolo DHT11 Bit-Bang — Explicacion Detallada

### ¿Por que bit-banging en lugar de una libreria?

1. **Elimina dependencias externas:** No requiere `DHT sensor library` de Adafruit ni ninguna otra libreria.
2. **Ahorra Flash:** ~2-3 KB de firmware que se usarian en la libreria.
3. **Control total de timeouts:** Las librerias genericas usan timeouts fijos pensados para AVR (16 MHz). En ARM Cortex-M0+ a 48 MHz, la temporizacion puede diferir.
4. **Compatibilidad garantizada:** No depende de que la libreria soporte el ASR6501.
5. **Auditable:** ~60 lineas de codigo vs cientos de lineas de una libreria de terceros.

### El protocolo DHT11 en detalle

El DHT11 usa un protocolo one-wire propietario (no confundir con Dallas OneWire / 1-Wire de Maxim). La comunicacion siempre es iniciada por el host (MCU).

#### Fase 1: START (host -> DHT11)

```
   MCU configura GPIO2 como OUTPUT
   
   GPIO2: ──────┐                 ┌──────────────────────────
                │                 │
                └─────────────────┘
                 
                |--- 20 ms ------|   |-- 40 us --|
                (LOW > 18ms)          (HIGH 20-40us)
                
   Luego: GPIO2 se configura como INPUT (pull-up interno del modulo DHT11
   mantiene la linea en HIGH)
```

#### Fase 2: ACK (DHT11 -> host)

```
   DHT11 responde:
   
   GPIO2: ──────┐         ┌─────────┐
                │  80 us  │  80 us  │
                └─────────┘         └──────────────────────
                
                |--- ACK LOW ------|--- ACK HIGH ---------|
                
   El host espera estos dos flancos con timeout de 200 us cada uno.
   Si no se detectan -> sensor ausente o defectuoso.
```

#### Fase 3: Datos (DHT11 -> host)

```
   40 bits = 5 bytes, MSB primero en cada byte:
   
   Byte 0: Humedad parte entera   (20-90 para DHT11)
   Byte 1: Humedad parte decimal  (siempre 0 en DHT11)
   Byte 2: Temperatura parte entera (0-50 para DHT11)
   Byte 3: Temperatura parte decimal (siempre 0 en DHT11)
   Byte 4: Checksum = byte0 + byte1 + byte2 + byte3
```

Cada bit se codifica como:

```
   Bit 0:
        ┌──┐
        │  │ 26-28 us
   ─────┘  └──────────────────
        |--|
        50 us LOW inicio
   
   Bit 1:
        ┌─────────────────────┐
        │                     │ 70 us
   ─────┘                     └───
        |--|
        50 us LOW inicio
```

#### Punto de muestreo optimo

El host muestrea el estado del pin a los **40 us** tras el flanco HIGH de inicio de cada bit:

- **Bit 0:** Pulso HIGH de 26-28 us. A los 40 us, el pin YA ha vuelto a LOW. El host lee LOW -> bit 0.
- **Bit 1:** Pulso HIGH de 70 us. A los 40 us, el pin SIGUE en HIGH. El host lee HIGH -> bit 1.

**Margenes de seguridad con timeout de 200 us:**

| Pulso | Duracion tipica | Timeout | Margen |
|---|---|---|---|
| ACK LOW | 80 us | 200 us | 120 us |
| ACK HIGH | 80 us | 200 us | 120 us |
| Bit start LOW | 50 us | 200 us | 150 us |
| Bit 0 HIGH | 26-28 us | 200 us | 172-174 us |
| Bit 1 HIGH | 70 us | 200 us | 130 us |

Con bateria baja (2.5 V), los flancos pueden ralentizarse ~20-30%, pero aun asi quedan dentro del margen de 200 us.

---

## Configuracion del Framework LoRaWAN

### Variables globales de configuracion

```cpp
// Canales US915: sub-banda 1, canales 0-7
// Frecuencias: 902.3, 902.5, 902.7, 902.9, 903.1, 903.3, 903.5, 903.7 MHz
uint16_t userChannelsMask[6] = { 0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };

// Region LoRaWAN desde configuracion de Arduino IDE Tools
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

// Clase A (minimo consumo)
DeviceClass_t loraWanClass = LORAWAN_CLASS;

// Intervalo de transmision: 600,000 ms = 10 minutos
uint32_t appTxDutyCycle = 600000;

// Activacion OTAA (configurado en Tools)
bool overTheAirActivation = LORAWAN_NETMODE;

// ADR deshabilitado (configurado en Tools)
bool loraWanAdr = LORAWAN_ADR;

// Preservar sesion de red tras deep sleep (NO re-join)
bool keepNet = LORAWAN_NET_RESERVE;

// Unconfirmed uplink (sin confirmacion del servidor)
bool isTxConfirmed = LORAWAN_UPLINKMODE;

// Puerto de aplicacion
uint8_t appPort = 1;

// Reintentos para mensajes confirmados (solo si isTxConfirmed=true)
uint8_t confirmedNbTrials = 4;
```

### Variables del framework (definidas en LoRaWan_APP.h)

| Variable | Tipo | Descripcion |
|---|---|---|
| `deviceState` | `DeviceState_t` | Estado actual de la maquina de estados |
| `appData[]` | `uint8_t[242]` | Buffer de payload para uplink |
| `appDataSize` | `uint8_t` | Tamano del payload en bytes |
| `txDutyCycleTime` | `uint32_t` | Tiempo hasta la siguiente transmision (ms) |
| `devEui[]` | `uint8_t[8]` | Device EUI (de `credentials.h`) |
| `appEui[]` | `uint8_t[8]` | Application EUI / JoinEUI |
| `appKey[]` | `uint8_t[16]` | Application Key |

---

## Guia de Modificacion y Extension

### Cambiar el intervalo de transmision

Editar la constante en `cubecell-dht11-lora.ino`, linea 43:

```cpp
// De 10 minutos (600,000 ms)
uint32_t appTxDutyCycle = 600000;

// A 5 minutos (300,000 ms)
uint32_t appTxDutyCycle = 300000;

// A 1 minuto (60,000 ms) — NO recomendado para bateria,
// reduciria la autonomia a ~0.5 anos
uint32_t appTxDutyCycle = 60000;
```

> **Impacto en autonomia:** Reducir el intervalo aumenta el consumo medio proporcionalmente. Con 5 minutos, la autonomia se reduce a la mitad (~2.6 anos). Con 1 minuto, la autonomia baja a ~0.5 anos.

### Cambiar la region LoRaWAN

En Arduino IDE: **Tools > LoRaWAN Region** o via arduino-cli:

```bash
arduino-cli compile \
  --fqbn "heltec_cubecell:cubecell:HTCC-AB02A:LORAWAN_REGION=REGION_EU868" \
  .
```

Para cambiar los canales por defecto, modificar `userChannelsMask`:

```cpp
// US915 sub-banda 1 (canales 0-7) — por defecto
uint16_t userChannelsMask[6] = { 0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };

// US915 sub-banda 2 (canales 8-15)
uint16_t userChannelsMask[6] = { 0xFF00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };

// US915 todos los canales (0-63) — NO recomendado, mayor tiempo de escaneo
uint16_t userChannelsMask[6] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
```

### Cambiar de OTAA a ABP

1. En Arduino IDE Tools:
   - `LORAWAN_NETMODE=LORAWAN_ABP`
2. Rellenar `nwkSKey[]`, `appSKey[]`, `devAddr` en `credentials.h` con los valores del Network Server.
3. Establecer `keepNet = false` (ABP no necesita persistencia de sesion).

> **ADVERTENCIA:** ABP no es recomendado para dispositivos de largo plazo. Los frame counters no se sincronizan tras un reset, pudiendo causar que el Network Server rechace frames.

### Habilitar ADR (Adaptive Data Rate)

En Arduino IDE Tools: `LORAWAN_ADR=ADR_ON`. Esto permite que el Network Server ajuste el SF automaticamente segun las condiciones de senal. Ventaja: mejor cobertura. Desventaja: mayor consumo si el servidor sube el SF.

### Anadir humedad al payload

Actualmente el firmware solo transmite temperatura. Para anadir humedad (aumentando el payload a 6 bytes):

```cpp
static void prepareTxFrame(uint8_t port)
{
    int16_t temperatura = leerDHT11();
    if (temperatura == TEMP_ERROR_VALUE) {
        temperatura = leerDHT11();
    }

    uint16_t humedad = leerHumedadDHT11();  // Necesitas modificar leerDHT11()

    uint16_t randomValue = generarRandom();

    // Payload de 6 bytes: random(2B) + temp(2B) + hum(2B)
    appDataSize = 6;
    appData[0] = (uint8_t)(randomValue & 0xFF);
    appData[1] = (uint8_t)((randomValue >> 8) & 0xFF);
    appData[2] = (uint8_t)(temperatura & 0xFF);
    appData[3] = (uint8_t)((temperatura >> 8) & 0xFF);
    appData[4] = (uint8_t)(humedad & 0xFF);
    appData[5] = (uint8_t)((humedad >> 8) & 0xFF);
}
```

Para extraer humedad de `leerDHT11_raw()`: `data[0] * 10 + data[1]` (humedad en decimas de %).

### Anadir monitoreo de voltaje de bateria

```cpp
// En setup():
pinMode(GPIO0, INPUT);  // VBAT_ADC_CTL

// Nueva funcion:
uint16_t leerBateria(void) {
    // El ADC del ASR6501 es de 12 bits (0-4095)
    // VBAT pasa por un divisor resistivo interno.
    // Consultar datasheet para el factor de conversion exacto.
    uint16_t raw = analogRead(ADC);
    // Conversion aproximada (requiere calibracion):
    // Vbat = raw * (3.3 / 4095) * factor_divisor
    return raw;
}
```

---

## Como Agregar Nuevos Sensores/Actuadores

### Agregar un sensor I2C (ej: BME280)

1. **Conectar el sensor:**
   - VCC -> Vext (o GPIO controlado para power gating)
   - GND -> GND
   - SDA -> GPIO4 (SDA)
   - SCL -> GPIO5 (SCL)

2. **Anadir libreria:**
   ```bash
   arduino-cli lib install "Adafruit BME280 Library"
   ```

3. **Modificar el codigo:**
   ```cpp
   #include <Wire.h>
   #include <Adafruit_BME280.h>

   Adafruit_BME280 bme;

   void setup() {
       // ... existente ...
       pinMode(Vext, OUTPUT);
       digitalWrite(Vext, HIGH);  // Encender perifericos I2C
       delay(100);

       Wire.begin(GPIO4, GPIO5);  // SDA, SCL
       if (!bme.begin(0x76)) {
           Serial.println("BME280 no encontrado!");
       }
       digitalWrite(Vext, LOW);   // Apagar para ahorrar
   }

   void leerBME280() {
       digitalWrite(Vext, HIGH);  // Encender
       delay(100);                // Estabilizacion
       float temp = bme.readTemperature();
       float hum = bme.readHumidity();
       digitalWrite(Vext, LOW);
   }
   ```

### Agregar un actuador (ej: rele para control)

1. **Conectar el rele:**
   - Control -> GPIO1 (pin libre, requiere reconfiguracion)
   - Usar un transistor/mosfet si el rele consume > 8 mA

2. **Modificar el codigo:**
   ```cpp
   #define RELE_PIN GPIO1

   void setup() {
       // ... existente ...
       pinMode(RELE_PIN, OUTPUT);
       digitalWrite(RELE_PIN, LOW);  // Rele apagado
   }
   ```

3. **Control via downlink LoRaWAN (requiere habilitar RX):**

   ```cpp
   // Esto requiere confirmed uplinks y procesar downlinks,
   // lo que aumenta el consumo significativamente.
   // Para aplicaciones de solo uplink como este proyecto,
   // no es recomendable anadir actuadores.
   ```

### Agregar un sensor analogico (ej: LDR, humedad de suelo)

```cpp
#define SENSOR_ANALOG_PIN GPIO0  // ADC

void setup() {
    pinMode(SENSOR_ANALOG_PIN, INPUT);
}

uint16_t leerSensorAnalogico() {
    // El ADC del ASR6501 es de 12 bits (0-4095)
    return analogRead(ADC);
}
```

---

## Optimizacion de Consumo Energetico

### Checklist para desarrolladores

Si estas modificando el codigo, verifica estos puntos para no degradar la autonomia:

- [ ] **No anadir `delay()` en `loop()`:** La maquina de estados ya gestiona el tiempo. Un `delay()` en `loop()` mantiene el MCU despierto consumiendo ~5-10 mA.
- [ ] **Apagar perifericos antes de dormir:** Si anades sensores I2C, apagar Vext con `digitalWrite(Vext, LOW)`.
- [ ] **No usar `String`:** Causa fragmentacion de heap. Usar `char[]` o `F()` macro para strings.
- [ ] **No re-semillar `random()`:** La semilla actual se pone en `setup()` y `random()` avanza naturalmente.
- [ ] **Mantener `keepNet = true`:** Evita re-join OTAA en cada ciclo.
- [ ] **Mantener unconfirmed uplinks:** No esperar RX1/RX2 ahorra ~1-2s de radio encendida por ciclo.
- [ ] **Configurar GPIOs no usados:** Todos como `INPUT_PULLDOWN`. Un pin flotante puede consumir ~100 uA.
- [ ] **No habilitar Serial en produccion:** `SERIAL_DEBUG=0` ahorra ~0.5-1 mA.
- [ ] **Evitar confirmed uplinks:** `isTxConfirmed = false`. Los confirmed requieren RX1/RX2.
- [ ] **Mantener SF bajo:** SF7 tiene el menor time-on-air (~46 ms para 4 bytes). Si usas ADR, el servidor puede subir el SF si la senal es debil.

### Medicion de consumo

Para medir el consumo real durante el desarrollo:

```
   Bateria ER14250 (+)
        │
        ├──── Multimetro (modo uA) ──── VBAT (+) de la placa
        │
   Bateria ER14250 (-) ──── GND de la placa

   IMPORTANTE:
   - USB DESCONECTADO (el USB suministra 5V que enmascara el consumo de bateria)
   - Esperar a que el dispositivo entre en deep sleep (> 2 segundos tras TX)
   - La lectura en deep sleep debe ser < 20 uA (ideal < 10 uA)
   - Durante TX, la lectura debe ser ~30 mA (cambiar el multimetro a rango mA)
```

---

## Directrices de Contribucion

### Estilo de codigo

- **Idioma:** Codigo en ingles, comentarios en espanol (consistente con el proyecto).
- **Indentacion:** 4 espacios. No usar tabs.
- **Nombres:** camelCase para funciones y variables, UPPER_SNAKE_CASE para macros y constantes.
- **Comentarios:** Todas las funciones deben tener un comentario de cabecera explicando proposito, parametros y retorno.
- **Longitud de linea:** Maximo 100 caracteres.

### Antes de enviar cambios

1. Compilar sin errores:
   ```bash
   arduino-cli compile --fqbn heltec_cubecell:cubecell:HTCC-AB02A .
   ```

2. Verificar tamano de firmware:
   - Flash usado < 100 KB (de 128 KB disponibles).
   - RAM usado < 12 KB (de 16 KB disponibles).

3. Verificar que no se han introducido:
   - Nuevas librerias externas (sin justificacion).
   - `String` o `malloc`/`new` (fragmentacion de heap).
   - `delay()` bloqueantes en `loop()`.
   - GPIOs flotantes sin configurar.

4. Verificar power gating:
   - Todos los sensores deben apagarse antes de `LoRaWAN.sleep()`.
   - Verificar que GPIO3 se pone LOW en TODAS las ramas de `leerDHT11()` (incluyendo errores).

5. Actualizar documentacion:
   - Si se cambia el payload: actualizar `README.md`, `docs/datasheet.md`, y el decodificador JavaScript.
   - Si se cambian pines: actualizar la tabla de pinout.
   - Si se anaden funciones: documentarlas en `docs/developer.md`.

### Estructura de commits

```
feat: agregar soporte para sensor BME280
fix: corregir timeout DHT11 con bateria baja
docs: actualizar documentacion de payload
perf: optimizar secuencia de sleep para reducir consumo
refactor: extraer logica de power gating a funcion separada
```

---

## Referencias para Desarrolladores

| Recurso | URL |
|---|---|
| CubeCell Arduino Framework (GitHub) | [https://github.com/HelTecAutomation/CubeCell-Arduino](https://github.com/HelTecAutomation/CubeCell-Arduino) |
| LoRaWan_APP API (Heltec Docs) | [https://docs.heltec.org/en/node/asr650x/index.html](https://docs.heltec.org/en/node/asr650x/index.html) |
| ASR6501 Datasheet | Consultar documentacion de Heltec |
| SX1262 Datasheet | Semtech SX1261/2 |
| DHT11 Protocol Specification | [Mouser DHT11](https://www.mouser.com/datasheet/2/758/DHT11-Technical-Data-Sheet-Translated-Version-1143054.pdf) |
| LoRaWAN Specification 1.0.3 | [LoRa Alliance](https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-3/) |
| LoRaWAN Regional Parameters US915 | RP002-1.0.3 |
| Arduino CLI Docs | [https://arduino.github.io/arduino-cli/](https://arduino.github.io/arduino-cli/) |
| ASR650x Deep Sleep & Low Power | [Heltec CubeCell Docs](https://docs.heltec.org/en/node/asr650x/index.html) |

---

**Documentacion para desarrolladores — Version 1.0**

*Proyecto: CubeCell DHT11 LoRaWAN Sensor*
*Placa: Heltec HTCC-AB02A (ASR6501 Cortex-M0+ + SX1262)*
*Ultima actualizacion: 2026-08-05*
