# CubeCell Serial-to-LoRaWAN Bridge v1.2

**Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262)**  
**Region: AU915 sub-banda 2 | OTAA Clase A | RadioLib 7.7.1**

---

## Que hace este proyecto

Convierte la **Heltec CubeCell HTCC-AB01** en un **puente Serial-to-LoRaWAN**: escribes datos en hexadecimal desde un monitor serial y la CubeCell los envia **inmediatamente** al gateway **Milesight UG56**.

No necesita intervalos, ni timers, ni programacion previa de payloads.

---

## Flujo de funcionamiento

```
Tu PC                          CubeCell                          Gateway UG56
┌──────────┐                  ┌──────────────────┐              ┌──────────────┐
│ Escribes  │    USB-Serial   │                  │    LoRa      │              │
│ 01 01 01  │ ──────────────> │  Convierte HEX   │ ───────────> │  Recibe      │
│           │                 │  y lo envia      │              │  Payload     │
│ Ves la    │ <────────────── │  Inmediatamente  │              │  010101      │
│ respuesta │    USB-Serial   │                  │              │              │
└──────────┘                  └──────────────────┘              └──────┬───────┘
                                                                      │
                                                                      v
                                                              Publica por MQTT
                                                              Tu servidor decodifica
```

---

## Especificaciones de la placa

| Caracteristica | Valor |
|---|---|
| **Placa** | Heltec CubeCell HTCC-AB01 |
| **MCU** | ASR6502 (ARM Cortex M0+ @ 48 MHz) |
| **Radio LoRa** | SX1262 (integrado en el modulo ASR) |
| **Banda** | AU915, sub-banda 2 (canales 8-15: 916.8 - 918.2 MHz) |
| **Activacion** | OTAA (Over-The-Air Activation) |
| **Clase** | Class A |
| **Puerto USB** | USB-C (alimentacion y programa) |
| **Antena** | Conector U.FL para antena LoRa externa 868/915 MHz |
| **Baudrate serial** | 115200 baud, 8N1 |

---

## Forma de operar

### 1. Conectar fisicamente

```
┌─────────────────────────────────────────────────────────┐
│  1. Conecta la antena LoRa al conector U.FL             │
│  2. Conecta la CubeCell al PC via cable USB-C           │
│  3. La placa se alimenta por USB (no requiere bateria)  │
└─────────────────────────────────────────────────────────┘
```

### 2. Abrir terminal serial

#### Opcion A: cutecom (recomendado, interfaz grafica)

```bash
cutecom
```

Configuracion en la ventana:

| Campo | Valor |
|---|---|
| **Device** | `/dev/ttyUSB0` |
| **Baud Rate** | `115200` |
| **Data Bits** | `8` |
| **Stop Bits** | `1` |
| **Parity** | `None` |
| **Flow Control** | `None` |
| **DTR** | **Desmarcado** (importante: evita reset al abrir) |

Click **"Open"**. Presiona Enter hasta que aparezca `>`.

#### Opcion B: Arduino IDE Serial Monitor

1. Tools → Serial Monitor (o `Ctrl+Shift+M`)
2. Baud: **115200**
3. Line ending: **Newline**
4. Esperar que aparezca `>`

### 3. Enviar datos

Escribe **cualquier combinacion de bytes en hex** separados por espacios y presiona Enter:

| Ejemplo | Lo que escribe | Lo que recibe el gateway |
|---|---|---|
| 1 byte | `AA` | `Payload(hex): AA` |
| 4 bytes | `DE AD BE EF` | `Payload(hex): DEADBEEF` |
| 3 bytes | `01 01 01` | `Payload(hex): 010101` |
| 6 bytes | `A0 B1 C2 D3 E4 F5` | `Payload(hex): A0B1C2D3E4F5` |
| Sin espacios | `AABBCC` | `Payload(hex): AABBCC` |
| Mixto | `AA BBCC DD` | `Payload(hex): AABBCCDD` |

El firmware responde confirmando:

```
> 01 01 01           ← tu escribiste
>> Enviando: 01 01 01 → OK     ← confirmacion de envio
>                    ← listo para el siguiente comando
```

### 4. Verificar en el gateway

En la consola web del Milesight UG56, los paquetes aparecen como:

```
DevEUI: 24F124725F486022
FCnt: 0, 1, 2, ...
Payload(hex): 010101     ← los bytes que escribiste
Port: 1
RSSI: -39    SNR: 13.2
```

---

## Pinout (conexiones de la placa)

La CubeCell HTCC-AB01 no requiere componentes externos para este proyecto. Solo antena y USB.

### Pines del modulo SX1262 (conexion interna con el MCU)

| Pin SX1262 | GPIO ASR6502 | Funcion |
|------------|-------------|---------|
| NSS | **P4_3 (GPIO 35)** | Chip Select SPI |
| DIO1 | **P4_6 (GPIO 38)** | Interrupcion de radio |
| RESET | **P5_7 (GPIO 47)** | Reset del modulo |
| BUSY | **P4_7 (GPIO 39)** | Indicador de estado |

### Pines de expansion disponibles para sensores

| Pin Arduino / GPIO | Modo | Componente conectado | Notas |
|-------------------|------|---------------------|-------|
| D0 / GPIO 0 | DIGITAL | Libre | Disponible en header |
| D1 / GPIO 1 | DIGITAL | Libre | Disponible en header |
| D2 / GPIO 2 | DIGITAL | Libre | Disponible en header |
| D3 / GPIO 3 | DIGITAL | Libre | Disponible en header |
| A0 / GPIO 10 | ANALOG | Libre | ADC 12 bits disponible |
| A1 / GPIO 11 | ANALOG | Libre | ADC 12 bits disponible |
| A2 / GPIO 12 | ANALOG | Libre | ADC 12 bits disponible |
| GPIO 46 | INPUT_PULLUP | Boton PRG (integrado) | Pulsador usuario, activo bajo |
| TX | OUTPUT | UART TX (a CP2102) | Depuracion Serial (115200 baud) |
| RX | INPUT | UART RX (a CP2102) | Depuracion Serial (115200 baud) |

---

## Instrucciones de compilacion y carga

### Requisitos

```bash
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell
arduino-cli lib install "RadioLib@7.7.1"
```

### Compilar y subir

```bash
cd CubeCell-LoRaWAN-Node/

# Compilar
arduino-cli compile --fqbn "CubeCell:CubeCell:CubeCell-Board" .

# Subir a la placa
arduino-cli upload -p /dev/ttyUSB0 --fqbn "CubeCell:CubeCell:CubeCell-Board" .
```

### Salida esperada en serial

```
=== CubeCell Serial-to-LoRaWAN Bridge v1.2 ===
Radio OK
Node OK
scanGuard=200ms, millis sleep
Joining...
+++ JOINED +++
=== Listo. Escribe HEX y presiona Enter ===
>
```

---

## Formato del payload

El payload se define en el momento de escribir en el terminal serial. **No hay payload fijo en el firmware**.

```
Monitor Serie (texto)       →  LoRaWAN (bytes)       →  Gateway (hex)
──────────────────────────────────────────────────────────────────────
"01 01 01"                 →  0x01 0x01 0x01         →  010101
"DE AD BE EF"              →  0xDE 0xAD 0xBE 0xEF    →  DEADBEEF
"AA"                       →  0xAA                   →  AA
"A0B1C2"                   →  0xA0 0xB1 0xC2         →  A0B1C2
```

Longitud maxima: **51 bytes** (limite de DR2/DR3 en AU915 para SF10).

---

## Workaround critico (RadioLib Issue #1807)

El stack oficial de Heltec (CubeCell core v1.4.0) incluye `CubeCellLib.a` precompilada para ASR6501, que escala los timers RTC incorrectamente en ASR6502 (factor ~100×), impidiendo el join OTAA.

**Solucion aplicada en v1.2:**

| Tecnica | Detalle |
|---|---|
| **RadioLib 7.7.1** | Stack LoRaWAN independiente, no usa CubeCellLib.a |
| **millis() sleep** | `millis()` usa el RTC WCO de 32.768 kHz, es preciso. `delay()` usa HF clock y escala mal. |
| **scanGuard = 200** | Extiende la ventana de escaneo del Join Accept para compensar diferencias de clock. |

---

## Como se recupera la sesion

Si el gateway se reinicia o la sesion LoRaWAN expira, el firmware:

1. Detecta que `node.isActivated()` retorna `false`
2. Intenta un **rejoin automatico** con `node.activateOTAA()`
3. Si el rejoin falla, espera 5 segundos y reintenta
4. Si el rejoin es exitoso, vuelve al prompt `>` para seguir enviando

---

## Buffer serial: proteccion contra datos obsoletos

Cada vez que se abre o cierra el puerto serial, pueden quedar residuos en el buffer del CP2102. El firmware incluye un **stale buffer flush**: si pasan mas de 500ms sin recibir un caracter, el buffer parcial se descarta automaticamente.

Esto evita que datos de una sesion anterior se mezclen con comandos nuevos.

---

## Ejemplos de uso

### Ejemplo 1: Enviar un valor de sensor analogico manual

Simulas una lectura y la envias:

```
> 0A F1        ← 0x0AF1 = 2801 = 2.801V
>> Enviando: 0A F1 → OK
```

### Ejemplo 2: Enviar multiples canales

```
> CH1 25 CH2 30     ← error si no es hex puro
?? Error: escribe hex valido. Ej: DE AD BE EF
> 
```

Solo se aceptan caracteres hex.

### Ejemplo 3: Enviar comando de actuador

```
> FF              ← enciendo todo (interpretado por tu servidor)
>> Enviando: FF → OK
```

---

## Advertencias y precauciones

### Electricas
| Advertencia | Detalle |
|---|---|
| **Antena** | No encender la radio sin antena conectada. Puede danar el SX1262 permanentemente. |
| **Alimentacion** | Los GPIO solo toleran 3.3V. No conectar 5V a los pines. |
| **USB** | El puerto USB-C solo acepta 5V. No conectar fuentes externas al mismo tiempo. |

### De diseno
| Advertencia | Detalle |
|---|---|
| **GPIO 38** | Compartido entre LED Verde y DIO1 del SX1262. No se usa el LED para no interferir con la radio. |
| **Consumo** | El firmware usa spin-loop con `millis()`. No usa deep sleep. Consumo ~25 mA en espera, ~80 mA en Tx. |
| **DevNonce** | Si se borra y recrea el dispositivo en el NS, se debe reiniciar la placa para que genere un nuevo DevNonce. |

### De operacion
| Advertencia | Detalle |
|---|---|
| **Hex invalido** | Si escribes caracteres no hex (ej: `GG 01 02`), el firmware devuelve error y espera un nuevo comando. |
| **Conexion** | Si abres el puerto serial y ves basura, presiona Enter varias veces hasta que aparezca `>`. |

---

## Solucion de problemas

| Problema | Causa | Solucion |
|---|---|---|
| `Join FAILED: -1116` | DevNonce repetido. | Borrar y recrear el dispositivo en el UG56, luego resetear la placa. |
| `>> Error -1101 (NOT_JOINED)` | Sesion perdida. | El firmware lo detecta y hace rejoin automatico. Esperar. |
| No aparece el prompt `>` | Terminal abrio el puerto y na inicio. | Presionar Enter varias veces. Si no funciona, resetear la placa (desconectar USB). |
| Los datos llegan mezclados | Buffer serial con residuos de conexion anterior. | El firmware limpia automaticamente tras 500ms sin datos. |
| `?? Error: escribe hex valido` | Escribiste caracteres no hex. | Usar solo 0-9, A-F, a-f y espacios. |

---

## Historial de versiones

| Version | Fecha | Descripcion |
|---------|-------|-------------|
| v1.0 | 2026-07-27 | Firmware inicial con stack Heltec (no funcional por timer bug). |
| v1.1 | 2026-07-28 | Migracion a RadioLib 7.7.1. OTAA funcional. Payload fijo configurable. Intervalo de 60s. |
| **v1.2** | **2026-07-28** | **Serial-to-LoRaWAN Bridge. Modo interactivo: escribes HEX y se envia al instante. Stale buffer flush. Rejoin automatico.** |

---

## Repositorio

**GitHub:** [wanderc137/CubeCell-LoRaWAN-Node](https://github.com/wanderc137/CubeCell-LoRaWAN-Node)  
**Version:** `v1.2` — tag estable

```bash
git checkout v1.2    # volver a esta version
git checkout v1.1    # volver a la version anterior (payload fijo cada 60s)
```

---

## Licencia

MIT. Libre para uso, copia, modificacion y distribucion.
