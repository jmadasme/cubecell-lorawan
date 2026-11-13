# Datasheet -- CubeCell DHT11 LoRaWAN Sensor

Documentacion de usuario del nodo sensor LoRaWAN de ultra-bajo consumo basado en Heltec CubeCell HTCC-AB02A con sensor DHT11.

---

## Tabla de Contenidos

1. [Vista General del Producto](#vista-general-del-producto)
2. [Especificaciones Tecnicas](#especificaciones-tecnicas)
3. [Componentes del Sistema](#componentes-del-sistema)
4. [Formato de Payload LoRaWAN](#formato-de-payload-lorawan)
5. [Decodificacion de Payload](#decodificacion-de-payload)
6. [Configuracion del Network Server](#configuracion-del-network-server)
7. [Librerias Requeridas](#librerias-requeridas)
8. [Solucion de Problemas Comunes](#solucion-de-problemas-comunes)
9. [Tabla de Compatibilidad](#tabla-de-compatibilidad)
10. [Especificaciones de Bateria](#especificaciones-de-bateria)
11. [Cumplimiento Normativo](#cumplimiento-normativo)

---

## Vista General del Producto

El **CubeCell DHT11 LoRaWAN Sensor** es un dispositivo IoT de ultra-bajo consumo disenado para monitoreo de temperatura ambiente en interiores y exteriores protegidos. Transmite datos cada 10 minutos mediante LoRaWAN (US915, OTAA, Clase A) a un gateway compatible, con una autonomia superior a 1 ano usando una bateria ER14250 de 1200 mAh.

### Caracteristicas clave

- Sensor de temperatura DHT11 (precisión ±2°C tipica, rango 0-50°C)
- Transmision LoRaWAN US915 SF7, Clase A, unconfirmed uplink
- Power gating del sensor para minimo consumo (GPIO3 alimenta el DHT11 solo durante la lectura)
- Payload compacto de 4 bytes (random + temperatura en decimas de C)
- OTAA (Over-The-Air Activation) con persistencia de sesion entre deep sleeps
- Deep sleep de 10 minutos con consumo ~4 uA
- Protocolo DHT11 implementado por bit-banging (sin librerias externas)
- Firmware: 92.6 KB Flash / ~5.5 KB RAM
- Puerto LoRaWAN: 1
- Time-on-air: ~46 ms (SF7, US915, 125 kHz, 4B payload)

### Aplicaciones tipicas

- Monitoreo de temperatura en almacenes, oficinas, hogares
- Agricultura de precision (invernaderos, cultivos protegidos)
- Data logging ambiental de largo plazo
- Proyectos educativos de IoT y LoRaWAN
- Prototipado rapido de sensores LoRaWAN de bajo consumo
- Monitoreo de cadena de frio (siempre que la temperatura este en 0-50°C)

---

## Especificaciones Tecnicas

### Placa y Microcontrolador

| Parametro | Valor |
|---|---|
| **Placa** | Heltec CubeCell HTCC-AB02A |
| **MCU** | ASR6501 (ARM Cortex-M0+ 32-bit @ 48 MHz) |
| **Memoria Flash** | 128 KB |
| **Memoria RAM** | 16 KB SRAM |
| **EEPROM** | No (usa Flash emulada por el framework para sesion LoRaWAN) |
| **Voltaje de operacion** | 2.5 - 3.6 V DC (3.3 V nominal via LDO interno) |
| **Consumo en deep sleep** | ~4 uA tipico, ~8 uA realista, < 20 uA worst-case |
| **Consumo en TX (SF7, +14 dBm)** | ~30 mA pico |
| **Rango de temperatura de operacion** | -40°C a +85°C (MCU ASR6501); 0°C a +50°C (limitado por DHT11) |
| **Humedad de operacion** | 10% - 90% RH (sin condensacion) |
| **Dimensiones** | ~51 x 25 x 10 mm (sin antena) |
| **Peso** | ~12 g (sin bateria) |
| **Conector de bateria** | Holder integrado formato 1/2 AA (ER14250) |
| **Conector de antena** | IPEX U.FL + antena de resorte integrada (915 MHz) |
| **Puerto de programacion** | USB-C (CH340/CP2102 USB-UART) |

### Radio LoRa / LoRaWAN

| Parametro | Valor |
|---|---|
| **Transceptor** | Semtech SX1262 |
| **Bandas soportadas** | 868 MHz (EU868), 915 MHz (US915/AU915), 433 MHz, 470 MHz, 923 MHz (AS923) -- configurable via board package |
| **Region usada en este firmware** | US915 (125 kHz BW, sub-banda 1, canales 0-7) |
| **Potencia de transmision** | Hasta +22 dBm (configurado a +14 dBm tipico en este firmware) |
| **Spreading Factor** | SF7 (configurable via ADR si se habilita) |
| **Bandwidth** | 125 kHz |
| **Coding Rate** | 4/5 |
| **Modo de activacion** | OTAA (Over-The-Air Activation) |
| **Clase** | A (minimo consumo: TX seguido de ventanas RX1/RX2 solo si confirmed) |
| **Modo de uplink** | Unconfirmed (sin ACK del servidor, sin espera de RX) |
| **Persistencia de sesion** | Habilitada (no re-join tras deep sleep) |
| **Time-on-air (4B payload, SF7, US915 125 kHz)** | ~46.3 ms |
| **Tasa de datos efectiva (SF7, US915)** | ~5.47 kbps |
| **Alcance tipico** | 2-5 km (linea de vista rural); 500 m - 2 km (urbano/suburbano) |
| **Sensibilidad (SF7, 125 kHz)** | -124 dBm (SX1262) |
| **Link budget (SF7, +14 dBm)** | ~138 dB |
| **Cifrado** | AES-128 (NwkSKey + AppSKey derivados de OTAA) |

### Sensor DHT11

| Parametro | Valor |
|---|---|
| **Tipo** | Sensor digital de temperatura y humedad |
| **Rango de temperatura** | 0°C a +50°C |
| **Precision temperatura** | +-2°C (tipica +-1°C) |
| **Resolucion temperatura** | 1°C (parte entera); 0.1°C en payload (decimas) |
| **Rango de humedad** | 20% a 90% RH (no transmitida en este firmware) |
| **Precision humedad** | +-5% RH |
| **Frecuencia de muestreo maxima** | 1 Hz (1 lectura por segundo) |
| **Consumo** | ~1-3 mA durante medicion; ~0 uA (apagado via GPIO3) |
| **Protocolo** | One-wire propietario (no confundir con Dallas OneWire) |
| **Resistencia pull-up** | ~5.1 kOhm incluida en el modulo DHT11 |
| **Tiempo de estabilizacion** | 1 segundo tras power-on (requerido por datasheet) |
| **Tiempo de respuesta** | ~6-15 segundos (constante termica del empaquetado) |

### Firmware

| Parametro | Valor |
|---|---|
| **Framework** | Arduino + Heltec CubeCell Board Package v1.4.0+ |
| **Lenguaje** | C++ (Arduino .ino) |
| **FQBN** | `heltec_cubecell:cubecell:HTCC-AB02A` |
| **Tamano Flash** | 92,640 bytes (70.7% de 128 KB) |
| **Uso RAM** | ~5,500 bytes (33.6% de 16 KB) |
| **Librerias externas** | Ninguna (DHT11 leido por bit-banging sin dependencias) |
| **Intervalo de transmision** | 600 segundos (10 minutos) + jitter aleatorio (~0-60s) |
| **Puerto LoRaWAN** | 1 |
| **Tamano de payload** | 4 bytes fijos |
| **Debug Serial** | 115200 baud, 8N1 (deshabilitable con `SERIAL_DEBUG=0`) |

---

## Componentes del Sistema

### Diagrama de bloques

```
┌──────────────────────────────────────────────────────────────────────┐
│                         CubeCell HTCC-AB02A                           │
│                                                                       │
│  ┌───────────┐    ┌────────────┐    ┌──────────────────────────────┐ │
│  │ ER14250   │───▶│ Regulador  │───▶│ ASR6501 Cortex-M0+ @ 48 MHz  │ │
│  │ 3.6V      │    │ LDO 3.3V   │    │                              │ │
│  │ 1200 mAh  │    │ (TPS7A...) │    │  ┌────────┐  ┌────────────┐  │ │
│  │ Li-SOCl2  │    └────────────┘    │  │ RTC    │  │ Flash      │  │ │
│  └───────────┘                      │  │ Timer  │  │ 128 KB     │  │ │
│                                     │  └───┬────┘  │  Firmware:  │  │ │
│  ┌───────────┐    ┌────────────┐    │      │ Wakeup │  92.6 KB   │  │ │
│  │ DHT11     │◄───│ GPIO3 VCC  │    │      │ c/600s └────────────┘  │ │
│  │           │    │ (P6_2)     │    │      │                        │ │
│  │  VCC ◄────┼────┤ on/off     │    │  ┌───┴────┐  ┌────────────┐  │ │
│  │  DATA ────┼───▶│ GPIO2 DATA │    │  │ SRAM   │  │ SX1262     │  │ │
│  │  GND ─────┼──┐ │ (P6_1)     │    │  │ 16 KB  │  │ LoRa Radio │  │ │
│  │           │  │ │ one-wire   │    │  │ ~5.5KB │  │ 915 MHz    │  │ │
│  └───────────┘  │ └────────────┘    │  │ usada  │  │ +14 dBm    │  │ │
│                 │                   │  └────────┘  └─────┬──────┘  │ │
│                 │                   └───────────────────┼─────────┘ │
│                 │                                       │           │
│                 │                                  Antena 915 MHz   │
│                 │                                  (resorte/IPEX)   │
│                 │                                                    │
│                 └──── GND                                            │
└──────────────────────────────────────────────────────────────────────┘
```

### Pines y conectores fisicos de la placa

```
    Vista superior HTCC-AB02A (conector USB-C hacia arriba)
    ╔══════════════════════════════════════════════╗
    ║   ┌────────────────────────────┐             ║
    ║   │       Antena resorte       │             ║
    ║   │       915 MHz integrada    │             ║
    ║   └────────────────────────────┘             ║
    ║                                              ║
    ║   ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ┌───┐  ║
    ║   │GND│  │ 5 │  │ 4 │  │ 3 │  │ 2 │  │ 1 │  ║
    ║   └───┘  └───┘  └───┘  └───┘  └───┘  └───┘  ║
    ║                                              ║
    ║        ┌──────────────────────┐              ║
    ║        │                      │              ║
    ║        │   ASR6501 + SX1262   │              ║
    ║        │                      │              ║
    ║        └──────────────────────┘              ║
    ║                                              ║
    ║   ┌──────────────────────────┐               ║
    ║   │  Holder bateria ER14250  │               ║
    ║   │  (+ hacia arriba)        │               ║
    ║   └──────────────────────────┘               ║
    ║                                              ║
    ║        ┌──────┐                              ║
    ║        │USB-C │                              ║
    ║        └──────┘                              ║
    ╚══════════════════════════════════════════════╝

    Pines (izquierda a derecha, vista superior):
     1 = GPIO1 (P6_0)
     2 = GPIO2 (P6_1) = DHT11 DATA
     3 = GPIO3 (P6_2) = DHT11 VCC
     4 = GPIO4 (P6_4)
     5 = GPIO5 (P3_4)
    GND = Tierra
```

---

## Formato de Payload LoRaWAN

### Estructura del mensaje uplink (4 bytes)

```
Byte 0    Byte 1    Byte 2    Byte 3
┌────────┬────────┬────────┬────────┐
│ Rand   │ Rand   │ Temp   │ Temp   │
│ LSB    │ MSB    │ LSB    │ MSB    │
│ uint16 little-endian │ int16 little-endian │
└────────┴────────┴────────┴────────┘
  [0]      [1]      [2]      [3]
```

| Campo | Bytes | Tipo C | Rango | Descripcion |
|---|---|---|---|---|
| `random` | 0-1 | `uint16_t` LE | 0 - 65535 | Numero pseudoaleatorio generado con `random(65536)`. Valor demostrativo sin significado fisico. |
| `temp_decimas` | 2-3 | `int16_t` LE | -32768 a 32767 (real 0-500 en DHT11) | Temperatura en decimas de °C. Ej: 245 = 24.5°C. `0xFFFF` (-1 como int16) = error de lectura del sensor. |

**Puerto LoRaWAN:** 1
**Modo:** Unconfirmed uplink
**ADR:** Deshabilitado por defecto

### Ejemplos de payload

| Payload (HEX) | Random (dec) | Temp (decimas) | Temp (°C) | Interpretacion |
|---|---|---|---|---|
| `25 A7 F5 00` | 42789 | 245 | 24.5 | Lectura normal |
| `2B 32 FB 00` | 12843 | 251 | 25.1 | Lectura normal |
| `00 00 6E 00` | 0 | 110 | 11.0 | Temperatura fresca |
| `A1 B2 10 01` | 45729 | 272 | 27.2 | Temperatura calida |
| `53 D7 F0 00` | 55123 | 240 | 24.0 | Lectura normal |
| `D9 22 FF FF` | 8921 | -1 (0xFFFF) | ERROR | Sensor no responde / timeout / checksum error |
| `00 00 C6 FF` | 0 | -58 | -5.8 | Valor negativo (imposible con DHT11; valido con DHT22) |

### Notas sobre el formato

- **Little-endian:** El byte menos significativo (LSB) va primero. Para 24.5°C = 245 decimal = `0x00F5`, el payload contiene `F5 00` (byte 2 = 0xF5, byte 3 = 0x00).
- **Valor centinela `0xFFFF`:** Las funciones `leerDHT11()` y `leerDHT11_raw()` retornan `TEMP_ERROR_VALUE = 0xFFFF` cuando el sensor no responde (timeout) o el checksum es invalido. En el payload esto se representa como `FF FF` en los bytes 2 y 3. Al decodificar, se interpreta como `int16_t` = -1, que el decodificador trata como `temperature_error = true`.
- **Numero aleatorio:** Generado con `random(65536)` usando semilla inicializada en `setup()` con `analogRead(ADC)`. No se re-semilla en cada ciclo, permitiendo que la secuencia PRNG avance naturalmente. No es criptograficamente seguro (no se requiere).
- **Puerto LoRaWAN 1:** El decodificador del Network Server debe estar configurado para el puerto 1.
- **Temperatura negativa:** El DHT11 solo mide de 0 a 50°C, por lo que nunca enviara valores negativos. Si se cambia el sensor a un DHT22 (rango -40 a 80°C), el formato `int16_t` soporta valores negativos correctamente (ej: -5.5°C = -55 decimas = 0xFFC9 en complemento a dos).

---

## Decodificacion de Payload

### JavaScript (compatible con ChirpStack v4, TTN v3, Helium Console)

```javascript
/**
 * Decodificador de uplink para CubeCell DHT11 LoRaWAN Sensor.
 * Formato payload: [random_LSB, random_MSB, temp_LSB, temp_MSB]
 * Little-endian. Puerto LoRaWAN: 1.
 *
 * Compatible con:
 *   - ChirpStack v4 (Codec > Custom JavaScript)
 *   - The Things Network v3 (Payload Formatters > Uplink > Custom Javascript)
 *   - Helium Console (Functions > Custom Script)
 */
function decodeUplink(input) {
  var bytes = input.bytes;
  var data = {};

  if (bytes.length >= 4) {
    // Random: 2 bytes uint16 little-endian
    data.random = bytes[0] | (bytes[1] << 8);

    // Temperatura: 2 bytes int16 little-endian
    var tempRaw = bytes[2] | (bytes[3] << 8);

    // Manejar signo (int16_t en complemento a dos)
    if (tempRaw >= 32768) {
      tempRaw = tempRaw - 65536;
    }

    data.temperature_decimas = tempRaw;

    if (tempRaw === -1) {
      // 0xFFFF = TEMP_ERROR_VALUE -> error de sensor
      data.temperature_c = null;
      data.temperature_error = true;
      data.temperature_status = "ERROR: sensor no responde";
    } else {
      data.temperature_c = tempRaw / 10.0;
      data.temperature_error = false;
      data.temperature_status = "OK";
    }
  } else {
    data.error = "Payload debe ser al menos 4 bytes, recibidos " + bytes.length;
  }

  return {
    data: data,
    warnings: [],
    errors: []
  };
}

// Verificacion rapida
// decodeUplink({bytes: [0x25, 0xA7, 0xF5, 0x00]})
// -> {data: {random: 42789, temperature_c: 24.5, ...}}
```

### Python (libreria struct)

```python
import struct
from typing import Dict, Union

def decode_cubecell_dht11(payload: bytes) -> Dict[str, Union[int, float, bool, None, str]]:
    """
    Decodifica payload de 4 bytes del CubeCell DHT11 LoRaWAN Sensor.

    Args:
        payload: bytes con el payload (4 bytes).

    Returns:
        dict con:
          - random (int): numero pseudoaleatorio 0-65535
          - temperature_decimas (int): temperatura en decimas de C,
            o -1 si error
          - temperature_c (float | None): temperatura en Celsius,
            o None si error
          - temperature_error (bool): True si la lectura fallo
          - temperature_status (str): "OK" o mensaje de error
    """
    if len(payload) < 4:
        return {
            "error": f"Payload debe ser al menos 4 bytes, recibidos {len(payload)}",
            "random": None,
            "temperature_c": None,
            "temperature_error": True,
            "temperature_decimas": None,
            "temperature_status": "ERROR: payload invalido"
        }

    # Little-endian
    random_val = struct.unpack_from('<H', payload, 0)[0]   # uint16
    temp_raw   = struct.unpack_from('<h', payload, 2)[0]   # int16

    result = {
        "random": random_val,
        "temperature_decimas": temp_raw,
        "temperature_error": False,
        "temperature_c": None,
        "temperature_status": "OK"
    }

    if temp_raw == -1:  # 0xFFFF como int16 = -1
        result["temperature_error"] = True
        result["temperature_status"] = "ERROR: sensor no responde"
    else:
        result["temperature_c"] = temp_raw / 10.0

    return result


# --- Ejemplos de uso ---
if __name__ == "__main__":
    # Ejemplo 1: lectura normal
    data = decode_cubecell_dht11(bytes.fromhex("25A7F500"))
    print(f"1: {data}")
    # {'random': 42789, 'temperature_decimas': 245,
    #  'temperature_error': False, 'temperature_c': 24.5,
    #  'temperature_status': 'OK'}

    # Ejemplo 2: error de sensor
    data = decode_cubecell_dht11(bytes.fromhex("D922FFFF"))
    print(f"2: {data}")
    # {'random': 8921, 'temperature_decimas': -1,
    #  'temperature_error': True, 'temperature_c': None,
    #  'temperature_status': 'ERROR: sensor no responde'}

    # Ejemplo 3: lote de payloads
    payloads = ["25A7F500", "2B32FB00", "D922FFFF", "00006E00"]
    for ph in payloads:
        d = decode_cubecell_dht11(bytes.fromhex(ph))
        temp_str = "ERROR" if d['temperature_error'] else f"{d['temperature_c']}°C"
        print(f"  {ph}: random={d['random']:5d}, temp={temp_str}")
```

### C++ (Arduino, para recibir y decodificar en otro dispositivo)

```cpp
#include <stdint.h>

struct SensorData {
    uint16_t randomValue;
    int16_t  tempDecimas;
    bool     tempError;
    float    tempCelsius;
};

SensorData decodePayload(const uint8_t* payload, size_t len) {
    SensorData data = {0, 0, false, 0.0f};

    if (len >= 4) {
        // Random: uint16_t little-endian
        data.randomValue = payload[0] | (((uint16_t)payload[1]) << 8);

        // Temperatura: int16_t little-endian
        data.tempDecimas = (int16_t)(payload[2] | (((uint16_t)payload[3]) << 8));

        if (data.tempDecimas == -1) {  // 0xFFFF = TEMP_ERROR_VALUE
            data.tempError = true;
            data.tempCelsius = 0.0f;
        } else {
            data.tempCelsius = data.tempDecimas / 10.0f;
        }
    } else {
        data.tempError = true;
    }

    return data;
}

// Uso:
// uint8_t pkt[] = {0x25, 0xA7, 0xF5, 0x00};
// SensorData sd = decodePayload(pkt, 4);
// Serial.printf("Temp: %.1f C, Random: %u\n", sd.tempCelsius, sd.randomValue);
```

### Node-RED (Function node para flujo MQTT/HTTP)

```javascript
// Node-RED Function node - Procesa mensaje MQTT de ChirpStack/TTN
// Entrada: msg.payload con objeto JSON del evento uplink

var input = msg.payload;

// Extraer bytes del payload (ChirpStack v4 / TTN v3)
var bytes = input.uplink_message ?
    input.uplink_message.frm_payload_bytes || input.uplink_message.f_port_bytes :
    msg.payload.bytes || msg.payload;

// Si es string base64 (ChirpStack v3), convertir
if (typeof bytes === 'string') {
    bytes = Buffer.from(bytes, 'base64');
}

if (bytes && bytes.length >= 4) {
    var random = bytes[0] | (bytes[1] << 8);
    var tempRaw = bytes[2] | (bytes[3] << 8);
    if (tempRaw >= 32768) tempRaw -= 65536;

    var deviceInfo = input.end_device_ids || {};
    var deviceName = deviceInfo.device_id || "desconocido";

    msg.payload = {
        device: deviceName,
        random: random,
        temperature_c: tempRaw === -1 ? null : tempRaw / 10.0,
        temperature_error: tempRaw === -1,
        temperature_status: tempRaw === -1 ? "ERROR" : "OK",
        rssi: input.uplink_message ? input.uplink_message.rx_metadata[0].rssi : null,
        snr: input.uplink_message ? input.uplink_message.rx_metadata[0].snr : null,
        timestamp: new Date().toISOString()
    };
}

return msg;
```

### Integracion con Grafana + InfluxDB (ejemplo de consulta Flux)

```flux
// Consulta Flux para Grafana - Temperatura del CubeCell DHT11
from(bucket: "lorawan")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "uplink")
  |> filter(fn: (r) => r.device_id == "cubecell-dht11-01")
  |> filter(fn: (r) => r._field == "temperature_c")
  |> filter(fn: (r) => r.temperature_error == "false")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "mean")
```

---

## Configuracion del Network Server

### ChirpStack v4

1. Crear un **Device Profile** con:
   - LoRaWAN MAC version: 1.0.3
   - Regional Parameters: US915 (Regional Parameters Revision B)
   - Class: A
   - RX1 Delay: 1 second
   - RX1 Data-rate Offset: 0
   - RX2 Data-rate: 8 (SF12, 500 kHz)
   - Max EIRP: 14 dBm
   - Supports OTAA: Yes
   - Supports Class-B: No
   - Supports Class-C: No

2. Crear un **Application** (o usar una existente).

3. Registrar un **Device** en la aplicacion:
   - Device EUI: El de tu dispositivo (o generado automaticamente si `LORAWAN_DEVEUI_AUTO=ON`)
   - JoinEUI (AppEUI): El de tu aplicacion (en ChirpStack v4 se llama JoinEUI)
   - AppKey: La clave de aplicacion (16 bytes)
   - Device Profile: El creado en el paso 1

4. Configurar el **Payload Codec** en el Device Profile:
   - Ir a Device Profile > Codec > Custom JavaScript
   - Pegar el [decodificador JavaScript](#javascript-compatible-con-chirpstack-v4-ttn-v3-helium-console)

### The Things Network (TTN v3)

1. Ir a **Applications** > **Add Application** o usar una existente.

2. **Register End Device:**
   - Input Method: Enter end device specifics manually
   - Frequency Plan: United States 902-928 MHz FSB 1 (used by TTN)
   - LoRaWAN Version: MAC V1.0.3
   - Regional Parameters version: RP002 Regional Parameters 1.0.3
   - Activation Mode: OTAA
   - Additional LoRaWAN Class Capabilities: Class A only

3. En **Provisioning Information:**
   - JoinEUI: El de tu aplicacion
   - DevEUI: El de tu dispositivo (o generado)
   - AppKey: La clave de aplicacion

4. En **Payload Formatters** > Uplink:
   - Formatter type: Custom Javascript
   - Pegar el decodificador JavaScript

### Helium Console

1. Ir a **Devices** > **Add Device**:
   - Device EUI, App EUI, App Key (generados o introducidos manualmente)
   - Integration: HTTP / MQTT segun tu backend

2. En **Functions** > **Add Function**:
   - Type: Custom Script (Decoder)
   - Pegar el decodificador JavaScript (`decodeUplink`)

3. Asignar la funcion al dispositivo en **Flows**:
   - Device > Function > Integration

### Verificacion de la configuracion

Despues de configurar el Network Server, verificar:

1. El JOIN es exitoso (monitor serial muestra `[JOIN] Joined!` o el Network Server reporta "join-accept").
2. Los uplinks se reciben en el Network Server con el device correcto.
3. El payload se decodifica correctamente (los campos `random`, `temperature_c` aparecen en los datos decodificados).
4. El valor centinela `0xFFFF` se muestra como `temperature_error: true` y no como un valor numerico.

---

## Librerias Requeridas

### Librerias del sistema (incluidas en el board package de CubeCell)

| Libreria | Archivo | Proposito |
|---|---|---|
| `LoRaWan_APP.h` | Framework CubeCell | API de alto nivel para LoRaWAN: `init()`, `join()`, `send()`, `sleep()`, `cycle()`, maquina de estados (`DEVICE_STATE_*`), persistencia de sesion (`ifskipjoin()`). |
| `Radio.h` | Framework CubeCell | Control directo del SX1262 (usado internamente por `LoRaWan_APP`). Proporciona `Radio.Sleep()` para minimo consumo. |
| `Arduino.h` | Framework CubeCell | Funciones estandar de Arduino adaptadas al ASR6501: `digitalRead/Write`, `pinMode`, `delay`, `delayMicroseconds`, `micros`, `random`, `randomSeed`, `analogRead`, `F()` macro, `Serial`. |
| `SPI.h` | Framework CubeCell | Comunicacion SPI con SX1262 (usado internamente por el framework). |

### Librerias externas requeridas

**Ninguna.** La lectura del DHT11 se implementa manualmente mediante bit-banging (~60 lineas de codigo en `cubecell-dht11-lora.ino`), eliminando completamente la dependencia de `DHT sensor library` de Adafruit o cualquier otra libreria de terceros.

**Ventajas de no usar librerias externas:**
- Reduce el tamano del firmware (~2-3 KB ahorrados).
- Elimina problemas de compatibilidad de temporizacion ARM Cortex-M0+ vs AVR (las librerias DHT estan optimizadas para AVR y pueden fallar en ARM).
- Proporciona control total sobre timeouts y reintentos.
- Facilita la auditoria de seguridad (sin codigo de terceros).
- Mejora la portabilidad entre diferentes MCUs ARM.

### Instalacion (solo board package)

```bash
# El board package incluye todas las dependencias necesarias.
# No se requieren librerias adicionales.

# Paso 1: agregar indice de placas Heltec
arduino-cli config add board_manager.additional_urls \
  https://resource.heltec.cn/download/package_CubeCell_index.json

# Paso 2: actualizar indice
arduino-cli core update-index

# Paso 3: instalar board package
arduino-cli core install heltec_cubecell:cubecell

# Paso 4: verificar
arduino-cli core list | grep cubecell
# heltec_cubecell:cubecell  1.5.0  Heltec CubeCell Board
```

---

## Solucion de Problemas Comunes

### 1. El dispositivo no se une a la red (JOIN FAILED)

**Sintomas:** Monitor serial muestra `[JOIN] Joining...` repetidamente sin exito.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| Credenciales incorrectas en `credentials.h` | Verificar DevEUI, AppEUI/JoinEUI, AppKey en el Network Server. Asegurar formato little-endian. Probar con `LORAWAN_DEVEUI_AUTO=ON` para descartar errores de DevEUI. |
| Gateway fuera de alcance o apagado | Verificar que el gateway Milesight UG56 esta encendido, conectado a internet, y configurado en el Network Server. Acercar el dispositivo al gateway para la prueba. |
| El dispositivo ya esta unido pero intenta re-join | Verificar `keepNet = LORAWAN_NET_RESERVE` en el codigo. Si `keepNet = false`, el dispositivo intenta un JOIN nuevo en cada ciclo. |
| Banda de frecuencia incorrecta | En Arduino IDE: Tools > LoRaWAN Region debe ser `REGION_US915`. Verificar que el gateway tambien use US915. |
| Canales no coincidentes con el gateway | El firmware usa canales 0-7 (sub-banda 1): `userChannelsMask[6] = {0x00FF, ...}`. Verificar que el gateway escuche en esos canales (frecuencias 902.3-903.7 MHz). |
| Bateria agotada (< 2.5 V) | Medir voltaje de bateria con multimetro. Reemplazar si < 2.5 V. El LDO necesita al menos 2.5V para regular a 3.3V. |
| Distancia excesiva al gateway | SF7 tiene alcance de 2-5 km en linea de vista, ~500m en interiores/urbano. Si la distancia es mayor, acercar el dispositivo o usar ADR para SF mas altos. |
| Interferencia LoRaWAN | Si hay muchos dispositivos LoRaWAN en la misma zona, las colisiones pueden impedir el JOIN. Intentar en horario de baja actividad. |

### 2. Lecturas de temperatura erroneas o DHT11 timeout

**Sintomas:** Monitor serial muestra `ERROR: DHT11 timeout o no responde` o `DHT11 checksum error: calc=0xXX recv=0xYY`.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| DHT11 desconectado o mal cableado | Verificar conexiones: VCC -> GPIO3, DATA -> GPIO2, GND -> GND. Probar continuidad con multimetro. |
| Cable DATA demasiado largo (> 20 cm) | Usar cables mas cortos (10-15 cm ideales). La capacitancia parasita en cables largos distorsiona los flancos del protocolo. |
| GPIO3 no suministra suficiente corriente | Verificar que GPIO3 esta en HIGH antes de la lectura. Medir voltaje en el pin VCC del DHT11: debe ser ~3.3V. Si es menor, la bateria puede estar baja o el pin tiene un problema. |
| `DHT11_STABILIZE_MS` insuficiente | Aumentar a 2000 ms si el ambiente es frio (< 15 C). El DHT11 tarda mas en estabilizarse en temperaturas bajas. |
| `DHT11_TIMEOUT_US` insuficiente | Aumentar a 300 us. Bateria baja (< 2.8 V) puede ralentizar los flancos digitales. |
| DHT11 defectuoso (comun en modulos baratos) | Reemplazar sensor. Probar con otro DHT11 nuevo. La tasa de fallos en DHT11 genericos es alta (~10-20%). |
| Interferencia electrica en la linea DATA | Separar el cable DATA de fuentes de ruido (motores, fuentes switching, cables de alimentacion). Usar cable apantallado si es necesario. |
| Checksum error persistente | Indica corrupcion de datos en la transmision one-wire. Verificar fuente de alimentacion limpia. Anadir condensador de 100 nF entre VCC y GND del DHT11. |

### 3. El dispositivo gasta bateria demasiado rapido

**Sintomas:** Bateria se agota en dias/semanas en lugar de meses/anos.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| `SERIAL_DEBUG=1` en produccion (UART activo consume ~1 mA) | Cambiar `#define SERIAL_DEBUG 1` a `#define SERIAL_DEBUG 0`. El UART del ASR6501 consume ~0.5-1 mA continuo si esta habilitado. |
| GPIOs flotantes (fugas de ~100 uA por pin) | El codigo ya configura GPIO1, GPIO4, GPIO5 como `INPUT_PULLDOWN`. Verificar que no haya pines adicionales sin configurar en otras revisiones de placa. |
| Sesion LoRaWAN no persiste (re-join en cada ciclo) | Verificar `keepNet = LORAWAN_NET_RESERVE`. Si `keepNet = false`, cada ciclo hace OTAA join (~5-10s de radio encendida). Esto multiplica el consumo por 50-100x. |
| ADR habilitado con confirmed uplinks | Deshabilitar ADR: `LORAWAN_ADR=ADR_OFF`. Los confirmed uplinks requieren RX1/RX2 (~1-2s extra de radio encendida). |
| USB conectado permanentemente (enmascara el consumo real, pero no lo aumenta) | Medir consumo con USB desconectado, multimetro en serie con bateria. El USB suministra 5V que no pasa por el LDO. |
| LED de JOIN encendido continuamente (si JOIN falla y reintenta) | Resolver el problema de JOIN (ver seccion 1). El LED consume ~2-5 mA. |
| DHT11 siempre encendido (GPIO3 en HIGH constante) | Verificar que `digitalWrite(DHT11_POWER_PIN, LOW)` se ejecuta en todas las ramas de codigo. Si GPIO3 queda en HIGH, el DHT11 consume ~1-3 mA continuamente. |
| Radio no entra en sleep | Verificar que `Radio.Sleep()` se ejecuta antes de `LoRaWAN.sleep()`. La funcion `LoRaWAN.sleep()` del framework ya deberia apagar la radio, pero una llamada explicita a `Radio.Sleep()` es mas segura. |

### 4. No se reciben datos en el Network Server (pero JOIN fue exitoso)

**Sintomas:** Monitor serial muestra `Payload [4B]: ...` pero los datos no aparecen en el Network Server.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| Puerto LoRaWAN incorrecto en el decodificador | El firmware usa puerto 1. Verificar que el decodificador este configurado para ese puerto, o que use el codec por defecto para todos los puertos. |
| Payload codec no configurado o con errores | Configurar el decodificador JavaScript en el Device Profile. Verificar que no tenga errores de sintaxis (usar la consola del navegador para probar). |
| Colision con otro nodo | El jitter aleatorio (`randr(0, APP_TX_DUTYCYCLE_RND)`) reduce colisiones. Si hay muchos nodos (> 50 en misma sub-banda), considerar espaciar intervalos. |
| Gateway desconectado del Network Server | Verificar conectividad IP del gateway. En ChirpStack, verificar que el gateway aparece como "online" y "last seen" es reciente. |
| Frecuencia incorrecta del gateway | El gateway Milesight UG56 debe estar configurado para US915, sub-banda 1 (canales 0-7, 902.3-903.7 MHz). Verificar en la configuracion del gateway. |
| Frame counter reset | Si el dispositivo pierde la sesion (frame counters en 0) y el Network Server espera contadores mas altos, los frames se descartan. Hacer un re-join forzado (borrar sesion en el Network Server). |
| Payload demasiado corto/largo | Verificar que el payload reportado en el Network Server es exactamente 4 bytes. Si es diferente, hay un problema de codificacion. |

### 5. Error de compilacion: librerias no encontradas

**Sintomas:** `arduino-cli compile` falla con errores de includes.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| Board package no instalado | `arduino-cli core install heltec_cubecell:cubecell` |
| FQBN incorrecto | Usar `heltec_cubecell:cubecell:HTCC-AB02A` exactamente. Verificar con `arduino-cli board listall \| grep HTCC-AB02A`. |
| Indice de placas desactualizado | `arduino-cli core update-index` antes de instalar. |
| `credentials.h` no encontrado | El archivo debe existir en el directorio del proyecto. Si se elimino, recrearlo con los placeholders (arrays de 0x00). |
| Version incompatible de arduino-cli | Actualizar: `arduino-cli upgrade`. Se requiere v0.35.0 o superior. |
| Error de sintaxis tras modificar codigo | Restaurar desde el repositorio original y aplicar cambios incrementalmente, compilando tras cada cambio. |

### 6. El dispositivo se reinicia constantemente (watchdog o BOD)

**Sintomas:** Monitor serial muestra el banner de inicio repetidamente.

**Causas probables y soluciones:**

| Causa | Solucion |
|---|---|
| Bateria baja (< 2.5 V) - Brown-Out Detector | Reemplazar bateria. El BOD del ASR6501 se activa cuando el voltaje cae por debajo del umbral (~2.5-2.7V). |
| Voltage sag durante TX LoRa | La ER14250 tiene resistencia interna de 10-30 Ohm. El pulso de 30 mA causa una caida de 0.3-0.9V. Si la bateria esta vieja o fria, la caida puede activar el BOD. Solucion: anadir capacitor de 100-470 uF entre VBAT y GND. |
| Stack overflow | Si se anade codigo adicional con arrays grandes en el stack, el stack de 1 KB puede desbordarse. Usar variables globales o `static` para arrays grandes. |
| Corrupcion de flash | Poco probable. Si ocurre, re-flashear el firmware. |

---

## Tabla de Compatibilidad

### Compatibilidad con placas Arduino / compatibles

| Placa | Compatibilidad | Notas |
|---|---|---|
| **Heltec CubeCell HTCC-AB02A** | **:white_check_mark: Soportada (objetivo primario)** | FQBN: `heltec_cubecell:cubecell:HTCC-AB02A` |
| Heltec CubeCell HTCC-AB01 | :white_check_mark: Compatible (requiere ajuste de pines) | Mismo MCU ASR6501. Verificar mapeo de GPIO2 y GPIO3. El pinout puede diferir ligeramente. |
| Heltec CubeCell HTCC-AB02 | :white_check_mark: Compatible (misma placa, sin holder de bateria) | Requiere bateria externa. Mismo pinout. FQBN identico. |
| Heltec CubeCell Capsule | :warning: Parcial (requiere reasignacion de pines) | Mismo ASR6501 pero pinout diferente. Los pines GPIO2/GPIO3 pueden estar en otras posiciones. Requiere modificar `DHT11_POWER_PIN` y `DHT11_DATA_PIN`. |
| Heltec CubeCell Dev-Board Plus | :warning: Parcial | Mismo ASR6501, pinout tipo Arduino UNO. Requiere reasignar pines. |
| Arduino Uno + Dragino LoRa Shield | :x: No compatible | AVR vs ARM. Deep sleep completamente diferente. Consumo mucho mayor (~mA en lugar de ~uA). Framework LoRaWAN incompatible. |
| ESP32 + SX1276 (TTGO LoRa32, Heltec WiFi LoRa) | :x: No compatible | Arquitectura ESP32 diferente. Codigo LoRaWAN y deep sleep requieren reescritura completa. |
| Arduino MKR WAN 1310 | :x: No compatible | MCU SAMD21, radio Murata CMWX1ZZABZ (SX1276). Framework LoRaWAN diferente (MKRWAN library). |
| STM32WL (Nucleo-WL55JC) | :x: No compatible | MCU STM32WL, radio integrada. Framework STM32CubeWL. |
| Raspberry Pi Pico + SX1262 | :x: No compatible | RP2040 Cortex-M0+ pero sin framework LoRaWAN nativo. Requiere port completo. |

### Compatibilidad con regiones LoRaWAN

| Region | Codigo | Compatibilidad | Configuracion necesaria |
|---|---|---|---|
| **US915** | REGION_US915 | :white_check_mark: **Soportada (por defecto)** | `LORAWAN_REGION=REGION_US915` en Arduino IDE Tools |
| AU915 | REGION_AU915 | :white_check_mark: Compatible | Cambiar a `REGION_AU915`. Canales similares a US915 (915-928 MHz). |
| EU868 | REGION_EU868 | :white_check_mark: Compatible | Cambiar a `REGION_EU868`. Implica duty cycle < 1% (este firmware cumple: ~0.008%). |
| AS923 | REGION_AS923 | :white_check_mark: Compatible | Cambiar a `REGION_AS923`. Verificar canales especificos del pais (AS923-1, AS923-2, AS923-3). |
| IN865 | REGION_IN865 | :white_check_mark: Compatible | Cambiar a `REGION_IN865`. Banda 865-867 MHz. |
| KR920 | REGION_KR920 | :white_check_mark: Compatible | Cambiar a `REGION_KR920`. Banda 920-923 MHz. |
| CN470 | REGION_CN470 | :white_check_mark: Compatible | Cambiar a `REGION_CN470`. Banda 470-510 MHz. |
| EU433 | REGION_EU433 | :white_check_mark: Compatible | Cambiar a `REGION_EU433`. Banda 433 MHz. |

> **Nota sobre el cambio de region:** Al cambiar de region LoRaWAN en el board package, el firmware debe recompilarse. La sesion OTAA previa no sera valida en la nueva region -- el dispositivo hara un nuevo JOIN.

### Compatibilidad con sensores DHT y alternativas

| Sensor | Compatibilidad | Notas |
|---|---|---|
| **DHT11** | :white_check_mark: **Soportado (por defecto)** | Lectura bit-bang integrada en el codigo. Protocolo one-wire de 40 bits. |
| DHT22 / AM2302 | :white_check_mark: Compatible con cambio menor | El protocolo es identico. Requiere modificar la interpretacion de datos: DHT22 tiene 1 decimal de precision real, y el bit de signo para temperaturas negativas. La funcion `leerDHT11()` necesitaria ajustarse para DHT22: la temperatura se codifica como `data[2]&0x80 ? -(data[2]&0x7F)*10+data[3] : data[2]*10+data[3]`. |
| AM2301 | :white_check_mark: Compatible (identico al DHT22) | Protocolo one-wire identico. |
| BME280 (I2C) | :x: Requiere reescritura significativa | Protocolo I2C, no one-wire. Requiere anadir libreria Adafruit BME280 y cambiar completamente la logica de lectura. Consumo en sleep ~0.1 uA. Precision +-1 C. |
| DS18B20 (OneWire) | :x: Requiere reescritura | Protocolo Dallas OneWire, completamente diferente del protocolo DHT. Requiere libreria OneWire + DallasTemperature. Alcance de temperatura: -55 a +125 C. |
| SHT30/SHT31 (I2C) | :x: Requiere reescritura | Protocolo I2C. Mejor precision (+-0.3 C) que DHT11. Requiere libreria Adafruit SHT31. |
| LM35 (analogico) | :warning: Posible con modificaciones | Sensor analogico. Requiere lectura ADC (`analogRead()`). Precision +-0.5 C. No requiere protocolo digital. |

### Compatibilidad con gateways LoRaWAN

| Gateway | Compatibilidad | Notas |
|---|---|---|
| **Milesight UG56** | :white_check_mark: **Compatible (objetivo)** | US915, 8 canales. Soportado por ChirpStack, TTN, Helium. |
| Milesight UG65 | :white_check_mark: Compatible | US915, 8 canales, igual que UG56 pero en formato interior. |
| RAKwireless RAK7258/RAK7268 | :white_check_mark: Compatible | US915, 8 canales. Soportado por ChirpStack integrado. |
| Laird Sentrius RG1xx | :white_check_mark: Compatible | US915, 8 canales. |
| Dragino LPS8 | :white_check_mark: Compatible | US915, 8 canales. Gateway open-source. |
| The Things Indoor Gateway | :white_check_mark: Compatible | US915, 8 canales. Optimizado para TTN. |
| Helium Hotspot (varios fabricantes) | :white_check_mark: Compatible | US915. Verificar que el hotspot tenga cobertura en la ubicacion. |

---

## Especificaciones de Bateria

### ER14250 - Li-SOCl2 (Cloruro de Tionil-Litio)

La bateria ER14250 es una celda primaria (NO recargable) de formato 1/2 AA con quimica de cloruro de tionil-litio. Es la bateria ideal para aplicaciones de bajo consumo de larga duracion debido a su alta densidad energetica, curva de descarga plana y baja autodescarga.

### Caracteristicas electricas

| Parametro | Valor | Condiciones |
|---|---|---|
| Voltaje nominal | 3.60 V | Circuito abierto, 25°C |
| Voltaje de corte | 2.50 V | Bajo carga tipica de 1 mA |
| Capacidad nominal | 1200 mAh | 25°C, descarga 1 mA continua hasta 2.0V |
| Capacidad a 0.1 mA | ~1300 mAh | 25°C, descarga ultra-baja |
| Corriente maxima continua | 50 mA | Sin degradacion significativa |
| Corriente de pulso maxima | 100 mA | < 1 segundo, 25°C |
| Resistencia interna | 10-30 Ohm | Nueva, @ 25°C. Aumenta con la edad y el frio. |
| Autodescarga | < 1% anual | @ 25°C (0.5-0.8% tipico) |
| Densidad energetica | ~500 Wh/kg | Muy alta comparada con otras quimicas |
| Rango de temperatura | -60°C a +85°C | Operacion y almacenamiento |
| Vida util (almacenamiento) | > 10 anos | @ 25°C, > 90% capacidad residual |

### Curva de descarga tipica

```
Voltaje (V)
  3.8 │
      │  ╔══════════════════════════════════════════════════╗
  3.6 │──╣  Meseta de voltaje (~90% de la vida util)        ║
      │  ║  Voltaje casi constante: 3.6V -> 3.4V            ║
  3.4 │──╣                                                  ╠═══╗
      │  ║                                                  ║   ║
  3.2 │  ║                                                  ║   ║
      │  ║                                                  ║   ║  Caida al
  3.0 │  ║                                                  ║   ║  final de
      │  ║                                                  ║   ║  vida
  2.8 │  ║                                                  ║   ╚═══╗
      │  ║                                                  ║       ║
  2.6 │  ╚══════════════════════════════════════════════════╝       ║
      │                                                              ║
  2.4 │                                                              ╚════
  2.2 │  Corte del LDO: ~2.5V (dispositivo deja de funcionar)
      └─────────────────────────────────────────────────────────────────
       0%                Tiempo de descarga                       100%

   Caracteristicas clave de Li-SOCl2:
   - La meseta plana (3.6V -> 3.4V) dura ~90% de la vida util
   - Esto significa que el dispositivo opera a voltaje casi constante
     durante la mayor parte de su vida
   - La caida final es abrupta: en pocos dias pasa de 3.4V a <2.5V
   - Esto permite predecir el momento de reemplazo con precision
```

### Impacto de la temperatura en la capacidad

| Temperatura | Capacidad relativa | Efecto en autonomia | Notas |
|---|---|---|---|
| +25°C | 100% | Nominal (~5.2 anos) | Temperatura de referencia |
| +10°C | ~90% | ~4.7 anos | Ligera reduccion |
| 0°C | ~80% | ~4.1 anos | Aceptable para la mayoria de aplicaciones |
| -10°C | ~70% | ~3.6 anos | La resistencia interna aumenta significativamente |
| -20°C | ~50% | ~2.6 anos | Voltage sag durante TX puede ser problematico |
| -30°C | ~30% | ~1.5 anos | Requiere capacitor de desacople para TX |
| -40°C | ~20% | ~1.0 anos | Borde de la especificacion. No recomendado |
| +40°C | ~102% | ~5.3 anos | Ligero aumento de capacidad (pero mayor autodescarga) |
| +60°C | ~105% | Mayor autodescarga | La autodescarga se acelera (~2-3% anual) |

> **Importante sobre temperatura y voltage sag:** A bajas temperaturas (< -10°C), la resistencia interna de la ER14250 aumenta significativamente (puede duplicarse o triplicarse). Los pulsos de corriente altos (TX LoRa ~30 mA) pueden causar caidas de voltaje de 0.5-1.5V, lo que podria activar el Brown-Out Detector del ASR6501 (~2.5-2.7V) y causar un reset. Para despliegue en exteriores frios (< 0°C), se recomienda anadir un supercapacitor de 100-470 uF (3.3V o 5V rating) en paralelo con la bateria para absorber los picos de corriente.

### Alternativas de bateria compatibles

| Modelo | Quimica | Voltaje | Capacidad | Formato | Notas |
|---|---|---|---|---|---|
| **ER14250** | Li-SOCl2 | 3.6V | 1200 mAh | 1/2 AA | **Recomendada.** Holder integrado en placa. |
| ER14505 | Li-SOCl2 | 3.6V | 2400 mAh | AA | Requiere holder externo. El doble de capacidad. |
| CR123A | Li-MnO2 | 3.0V | 1500 mAh | CR123A | Requiere holder externo. Voltaje ligeramente menor. |
| LS14250 | Li-SOCl2 | 3.6V | 1200 mAh | 1/2 AA | Equivalente a ER14250 (distinto fabricante). |
| 2x AAA alcalinas | Alcalina | 3.0V | ~1000 mAh | AAA | No recomendado: voltaje variable, alta autodescarga, baja densidad energetica. Solo para prototipado. |
| Li-Po 1S | Li-Po | 3.7V | Variable | Personalizado | Recargable. Mayor autodescarga (~5% mensual). Requiere circuito de carga. No recomendado para > 1 ano sin mantenimiento. |

### Reemplazo de bateria

1. Extraer la bateria agotada del holder (deslizar hacia afuera).
2. Insertar una nueva ER14250, observando polaridad (+ hacia arriba en el holder de la HTCC-AB02A, normalmente marcado con un signo + en la placa).
3. El dispositivo hara cold boot automaticamente (la sesion RAM se pierde, pero el firmware en Flash se conserva).
4. El dispositivo hara OTAA join automaticamente en el primer ciclo tras el reemplazo (esto es normal y solo ocurre una vez).
5. Verificar en el Network Server que el JOIN es exitoso y los datos se reciben.
6. **NUNCA** intentar recargar una ER14250. Es una bateria primaria de un solo uso. La recarga de Li-SOCl2 puede causar:
   - Explosion o incendio
   - Fuga de cloruro de tionilo (SOCl2), compuesto altamente toxico y corrosivo
   - Generacion de gases toxicos (SO2, HCl)

### Desecho de baterias

Las baterias Li-SOCl2 contienen materiales peligrosos (litio metalico, cloruro de tionilo). Deben desecharse segun la normativa local de residuos peligrosos:
- **EE.UU.:** EPA Resource Conservation and Recovery Act (RCRA). Buscar puntos de recoleccion de baterias.
- **Europa:** Directiva 2006/66/EC sobre pilas y acumuladores. Depositar en contenedores de reciclaje de pilas.
- **Internacional:** Seguir normativa local. No tirar a la basura domestica. No incinerar.

---

## Cumplimiento Normativo

### FCC (EE.UU. - US915)

El dispositivo opera en la banda ISM de 902-928 MHz bajo las reglas FCC Parte 15.247 (Digital Modulation Systems). El Heltec CubeCell HTCC-AB02A utiliza el modulo SX1262 de Semtech, que cuenta con certificacion modular FCC. La potencia de transmision esta limitada a +30 dBm para FHSS/DTS, con un maximo de +14 dBm configurado en este firmware para cumplimiento conservador.

- FCC ID: Depende del modulo SX1262 y la certificacion de Heltec. Consultar la documentacion del fabricante.
- Potencia maxima configurada: +14 dBm (muy por debajo del limite de +30 dBm).
- Duty cycle efectivo: ~0.008% (46 ms cada 600 s).

### CE (Europa - EU868)

Si se recompila para EU868, el dispositivo debe cumplir con:
- ETSI EN 300.220 (banda 863-870 MHz)
- Duty cycle < 1% por sub-banda (este firmware: ~0.008%, cumple ampliamente)
- Potencia maxima: +14 dBm (cumple)
- El cumplimiento CE depende de la certificacion general de la placa Heltec CubeCell. Verificar la documentacion del fabricante.

### ISED (Canada - US915)

La banda 902-928 MHz en Canada esta regulada por ISED RSS-247. Los requisitos son similares a FCC Parte 15.247. El dispositivo debe cumplir con los limites de potencia y emisiones fuera de banda. La certificacion modular del SX1262 generalmente cubre el cumplimiento ISED.

### Seguridad de bateria (normativa internacional)

- **IEC 60086-4:** Seguridad de baterias de litio primarias.
- **UN 38.3:** Transporte de baterias de litio (requerido para envio aereo).
- **UL 1642:** Estandar de seguridad para baterias de litio (aplicable en EE.UU.).
- Las baterias ER14250 de fabricantes reconocidos (Saft, Tadiran, Eve, Varta) cumplen con estas normativas.

### RoHS / WEEE (Union Europea)

El dispositivo utiliza componentes que cumplen con la directiva RoHS (Restriction of Hazardous Substances). El desecho del dispositivo debe realizarse segun la directiva WEEE (Waste Electrical and Electronic Equipment). La bateria ER14250 puede contener compuestos que requieren manejo especial como residuo peligroso.

---

**Documentacion de usuario - Version 1.0 - Proyecto CubeCell DHT11 LoRaWAN Sensor**

*Ultima actualizacion: 2026-08-05*
