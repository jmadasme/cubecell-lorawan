# CubeCell LoRaWAN Node v1.1

Nodo LoRaWAN Clase A basado en **Heltec CubeCell HTCC-AB01** (ASR6502 + SX1262) que transmite payloads HEX genéricos a un gateway **Milesight UG56** mediante **RadioLib 7.7.1**.

> Proposito: Enviar datos crudos en formato hexadecimal desde la CubeCell hacia un gateway LoRaWAN. El gateway publica los datos por MQTT, y un programa externo en el servidor se encarga de decodificar e interpretar el payload. No se requiere parsing en el firmware.

---

## Especificaciones de la placa

| Caracteristica | Valor |
|---|---|
| **Placa** | Heltec CubeCell HTCC-AB01 |
| **MCU** | ASR6502 (ARM Cortex M0+ a 48 MHz) |
| **Radio LoRa** | SX1262 (integrado en el modulo ASR) |
| **Banda** | AU915 (sub-banda 2, canales 8-15: 916.8 - 918.2 MHz) |
| **Activacion** | OTAA (Over-The-Air Activation) |
| **Clase** | Class A |
| **Puerto USB** | USB-C (alimentacion y programa) |
| **Antena** | Conector U.FL para antena LoRa externa 868/915 MHz |
| **Consumo** | ~80 mA en Tx, se puede optimizar con deep sleep |

---

## Principio de funcionamiento

La CubeCell ejecuta un firmware basado en **RadioLib 7.7.1**, una libreria independiente del stack de Heltec que ofrece control directo sobre el modulo SX1262.

### Flujo de operacion

```
ENCENDIDO
    │
    v
Inicializar radio SX1262
    │
    v
Configurar nodo LoRaWAN (OTAA)
    │
    v
Activar OTAA (Join Request / Join Accept)
    │
    v
[JOINED] ──> Enviar uplink con payload HEX
    │               │
    │               v
    │         Esperar 60 segundos
    │               │
    └──<───────────┘
    │
    v
Si se pierde la sesion → Re-Join automatico
```

### Workaround critico

El stack oficial de Heltec (CubeCell core v1.4.0) incluye una libreria precompilada (`CubeCellLib.a`) que escala incorrectamente los timers RTC en el ASR6502 (factor ~100× para timers cortos), lo que impide el join OTAA.

**Solucion:** Migrar a RadioLib 7.7.1, que permite usar `millis()` (preciso en CubeCell porque usa el RTC WCO de 32.768 kHz) como funcion de sleep, y ajustar `scanGuard = 200` para compensar pequenas imprecisiones del clock.

---

## Forma de operar

1. Conecta la antena LoRa al conector U.FL de la CubeCell.
2. Conecta la CubeCell al PC mediante cable USB-C.
3. Configura las credenciales OTAA en el firmware (`devEui`, `joinEui`, `appKey`).
4. Define el payload HEX deseado en `payload[]`.
5. Compila y sube el firmware a la placa.
6. La placa hara join automaticamente y comenzara a enviar datos cada 60 segundos.
7. Verifica la recepcion en la consola del gateway Milesight UG56.

---

## Pinout (Conexiones de la placa)

La CubeCell HTCC-AB01 no requiere componentes externos para este proyecto — solo antena y alimentacion USB. Los pines del modulo SX1262 se conectan internamente al ASR6502 mediante SPI.

### Pines del modulo SX1262 (conexion interna con el MCU)

| Pin SX1262 | GPIO ASR6502 | Funcion |
|------------|-------------|---------|
| NSS | **P4_3 (GPIO 35)** | Chip Select SPI |
| DIO1 | **P4_6 (GPIO 38)** | Interrupcion de radio |
| RESET | **P5_7 (GPIO 47)** | Reset del modulo |
| BUSY | **P4_7 (GPIO 39)** | Indicador de estado |

### Pines de expansion disponibles (GPIO del ASR6502)

Los siguientes pines se pueden usar para conectar sensores o actuadores externos:

| Pin Arduino / GPIO | Modo | Componente conectado | Notas |
|-------------------|------|---------------------|-------|
| D0 / GPIO 0 | DIGITAL | Libre | Disponible en header |
| D1 / GPIO 1 | DIGITAL | Libre | Disponible en header |
| D2 / GPIO 2 | DIGITAL | Libre | Disponible en header |
| D3 / GPIO 3 | DIGITAL | Libre | Disponible en header |
| A0 / GPIO 10 | ANALOG | Libre | ADC disponible |
| A1 / GPIO 11 | ANALOG | Libre | ADC disponible |
| A2 / GPIO 12 | ANALOG | Libre | ADC disponible |
| GPIO 46 | INPUT_PULLUP | Boton PRG (integrado) | Pulsador usuario, activo bajo |
| GPIO 9 | OUTPUT | LED RGB Azul | Activo bajo |
| GPIO 38 | OUTPUT | LED RGB Verde | Compartido con DIO1 de radio |
| GPIO 45 | OUTPUT | LED RGB Rojo | Activo bajo |
| TX | OUTPUT | UART TX (a CP2102) | Depuracion Serial (115200 baud) |
| RX | INPUT | UART RX (a CP2102) | Depuracion Serial (115200 baud) |

**Nota:** El GPIO 38 esta compartido entre el LED Verde y el DIO1 del SX1262. Si se usa el LED, las interrupciones de radio se veran afectadas. En este firmware no se usa el LED para evitar conflictos.

---

## Diagrama de conexion

Para el funcionamiento basico, solo se requiere:

```
 PC (USB-C)          CubeCell HTCC-AB01
┌──────────┐       ┌──────────────────────┐
│ USB-C    │───────│ USB-C (alimentacion y │
│ cable    │       │       programa)       │
└──────────┘       └──────────┬───────────┘
                              │
                              │ Conector U.FL
                              ├─────────────────── Antena LoRa
                              │                    (868/915 MHz)
                              │
                        (Sin componentes
                         externos requeridos)
```

### Si se conecta un sensor externo (ejemplo)

```
 Sensor BMP280          CubeCell
┌──────────────┐      ┌──────────┐
│ VCC (3.3V)   │──────│ 3.3V     │
│ GND          │──────│ GND      │
│ SDA (GPIO 0) │──────│ D0       │
│ SCL (GPIO 1) │──────│ D1       │
└──────────────┘      └──────────┘
```

---

## Instrucciones de compilacion y carga

### Requisitos

- [arduino-cli](https://arduino.github.io/arduino-cli/) instalado
- Core CubeCell instalado
- RadioLib 7.7.1 instalada

### Instalacion de herramientas

```bash
# Instalar arduino-cli (si no esta instalado)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Anadir el core de CubeCell
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell

# Instalar RadioLib 7.7.1 (via Arduino Library Manager)
arduino-cli lib install "RadioLib@7.7.1"
```

### Compilar y subir

```bash
# Ir al directorio del proyecto
cd /ruta/al/proyecto/CubeCell-LoRaWAN-Node

# Compilar
arduino-cli compile --fqbn "CubeCell:CubeCell:CubeCell-Board" .

# Subir a la placa (puerto tipico /dev/ttyUSB0)
arduino-cli upload -p /dev/ttyUSB0 --fqbn "CubeCell:CubeCell:CubeCell-Board" .
```

### Ver depuracion Serial

```bash
# Monitor serie
screen /dev/ttyUSB0 115200
# o
minicom -D /dev/ttyUSB0 -b 115200
```

Salida esperada:
```
=== CubeCell + RadioLib AU915 ===
Radio OK
Node OK
Workaround aplicado: millis sleep + scanGuard=200ms
Joining...
+++ JOINED +++
Payload (4 bytes): 01 02 03 04 
Enviando uplink...
Downlink recibido! (1 bytes)
Esperando 60000 ms...
Enviando uplink...
Downlink recibido! (1 bytes)
```

### Verificacion en gateway

En la consola del gateway Milesight UG56, los paquetes aparecen como:
```
DevAddr: 06D303F1
DevEUI: 24F124725F486022
FCnt: 0
Payload(hex): 01020304
Payload(b64): AQIDBA==
RSSI: -39
SNR: 13.2
```

---

## Ejemplos de uso

### Ejemplo 1: Enviar datos de un sensor de temperatura simulado

Cambiar el payload en el firmware:
```cpp
const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
```

Salida en el gateway:
```
Payload(hex): 01020304
```

El servidor recibe por MQTT y decodifica: temperatura = 0x0102 = 25.8 °C (ejemplo).

### Ejemplo 2: Enviar estado de 4 entradas digitales

```cpp
const uint8_t payload[] = { digitalRead(0), digitalRead(1), digitalRead(2), digitalRead(3) };
```

Salida en el gateway:
```
Payload(hex): 01000001  → (HIGH, LOW, LOW, HIGH)
```

### Ejemplo 3: Enviar valor analogico del sensor de bateria interno

```cpp
uint16_t vbat = analogRead(ADC_BATTERY);  // lectura ADC
uint8_t payload[2];
payload[0] = (vbat >> 8) & 0xFF;
payload[1] = vbat & 0xFF;
```

---

## Calibracion

No se requiere calibracion para el funcionamiento basico de transmision LoRaWAN.

Si se conectan sensores analogicos, se debe calibrar el factor de conversion ADC segun el sensor especifico. El ADC del ASR6502 tiene resolucion de 12 bits (0-4095) y referencia interna de 1.5 V.

---

## Advertencias y precauciones

### Electricas
- **Alimentacion:** Solo 3.3V en los pines GPIO. No aplicar voltajes mayores.
- **Corriente:** Los pines GPIO suministran maximo ~4 mA cada uno.
- **Antena:** No encender la radio sin antena conectada — puede danar el SX1262.
- **USB:** El puerto USB-C solo acepta 5V. No conectar fuentes externas al mismo tiempo.

### De diseno
- **GPIO 38** (LED Verde) comparte linea con DIO1 del SX1262. No usar LED Verde si se requiere radio estable.
- **deep sleep:** El firmware actual usa spin-loop con `millis()`. No entra en deep sleep. Consumo ~25 mA en idle, ~80 mA en Tx. Para bajo consumo (< 10 µA), se necesita una version con deep sleep.
- **Rejoin:** Si el gateway reinicia o se pierde la sesion, el firmware reintenta el join automaticamente.

### De manejo
- **DevNonce:** Si se borra y recrea el dispositivo en el NS, tambien se debe reiniciar la placa (o esperar a que expire el nonce).
- **Puerto serial:** No conectar nada a los pines TX/RX mientras se programa por USB.

---

## Formato del Payload

| Posicion | Tamano | Descripcion | Ejemplo |
|----------|--------|-------------|---------|
| 0 | 1 byte | Byte 0 del payload definido por el usuario | `0x01` |
| 1 | 1 byte | Byte 1 del payload definido por el usuario | `0x02` |
| 2 | 1 byte | Byte 2 del payload definido por el usuario | `0x03` |
| 3 | 1 byte | Byte 3 del payload definido por el usuario | `0x04` |

El numero de bytes es configurable. Por defecto son 4 bytes. Se pueden cambiar editando `payload[]`.

---

## Licencia

Este proyecto se distribuye bajo licencia MIT. Libre para uso, copia, modificacion y distribucion.

---

## Repositorio

GitHub: [wanderc137/CubeCell-LoRaWAN-Node](https://github.com/wanderc137/CubeCell-LoRaWAN-Node)

Version: **v1.1** — Firmware funcional con RadioLib 7.7.1, envio de payload HEX generico, region AU915 sub-banda 2, OTAA Clase A.
