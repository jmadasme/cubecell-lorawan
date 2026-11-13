# Datasheet — HTCC-AB02A Nodo Sensor LoRaWAN Low-Power (v3.0, RadioLib)

Documentación de usuario del nodo sensor LoRaWAN de bajo consumo basado en **Heltec CubeCell HTCC-AB02A** (ASR6502 + SX1262), que reporta entradas digitales y analógicas a un gateway **Milesight UG56** con servidor de red embebido, y acepta comandos de downlink para una salida digital.

---

## Información General

### Descripción del producto

| Campo | Valor |
|-------|-------|
| **Nombre del firmware** | HTCC_AB02A_LoRaWAN **v3.0** |
| **Plataforma** | Heltec CubeCell HTCC-AB02A ("CubeCell 1/2AA Node") |
| **Tipo de dispositivo** | Nodo sensor LoRaWAN **Clase A** (low-power) |
| **Caso de uso principal** | Telemetría de entradas digitales/analógicas + actuación remota por downlink |
| **Stack LoRaWAN** | RadioLib **7.7.1** (`LoRaWANNode` + `SX1262`) — no el stack nativo CubeCell |
| **Región / subbanda** | AU915, subbanda 2 (canales 8–15) |
| **Activación** | OTAA (Over-The-Air Activation) |
| **Clase** | A (bi-direccional con ventanas RX tras cada uplink) |
| **Puerto de aplicación** | 1 |
| **Ciclo de operación** | 10 min (deep sleep → medir → uplink no confirmado → dormir) |

### Funcionalidades

| Funcionalidad | Detalle |
|---------------|---------|
| Join OTAA | AU915 subbanda 2, LoRaWAN 1.0.x |
| Ciclo low-power | 10 min con deep sleep real (`CySysPmDeepSleep` + RTC) |
| 6 entradas digitales | Pull-up interno, empaquetadas en 1 byte de bits |
| 2 entradas analógicas | 0–2400 mV (ADC2=P2_1, ADC3=P2_2) |
| 1 salida digital | GPIO1 (P6_0), comandada por downlink (ON/OFF/TOGGLE) |
| Batería | Voltaje en mV (ADC 12-bit, divisor ×2, promedio ×50) |
| Uplink no confirmado | Puerto 1, payload 7 bytes big-endian |
| Re-join automático | Reintenta OTAA con backoff de 10 s |

---

## Especificaciones Técnicas

### Placa

| Parámetro | Valor |
|-----------|-------|
| Placa | Heltec CubeCell HTCC-AB02A |
| Microcontrolador | ASR6502 (ARM Cortex-M0+ @ 48 MHz) |
| Radio | SX1262 (LoRa 915 MHz) |
| RAM | 16 384 bytes (16 KB) |
| Flash | 131 072 bytes (128 KB) |
| Voltaje | 3.3 V (USB 5 V o batería 1/2AA) |
| Rango ADC analógico | 0 – 2400 mV (referencia interna 2.4 V) |
| Frecuencia | 915 MHz (AU915) |
| USB-Serial | CP210x |
| Conector de antena | U.FL/IPEX |
| Consumo en deep sleep | ~10–20 µA (MCU + radio en sleep) |

### Credenciales OTAA

| Parámetro | Valor |
|-----------|-------|
| DevEUI | `70B3D56F27CEE98B` |
| JoinEUI (AppEUI) | `24E124C0002ADD01` |
| AppKey | `89F5A1A24D63475791F8BF33EA031486` |
| NwkKey | = AppKey |
| Región | AU915 subbanda 2 |

### Pines SX1262 (internos, no reasignables)

| Pin Arduino | Señal | Pin ASR6502 |
|-------------|-------|-------------|
| 35 | NSS (CS) | P4_3 |
| 38 | DIO1 | P4_6 |
| 47 | RST | P5_7 |
| 39 | BUSY | P4_7 |

### Pines de aplicación (v3.0)

| Función | Pin Arduino | Pin ASR6502 | Alias | Modo |
|---------|-------------|-------------|-------|------|
| Salida digital | 48 | P6_0 | GPIO1 / MOSI1 | OUTPUT |
| Entrada digital 1 | 28 | P3_4 | GPIO5 | INPUT_PULLUP |
| Entrada digital 2 | 30 | P3_6 | GPIO6 | INPUT_PULLUP |
| Entrada digital 3 | 31 | P3_7 | GPIO7 | INPUT_PULLUP |
| Entrada digital 4 | 56 | P7_0 | GPIO8 / SCL1 | INPUT_PULLUP |
| Entrada digital 5 | 57 | P7_1 | GPIO9 / SDA1 | INPUT_PULLUP |
| Entrada digital 6 | 58 | P7_2 | GPIO10 | INPUT_PULLUP |
| Entrada analógica 1 | 17 | P2_1 | ADC2 | ANALOG |
| Entrada analógica 2 | 18 | P2_2 | ADC3 | ANALOG |
| Batería (interno) | 16 | P2_0 | ADC1 / ADC | ANALOG |

---

## Formato del Payload

**Longitud: 7 bytes · Puerto: 1 · Big-endian (MSB primero)**

| Byte | Campo | Tipo | Descripción |
|------|-------|------|-------------|
| 0 | `digitales` | uint8 | bit0=GPIO5, bit1=GPIO6, bit2=GPIO7, bit3=GPIO8, bit4=GPIO9, bit5=GPIO10 (1 = alto) |
| 1–2 | `adc2_mv` | uint16 BE | Voltaje ADC2 (P2_1) en mV |
| 3–4 | `adc3_mv` | uint16 BE | Voltaje ADC3 (P2_2) en mV |
| 5–6 | `battery_mv` | uint16 BE | Voltaje de batería en mV |

### Semántica de los campos

- **`digitales`** (uint8, máscara de bits): cada bit representa el estado de una entrada digital (1 = alto, 0 = bajo). Bits 6–7 reservados (siempre 0).
- **`adc2_mv` / `adc3_mv`** (uint16, sin signo, BE): voltaje en mV, rango 0–2400. `0x0000` = 0 V.
- **`battery_mv`** (uint16, sin signo, BE): voltaje en mV. Rango típico 3000–4200 (LiPo/LiSOCl₂) o ~5000 (USB). `0x0000` = no medible.

### Ejemplos

| Payload hex | digitales | ADC2 | ADC3 | Batería |
|-------------|-----------|------|------|---------|
| `0504B4033E0D00` | 0x05 (GPIO5+7 altos) | 1204 mV | 830 mV | 3328 mV |
| `3F000000000D00` | 0x3F (6 altos) | 0 mV | 0 mV | 3328 mV |

---

## Downlink (comandos a la salida GPIO1)

El primer byte del downlink en el puerto 1 comanda `GPIO1` (P6_0):

| Byte | Comando | Efecto |
|------|---------|--------|
| `0x01` | ON | GPIO1 → HIGH |
| `0x00` | OFF | GPIO1 → LOW |
| `0x02` | TOGGLE | Invierte el estado actual |
| otro | — | Ignorado |

> El downlink se entrega en la ventana RX1/RX2 **tras el siguiente uplink** (clase A). Los MAC commands (puerto 0) son gestionados internamente por RadioLib.

---

## Decoder JavaScript para Milesight UG56

El UG56 admite un **Payload Codec** en JavaScript (Network Server → Payload Codec). Configurar tanto el decoder (uplink) como, si se desea, el encoder (downlink).

> 💡 El archivo [`decoder.js`](../decoder.js) del repositorio contiene la versión **lista para pegar**. IMPORTANTE: el NS embebido de Milesight usa el formato **ChirpStack v3**, no v4:
> - Firma `Decode(fPort, bytes)` → retorna el objeto JSON **directo** (NO `{data, errors}`).
> - Firma `Encode(fPort, obj)` → retorna el array de bytes **directo** (NO `{bytes: [...]}`).
> - JavaScript **ES5** (sin `let`/`const`, sin arrow functions).

```javascript
/**
 * Codec para HTCC-AB02A LoRaWAN node v3.0 (RadioLib) — Milesight NS embebido
 * Uplink  (fPort 1): 7 bytes big-endian
 *   [0]      digitales (bit0..5 = GPIO5..GPIO10)
 *   [1:2]    adc2_mV    uint16 BE
 *   [3:4]    adc3_mV    uint16 BE
 *   [5:6]    battery_mV uint16 BE
 * Downlink (fPort 1): primer byte comanda GPIO1 (0x01=ON, 0x00=OFF, 0x02=TOGGLE)
 */

// Mapa de nombres de entradas digitales (índice de bit → pin)
var DIG_NAMES = ["GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10"];

function Decode(fPort, bytes) {
  var decoded = {};

  if (fPort === 1 && bytes.length >= 7) {
    // Entradas digitales (bits)
    var bits = bytes[0];
    decoded.digitales = bits;
    for (var i = 0; i < 6; i++) {
      decoded["digital_" + (i + 1)] = (bits >> i) & 0x01;
      decoded[DIG_NAMES[i]] = (bits >> i) & 0x01;
    }

    // Voltajes en mV (uint16 BE)
    var adc2mv = (bytes[1] << 8) | bytes[2];
    var adc3mv = (bytes[3] << 8) | bytes[4];
    var battmv = (bytes[5] << 8) | bytes[6];

    decoded.adc2_mV    = adc2mv;
    decoded.adc2_V     = Math.round(adc2mv / 10) / 100;
    decoded.adc3_mV    = adc3mv;
    decoded.adc3_V     = Math.round(adc3mv / 10) / 100;
    decoded.battery_mV = battmv;
    decoded.battery_V  = Math.round(battmv / 10) / 100;
  }

  return decoded;
}

/**
 * Encoder para downlink (comando a GPIO1).
 * entrada esperada: un objeto { "gpio1": 1|0|2 } (o "ON"/"OFF"/"TOGGLE")
 */
function Encode(fPort, obj) {
  var bytes = [];
  if (fPort === 1 && obj !== null && typeof obj === "object") {
    var cmd = (obj.gpio1 !== undefined && obj.gpio1 !== null) ? obj.gpio1
            : (obj.output !== undefined && obj.output !== null) ? obj.output : null;
    if (typeof cmd === "string") cmd = cmd.toLowerCase();

    if (cmd === 1 || cmd === "on" || cmd === "high")       bytes.push(0x01);
    else if (cmd === 0 || cmd === "off" || cmd === "low")  bytes.push(0x00);
    else if (cmd === 2 || cmd === "toggle")                bytes.push(0x02);
  }
  return bytes;
}

/**
 * Ejemplo Decode para payload 3f000000000d00:
 * {
 *   digitales: 63,
 *   digital_1: 1, GPIO5: 1, digital_2: 1, GPIO6: 1,
 *   digital_3: 1, GPIO7: 1, digital_4: 1, GPIO8: 1,
 *   digital_5: 1, GPIO9: 1, digital_6: 1, GPIO10: 1,
 *   adc2_mV: 0,    adc2_V: 0,
 *   adc3_mV: 0,    adc3_V: 0,
 *   battery_mV: 3328, battery_V: 3.33
 * }
 */
```

---

## Operación

### Puesta en marcha

1. Conectar la **antena LoRa de 915 MHz** al conector U.FL/IPEX **antes** de alimentar.
2. Registrar el dispositivo en el UG56 (LoRaWAN → Device) con las credenciales de la sección anterior.
3. Alimentar por **USB** (o batería 1/2AA) y cargar el firmware (ver `README.md` → Compilación y carga).
4. Abrir el **monitor serial a 115200 baud** y verificar `[JOIN] +++ JOINED +++`.

### Comportamiento en operación

- El nodo transmite cada **10 minutos** y duerme en deep sleep el resto del tiempo (serial mudo entre uplinks).
- Cada ciclo imprime `[GPIO]`, `[ADC]` ×2, `[BATT]`, `[UPLINK]` y `[SLEEP]`.
- Si el NS responde con un downlink, se imprime `[RX]` y el comando sobre GPIO1.
- Si se pierde la sesión, se ejecuta un re-join automático (`[REJOIN]`) con backoff de 10 s.

### Verificación en el UG56

1. Interfaz web del UG56 → **LoRaWAN → Incoming Packets**.
2. Observar el `Join Request` (MHDR=0x00) y después los `Unconfirmed Data Up` del DevEUI `70B3D56F27CEE98B`.
3. El campo `digital` debe reflejar el estado de las entradas; `adc2_mV`/`adc3_mV` el voltaje aplicado; `battery_mV` el voltaje de batería.

---

## Estimación de consumo y duración de batería

El nodo está pensado para funcionar con una batería **1/2AA**. Valores de referencia (estimados; medir en campo para confirmar el quiescent exacto de la placa):

| Fase | Duración | Corriente media |
|------|----------|-----------------|
| Deep sleep (MCU + SX1262 sleep) | 600 s | ~15 µA (10–20 µA según regulador/RTC) |
| Activa (medir + TX + RX1/RX2) | ~2 s | ~20 mA (pico TX ~90 mA unos 50 ms) |

### Cálculo trabajado (batería ER14250 LiSOCl₂ 1200 mAh)

```
Energía por ciclo:
  sleep  = 0.015 mA × 600 s = 9.0 mAs   = 0.0025 mAh
  activo = 20 mA    ×   2 s = 40 mAs    = 0.0111 mAh
  total/cycle                 ≈ 0.0136 mAh

Ciclos por día = 86400 s / 600 s = 144
Consumo diario = 144 × 0.0136 ≈ 1.96 mAh/día

Duración ≈ 1200 mAh / 1.96 mAh/día ≈ 610 días ≈ 1.7 años
```

### Escenarios

| Batería | Capacidad | Consumo diario est. | Duración est. |
|---------|-----------|---------------------|---------------|
| ER14250 LiSOCl₂ 1/2AA | 1200 mAh | ~1.96 mAh/día | ~1.7 años |
| LiPo 1/2AA | 2000 mAh | ~1.96 mAh/día | ~2.8 años |

> **Notas**:
> - El dato dominante suele ser el **quiescent del regulador/LDO de la placa** (no el MCU ni la radio). Un LDO con 10 µA extra duplica el consumo en reposo. Medir el consumo real en deep sleep con un multímetro/µA-meter para refinar la estimación.
> - El **DR** (data rate) afecta al tiempo de TX: en DR0 (SF12) el uplink es más largo y consume más por ciclo; con ADR activo el NS puede subir el DR y reducir el consumo.
> - La **autodescarga** de las LiSOCl₂ es muy baja (<1 %/año), por lo que la vida útil en frío/poco uso puede ser mayor.

---

## Solución de Problemas (Troubleshooting)

| Síntoma | Causa probable | Solución |
|---------|----------------|----------|
| Serial no muestra nada | Baud/cable/puerto incorrecto | Verificar 115200 baud, cable USB, `/dev/ttyUSB0` |
| `[INIT] Radio FAILED: -705 (CHIP_NOT_FOUND)` | SPI de la radio / pines mal / core incorrecto | Verificar `Module mod(35, 38, 47, 39)` y core CubeCell 1.4.0 |
| `[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)` | Credenciales/endianidad incorrectas, gateway fuera de alcance, subbanda distinta | Verificar DevEUI/JoinEUI/AppKey exactos y big-endian; verificar AU915 **subbanda 2**; acercar el gateway |
| Join nunca progresa (sin `+++ JOINED +++`) | Workaround de timers no aplicado | Confirmar `node.scanGuard = 200` en `setup()` |
| Uplinks OK en serial pero no aparecen en UG56 | Subbanda/región del gateway distinta | Asegurar UG56 en AU915 subbanda 2 |
| `[ADC] pin=17 -> 0 mV` / `pin=18 -> 0 mV` | Sin señal aplicada o pin a GND | Aplicar 0–2.4 V entre el pin y GND |
| `[ADC]` satura en ~2400 mV | Tensión ≥ 2.4 V en la entrada | Usar divisor resistivo externo y escalar en el decoder |
| `[BATT] 0 mV` o valor incoherente | Batería no conectada / conflicto en `P3_3` | Conectar batería; no usar `P3_3` como GPIO permanente |
| Salida GPIO1 no responde al downlink | Downlink en puerto equivocado / no hubo uplink previo | Enviar downlink en **puerto 1**; el nodo solo lo recibe tras un uplink (clase A) |
| `[REJOIN]` repetido | Sesión perdida (reset del NS, cambio de credenciales) | Verificar que el NS no haya eliminado la sesión; re-join automático normal |
| Serial mudo ~10 min entre uplinks | Comportamiento esperado (deep sleep) | Es normal; el MCU duerme. Para depurar, bajar `SLEEP_INTERVAL_MS` |

---

## Tabla de Compatibilidad con otras placas Arduino

| Placa | Compatibilidad | Notas |
|-------|----------------|-------|
| **Heltec CubeCell HTCC-AB01** | ✅ Alta | Misma familia CubeCell (ASR650x + SX1262); mismos pines del SiP |
| **Heltec CubeCell Board (HTCC-AB01/AB02, 1/2AA)** | ✅ Alta | Mismo core `CubeCell:CubeCell`; ajustar FQBN |
| **Heltec CubeCell Capsule** | ⚠️ Media | Mismo ASR6502; verificar pinout del SiP y alimentación |
| **Heltec Wireless Stick / LoRa 32 (ESP32 + SX1262)** | ⚠️ Media | RadioLib soporta ambas; requiere cambiar pines, FQBN (esp32) y reimplementar el deep sleep (`esp_sleep`) |
| **TTGO T-Beam / LilyGO LoRa (ESP32)** | ⚠️ Media | RadioLib funciona, pines SX126x distintos; región AU915 compatible |
| **Arduino Uno/Nano/Leonardo (AVR)** | ❌ No recomendada | RAM insuficiente para LoRaWAN + RadioLib; sin radio LoRa onboard |
| **STM32 (Nucleo/Blue Pill)** | ⚠️ Media | RadioLib soportado; requiere SX1262 externo, mapeo de pines y adaptar el low-power |

> El **core CubeCell** (`CubeCell:CubeCell`) es requisito para `getBatteryVoltage()`, el mapeo de pines del SiP y el patrón de deep sleep (`TimerEvent_t` + `lowPowerHandler()`). En otras familias (ESP32/STM32), sustituir la lectura de batería por un `analogRead` propio, mapear los pines SX1262 y adaptar el mecanismo de sueño a la plataforma destino.
