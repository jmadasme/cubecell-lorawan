# Datasheet — HTCC-AB02A Nodo de Temperatura LoRaWAN (v1.2)

> Documentación de usuario y especificaciones técnicas del firmware **HTCC_AB02A_DS18B20**.

---

## 1. Resumen

Nodo sensor LoRaWAN **Clase A** de **temperatura**, basado en la placa **Heltec CubeCell
HTCC-AB02A** (ASR6502 + SX1262), que lee un sensor **DS18B20** (cápsula sumergible 1 m)
por protocolo **OneWire** (bit-banging manual), reporta temperatura y batería por
LoRaWAN (**AU915 sub-banda 2**, **OTAA**) y duerme 10 minutos en **deep sleep real** con
**power gating** del sensor.

---

## 2. Especificaciones técnicas

### 2.1 Plataforma

| Parámetro | Valor |
|---|---|
| Placa | Heltec CubeCell HTCC-AB02A |
| SoC | ASR6502 (ARM Cortex-M0+, 48 MHz) |
| Memoria | 128 KB Flash / 16 KB RAM |
| Radio | SX1262 (integrada en el SiP) |
| Librería RF | RadioLib **7.7.1** |

### 2.2 LoRaWAN

| Parámetro | Valor |
|---|---|
| Región | **AU915** |
| Sub-banda | **2** |
| Activación | **OTAA** |
| Clase | **A** (uplink no confirmado) |
| Puerto uplink | **1** |
| Puerto downlink | **1** |
| Ciclo de envío | **600000 ms (10 min)** |
| `node.scanGuard` | 200 ms (workaround de timers) |

### 2.3 Sensor

| Parámetro | Valor |
|---|---|
| Sensor | DS18B20 (cápsula sumergible 1 m) |
| Bus | OneWire (open-drain, bit-banging manual) |
| Resolución | **12 bits** (0.0625 °C/LSB) fija |
| Rango | −55 °C … +125 °C |
| Tiempo de conversión | 750 ms |
| Alimentación | 3.0–5.5 V (power gating por GPIO) |
| Pull-up | Externo **4.7 kΩ** entre DQ y VDD del sensor |

### 2.4 Consumo estimado

| Estado | Consumo aprox. |
|---|---|
| Deep sleep (MCU + radio + sensor) | MCU deep sleep + radio ~0.6 µA + sensor ~0 mA |
| Medición (conversión 750 ms) | ~1.5–4 mA (sensor) + MCU activo |
| Transmisión uplink | Pico de corriente del SX1262 (depende de TX power) |

### 2.5 Condiciones ambientales

| Parámetro | Valor |
|---|---|
| Temperatura de operación (electrónica) | Según HTCC-AB02A (−20 … +70 °C típico) |
| Sensor sumergible | Sellado, apto para inmersión 1 m |

---

## 3. Conexionado (pinout)

### 3.1 DS18B20 → HTCC-AB02A

| Hilo sensor | Color típico | Pin placa | Función |
|---|---|---|---|
| VDD | Rojo | `GPIO3` / **P6_2** | Alimentación (power gating) |
| GND | Negro | GND | Masa |
| DQ | Amarillo | `GPIO4` / **P6_4** | Datos OneWire |
| — | — | (DQ ↔ VDD) | Pull-up externo 4.7 kΩ |

### 3.2 Pines internos/heredados

| Función | Pin |
|---|---|
| SX1262 NSS | 35 (P4_3) |
| SX1262 DIO1 | 38 (P4_6) |
| SX1262 RST | 47 (P5_7) |
| SX1262 BUSY | 39 (P4_7) |
| Salida downlink | `GPIO1` (P6_0) |
| Batería | P2_0 (ADC interno) |

---

## 4. Protocolo de aplicación (formato del payload)

### 4.1 Uplink — puerto 1, 4 bytes, big-endian, no confirmado

| Byte | Bit(s) | Campo | Tipo | Descripción |
|---|---|---|---|---|
| 0 | [15:8] | Temperatura (MSB) | `int16` BE | Byte alto de la temperatura en décimas de °C |
| 1 | [7:0] | Temperatura (LSB) | `int16` BE | Byte bajo de la temperatura en décimas de °C |
| 2 | [15:8] | Batería (MSB) | `uint16` BE | Byte alto del voltaje de batería en mV |
| 3 | [7:0] | Batería (LSB) | `uint16` BE | Byte bajo del voltaje de batería en mV |

**Temperatura**: `int16` en **complemento a dos**, en **décimas de °C**.
- `234` → `23.4 °C`
- `-55` → `-5.5 °C`
- `0` → `0.0 °C`
- Rango válido: **−550 … +1250** (equivalente a −55.0 … +125.0 °C).

**Sentinel de error**: `0x7FFF` (32767) → sensor **desconectado / cortocircuito / CRC
inválido / scratchpad insano**. No es un valor físico.

**Batería**: `uint16` en **mV**, sin signo.

### 4.2 Ejemplos de payload decodificado

| Bytes (hex) | Temperatura | Batería | Resultado |
|---|---|---|---|
| `00 EA 0E 4C` | 234 (0x00EA) | 3660 mV | `temperatura_C = 23.4`, `bateria_mV = 3660` |
| `FF C9 0E 4C` | -55 (0xFFC9) | 3660 mV | `temperatura_C = -5.5`, `bateria_mV = 3660` |
| `00 00 0E 4C` | 0 | 3660 mV | `temperatura_C = 0.0`, `bateria_mV = 3660` |
| `7F FF 0E 4C` | Sentinel | 3660 mV | `temperatura_C = null`, `error = SENSOR_DS18B20_INVALIDO` |
| `05 3C 0E 4C` | 1340 (0x053C) | 3660 mV | `temperatura_C = 134.0` (fuera de rango físico) |

### 4.3 Downlink — puerto 1, 1 byte (opcional)

| Byte | Comando | Acción en el nodo |
|---|---|---|
| `0x01` | ON | `GPIO1` (P6_0) → HIGH |
| `0x00` | OFF | `GPIO1` (P6_0) → LOW |
| `0x02` | TOGGLE | `GPIO1` (P6_0) → invierte estado |
| otros | — | Ignorado (log "comando desconocido") |

---

## 5. Codec (decoder.js) — ChirpStack v4

El codec implementa la convención de **ChirpStack v4**:
- `Decode(fPort, bytes, variables)` → `{ data: {...}, errors: [...] }`
- `Encode(fPort, obj, variables)` → `{ bytes: [...] }`

> ⚠️ **No** confundir con la convención de The Things Stack v3 (`decodeUplink` /
> `encodeDownlink`). Este codec usa `Decode`/`Encode`, que es lo que exige ChirpStack v4.

### 5.1 Código completo

```javascript
/*
 * decoder.js — Codec ChirpStack v4 para HTCC_AB02A_DS18B20
 * =========================================================
 * Firmware v1.2 — payload uplink (puerto 1, big-endian, 4 bytes):
 *   [0:1] temperatura  int16  BE  décimas de °C (complemento a dos)
 *         234  -> 23.4 °C       FF C9 (-55) -> -5.5 °C
 *         Sentinel 0x7FFF (32767) = sensor desconectado/corto/CRC inválido
 *         -> se expone como temperatura_C = null + flag de error.
 *   [2:3] batería      uint16 BE  mV
 *
 * Downlink (puerto 1, opcional):
 *   Encode({ gpio1: 1|0|2 }) -> 1 byte que comanda GPIO1/P6_0
 *   (0x01 ON, 0x00 OFF, 0x02 TOGGLE)
 *
 * Uso: copiar en ChirpStack v4 (Device Profile -> Codec).
 */

// Convierte el payload a una cadena hex (para reportar puertos no soportados).
function toHex(bytes) {
    var hex = '';
    var buf = Array.isArray(bytes) ? bytes : Array.prototype.slice.call(bytes);
    for (var i = 0; i < buf.length; i++) {
        var b = buf[i] & 0xFF;
        hex += (b < 16 ? '0' : '') + b.toString(16);
    }
    return hex;
}

function Decode(fPort, bytes, variables) {
    var decoded = {};
    var errors = [];

    var buf = Array.isArray(bytes) ? bytes : Array.prototype.slice.call(bytes);

    // Validar puerto. Este dispositivo solo reporta telemetría por el puerto 1.
    if (fPort !== 1) {
        return {
            data: { raw: toHex(buf) },
            errors: errors
        };
    }

    // Validar longitud: 4 bytes (temperatura int16 BE + batería uint16 BE).
    if (buf.length !== 4) {
        errors.push("Payload debe tener 4 bytes (temperatura int16 BE + bateria uint16 BE), recibio " + buf.length);
        return { data: decoded, errors: errors };
    }

    // Temperatura: int16 big-endian en décimas de °C (complemento a dos)
    var rawTemp = (buf[0] << 8) | buf[1];
    if (rawTemp >= 0x8000) {
        rawTemp -= 0x10000;
    }

    // Batería: uint16 big-endian en mV
    var bateriaMv = (buf[2] << 8) | buf[3];

    if (rawTemp === 0x7FFF) {
        decoded["temperatura_C"] = null;
        decoded["error"] = "SENSOR_DS18B20_INVALIDO";   // desconectado/corto/CRC inválido
    } else {
        decoded["temperatura_C"] = round2(rawTemp / 10.0);
    }
    decoded["bateria_mV"] = bateriaMv;
    decoded["bateria_V"]  = round2(bateriaMv / 1000.0);

    return {
        data: decoded,
        errors: errors
    };
}

function Encode(fPort, obj, variables) {
    // Downlink opcional: { gpio1: 1 | 0 | 2 } comanda GPIO1/P6_0
    var bytes = [];

    if (fPort !== 1) {
        return { bytes: bytes };
    }
    if (obj === null || typeof obj !== "object") {
        return { bytes: bytes };
    }

    var cmd = null;
    if (obj.gpio1 !== undefined && obj.gpio1 !== null) {
        cmd = Number(obj.gpio1);
    } else if (obj.output !== undefined && obj.output !== null) {
        cmd = Number(obj.output);
    }

    if (cmd === 0 || cmd === 1 || cmd === 2) {
        bytes.push(cmd);
    }

    return { bytes: bytes };
}

function round2(v) {
    return Math.round(v * 100) / 100;
}
```

### 5.2 Salida de `Decode` (ejemplos)

**Payload `00EA 0E4C`:**
```json
{
  "data": {
    "temperatura_C": 23.4,
    "bateria_mV": 3660,
    "bateria_V": 3.66
  },
  "errors": []
}
```

**Payload `FFC9 0E4C` (temperatura negativa):**
```json
{
  "data": {
    "temperatura_C": -5.5,
    "bateria_mV": 3660,
    "bateria_V": 3.66
  },
  "errors": []
}
```

**Payload `7FFF 0E4C` (sentinel / sensor inválido):**
```json
{
  "data": {
    "temperatura_C": null,
    "error": "SENSOR_DS18B20_INVALIDO",
    "bateria_mV": 3660,
    "bateria_V": 3.66
  },
  "errors": []
}
```

### 5.3 Uso de `Encode` (downlink)

Para enviar un comando a `GPIO1`, el NS llama a `Encode(1, { gpio1: 1 })`, que produce
`{ bytes: [1] }` (ON). Valores aceptados: `1` (ON), `0` (OFF), `2` (TOGGLE). También
acepta el alias `{ output: n }`.

---

## 6. Dependencias

Este proyecto **no usa dependencias de Python** (no existe `requirements.txt`). Las
únicas dependencias son:

| Dependencia | Versión | Tipo | Instalación |
|---|---|---|---|
| **RadioLib** | 7.7.1 | Librería Arduino (firmware) | Gestor de Librerías Arduino / PlatformIO |
| **Core CubeCell** (Heltec) | — | Soporte de placas | Gestor de placas de Heltec |

Instalación de RadioLib:

- **Arduino IDE**: *Herramientas → Gestor de Librerías* → buscar "RadioLib" → instalar
  la versión 7.7.1 (o la más reciente compatible).
- **PlatformIO**: añadir a `platformio.ini`:
  ```ini
  lib_deps =
      jgromes/RadioLib@^7.7.1
  ```

El codec `decoder.js` no requiere dependencias: es JavaScript puro ejecutado por el
motor del Network Server.

---

## 7. Guía de solución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| El nodo no se une (no aparece `JOINED`) | Credenciales OTAA incorrectas o no registradas en el NS; región/sub-banda errónea | Verificar `DevEUI`/`JoinEUI`/`AppKey`; registrar el `DevEUI` en el NS; confirmar AU915 SB2 y cobertura del gateway |
| `temperatura_C = null` con `SENSOR_DS18B20_INVALIDO` | Sensor desconectado, DQ en corto a GND, CRC inválido o scratchpad insano | Revisar cableado del DS18B20 (VDD, DQ, GND), verificar pull-up 4.7 kΩ, verificar que el sensor esté conectado |
| Temperatura fija en 0.0 °C | DQ en corto a GND (scratchpad todo-ceros) | El firmware detecta esta condición (sanidad del scratchpad) y reporta el sentinel; revisar el cableado de DQ |
| CRC inválido intermitente | Deriva de timing del bit-bang en el core CubeCell | Ver fallback de timing en `docs/developer.md`; verificar ≥10 lecturas consecutivas sin error |
| El nodo se queda colgado tras arrancar | Fallo de init del radio/nodo | `fatalErrorReset()` reinicia el nodo; verificar alimentación y conexión del radio |
| No llega el downlink a `GPIO1` | `sendReceive` sin ventana RX abierta, o comando con valor incorrecto | Enviar downlink por puerto 1 con `0x01`/`0x00`/`0x02`; verificar que el NS lo encola en la ventana RX de Clase A |
| Batería reportada anómala | ADC mal calibrado o batería descargada | Verificar el ADC interno `P2_0` y el circuito de batería de la placa |
| Consumo alto en sueño | VDD del sensor atado al 3.3 V fijo, o pull-up al 3.3 V fijo | Conectar VDD al pin de power gating (`GPIO3/P6_2`) y el pull-up a VDD del sensor (no al 3.3 V fijo) |

---

## 8. Parámetros de configuración clave (resumen)

| Constante | Valor | Descripción |
|---|---|---|
| `SLEEP_INTERVAL_MS` | `600000` | Ciclo de envío (10 min) |
| `DS18B20_POWERUP_SETTLE_MS` | `10` | Estabilización tras encender VDD |
| `DS18B20_CONV_TIME_MS` | `750` | Tiempo de conversión 12 bits |
| `DS18B20_INVALID` | `0x7FFF` | Sentinel de error de lectura |
| `DS18B20_CMD_CONVERT` | `0x44` | Comando Convert T |
| `DS18B20_CMD_READ_SCRATCH` | `0xBE` | Comando Read Scratchpad |
| `DS18B20_CMD_SKIP_ROM` | `0xCC` | Comando Skip ROM (1 solo sensor) |

---

*Documentación de usuario generada a partir de `PLAN.md`, `HTCC_AB02A_DS18B20.ino` y
`decoder.js` (firmware v1.2).*
