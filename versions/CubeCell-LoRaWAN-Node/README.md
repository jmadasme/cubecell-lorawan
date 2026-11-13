# CubeCell LoRaWAN Bridge v1.3 — Serial ↔ LoRaWAN + Downlink

**Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262)**  
**Region: AU915 sub-banda 2 | OTAA Clase A | RadioLib 7.7.1**

---

## Que hace este proyecto

La CubeCell actúa como **puente bidireccional** entre el puerto serial y la red LoRaWAN:

| Direccion | Funcion |
|-----------|---------|
| **Serial → LoRaWAN** (v1.2) | Escribes HEX en el serial y se envía al instante al gateway UG56 |
| **LoRaWAN → Serial** (NUEVO v1.3) | El gateway envía comandos downlink y la CubeCell los imprime por el serial |

Esto permite que un **ESP32** conectado al serial TTL de la CubeCell reciba comandos del gateway y ejecute acciones: activar relés, leer sensores, etc.

---

## Flujo de funcionamiento

```
                    Serial (USB/TTL)
┌─────────┐      ┌──────────────────┐       LoRaWAN        ┌─────────────┐
│ Cutecom  │      │                  │ ──────────────────►  │             │
│ o ESP32  │◄────►│   CubeCell v1.3  │                     │  UG56       │
│          │      │                  │ ◄──────────────────  │  Gateway    │
└─────────┘      │  - Envia HEX     │    Downlink          │             │
                 │  - Recibe comandos│    (comandos)        └──────┬──────┘
                 │  - Heartbeat c/30s│                            │
                 └──────────────────┘                             │
                                                           Publica MQTT
```

---

## Modos de operacion

### Modo 1: Bridge Serial (envias HEX y se va al gateway)

```
> 01 02 03              ← escribes en cutecom
>> Enviando: 01 02 03 → OK + downlink (1 bytes)
[DOWNLINK] 00 → "."     ← comando MAC del gateway (normal)
>
```

### Modo 2: Recepcion de downlink (comandos del gateway)

Cuando envias un comando desde el gateway (interfaz web o MQTT), la CubeCell lo imprime:

```
[HEARTBEAT] >> Enviando: BB → OK + downlink (2 bytes)
[DOWNLINK] 48 49  →  "HI"
>
```

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
>
```

```
[DOWNLINK] 01 0A 01  →  "..."
>
```

### Modo 3: Heartbeat automatico

Cada **30 segundos** la CubeCell envía un heartbeat (`BB`) para abrir la ventana RX y permitir que el gateway envíe downlinks.

---

## Pinout

### Pines SX1262 (conexion interna)

| Pin SX1262 | GPIO ASR6502 | Funcion |
|------------|-------------|---------|
| NSS | **P4_3 (GPIO 35)** | Chip Select SPI |
| DIO1 | **P4_6 (GPIO 38)** | Interrupcion de radio |
| RESET | **P5_7 (GPIO 47)** | Reset del modulo |
| BUSY | **P4_7 (GPIO 39)** | Indicador de estado |

### Pines de expansion

| Pin / GPIO | Compatible con | Notas |
|-----------|----------------|-------|
| D0 (GPIO 0) | DIGITAL | I2C SDA disponible |
| D1 (GPIO 1) | DIGITAL | I2C SCL disponible |
| GPIO 10-12 | ANALOG | ADC 12 bits |
| TX/RX | UART | Serial a 115200 baud |

---

## Como enviar un downlink desde el UG56

### Opcion A: Interfaz web del gateway

1. `LoRa → Network Server → Device`
2. Seleccionar **JAM101** (`24F124725F486022`)
3. Click **"Send Downlink"**
4. Configurar:

| Campo | Valor |
|-------|-------|
| **Payload (hex)** | `686F6C61` ( "hola" ) |
| **Port** | `1` |
| **Confirmed** | `false` |
| **Type** | `Unconfirmed` |

5. Click **"Send"**
6. Esperar el heartbeat (max 30s) y en el serial aparecerá:

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
```

### Opcion B: Por MQTT

```
Topic: /downlink/24F124725F486022
Payload: 686F6C61
```

---

## Instalacion y compilacion

```bash
# Instalar core CubeCell
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell

# Instalar RadioLib 7.7.1
arduino-cli lib install "RadioLib@7.7.1"

# Compilar y subir
cd CubeCell-LoRaWAN-Node/
arduino-cli compile --fqbn "CubeCell:CubeCell:CubeCell-Board" .
arduino-cli upload -p /dev/ttyUSB0 --fqbn "CubeCell:CubeCell:CubeCell-Board" .
```

---

## Especificaciones tecnicas

| Parametro | Valor |
|-----------|-------|
| MCU | ASR6502 (ARM Cortex M0+ @ 48 MHz) |
| Radio | SX1262 |
| Banda | AU915 sub-banda 2 (916.8 - 918.2 MHz) |
| Activacion | OTAA Clase A |
| Heartbeat | Cada 30 segundos (payload `BB`) |
| Payload maximo | 51 bytes |
| Baudrate serial | 115200 baud, 8N1 |
| Prompt serial | `> ` |
| Formato downlink | `[DOWNLINK] XX YY ZZ → "texto"` |

---

## Workaround ASR6502

El stack oficial de Heltec escala los timers ~100×. Solucion:

1. **RadioLib 7.7.1** — no usa CubeCellLib.a
2. **millis() sleep** — `millis()` usa RTC WCO 32.768 kHz (preciso)
3. **scanGuard = 200** — extiende ventana de Join Accept

---

## Advertencias

| Precaución | Detalle |
|-----------|---------|
| **Antena** | No operar sin antena conectada al U.FL |
| **Downlink** | Solo llega si la CubeCell envia un uplink (heartbeat cada 30s habilita ventana) |
| **Voltaje** | GPIO solo toleran 3.3V |
| **USB** | No conectar alimentacion externa junto con USB |
| **DevNonce** | Si se recrea el dispositivo en el NS, tambien reiniciar la placa |

---

## Versionado

| Version | Fecha | Funcionalidad |
|---------|-------|---------------|
| v1.1 | 2026-07-28 | RadioLib 7.7.1, OTAA, payload fijo cada 60s (legacy) |
| v1.2 | 2026-07-28 | Bridge Serial → LoRaWAN interactivo |
| **v1.3** | **2026-07-28** | **Bridge bidireccional: Serial ↔ LoRaWAN + Downlink + Heartbeat** |

---

## Repositorio

**GitHub:** [wanderc137/CubeCell-LoRaWAN-Node-V1.2](https://github.com/wanderc137/CubeCell-LoRaWAN-Node-V1.2)  
**Tag:** `v1.3`
