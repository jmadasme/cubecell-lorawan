# Datasheet — CubeCell LoRaWAN Node v1.1

## Resumen

Firmware para Heltec CubeCell HTCC-AB01 que implementa un nodo LoRaWAN Clase A usando la libreria **RadioLib 7.7.1**. El nodo se activa via OTAA en la banda **AU915 sub-banda 2** y transmite un payload HEX generico configurable por el usuario. El proposito es enviar datos crudos a un gateway LoRaWAN (tipicamente Milesight UG56), que los publica por MQTT para que un servidor externo los decodifique.

---

## Especificaciones tecnicas

### Microcontrolador

| Parametro | Valor |
|-----------|-------|
| Modelo | ASR6502 (ARM Cortex M0+) |
| Frecuencia | 48 MHz |
| Flash | 128 KB |
| SRAM | 16 KB |
| ADC | 12 bits, 8 canales |
| UART | 2 |

### Radio LoRa (SX1262 integrado en ASR)

| Parametro | Valor |
|-----------|-------|
| Modelo | SX1262 (sub-GHz) |
| Frecuencia | 150 - 960 MHz |
| Potencia Tx | hasta +22 dBm |
| Sensibilidad | hasta -148 dBm (SF12, BW125) |
| Modulacion | LoRa, (G)FSK, MSK |
| Interface | SPI (NSS=GPIO35, DIO1=GPIO38, RESET=GPIO47, BUSY=GPIO39) |

### Parametros LoRaWAN

| Parametro | Valor |
|-----------|-------|
| Banda | AU915 (915-928 MHz) |
| Sub-banda | 2 (canales 8-15: 916.8 - 918.2 MHz) |
| Activacion | OTAA (Over-The-Air Activation) |
| Clase | Class A |
| Puerto | 1 |
| ADR | Habilitado |
| Intervalo | 60 segundos (configurable) |
| Payload | 4 bytes HEX (configurable) |
| SF | 10 (DR2 en AU915) |
| BW | 125 kHz |
| CR | 4/5 |

### Alimentacion

| Parametro | Valor |
|-----------|-------|
| Voltaje USB | 5 VDC |
| Voltaje operacion | 2.2 - 3.6 V (regulado a 3.3V) |
| Consumo Tx | ~80 mA (@ +14 dBm, SF10) |
| Consumo idle | ~25 mA (spin-loop) |
| Consumo sleep | ~3.5 µA (deep sleep, no implementado en v1.1) |

---

## Librerias requeridas

### RadioLib 7.7.1

La libreria principal que implementa el stack LoRaWAN completo y el driver del SX1262.

**Instalacion via arduino-cli:**
```bash
arduino-cli lib install "RadioLib@7.7.1"
```

**Instalacion via Arduino IDE:**
1. Abrir Arduino IDE
2. Sketch → Include Library → Manage Libraries
3. Buscar "RadioLib" e instalar version 7.7.1

**Fuente:** [GitHub - jgromes/RadioLib](https://github.com/jgromes/RadioLib)

### Dependencias de RadioLib

RadioLib no requiere librerias externas adicionales para el modo LoRaWAN. Incluye internamente:
- Cifrado AES-128 (para MIC y payload LoRaWAN)
- Algoritmos de bandplan (para cumplir con regulaciones regionales)
- Manejo de timers (se reemplaza por `millis()` en este firmware)

---

## Credenciales OTAA (configuracion)

El dispositivo debe registrarse en el **Network Server** (NS) del gateway antes de su uso. Para el Milesight UG56:

### En el gateway (interfaz Web):

1. `LoRa → Network Server → Device → Add`
2. Configurar:

   | Campo | Valor |
   |-------|-------|
   | **Device EUI** | `24F124725F486022` |
   | **App EUI** | `24E124C0002ADD01` |
   | **AppKey** | `A7F19C4E82D5C00F3E9A14C8D7B25F61` |
   | **Class** | `Class A` |
   | **Activation** | `OTAA` |
   | **Status** | `Enabled` |

### En el firmware:

Editar las credenciales en la seccion correspondiente del archivo `.ino`:

```cpp
uint64_t joinEui = 0x24E124C0002ADD01;
uint64_t devEui  = 0x24F124725F486022;
uint8_t appKey[] = { 0xA7, 0xF1, 0x9C, 0x4E, 0x82, 0xD5, 0xC0, 0x0F,
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 };
uint8_t nwkKey[] = { 0xA7, 0xF1, 0x9C, 0x4E, 0x82, 0xD5, 0xC0, 0x0F,
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 }; // = appKey
```

**Nota:** `nwkKey` se iguala a `appKey` cuando el NS no usa claves separadas (modo LoRaWAN 1.0.x). Si el NS requiere claves separadas, se debe configurar adecuadamente.

---

## Guia de solucion de problemas

### El join falla (-1116 NO_JOIN_ACCEPT)

| Causa posible | Solucion |
|---------------|----------|
| Credenciales incorrectas en firmware | Verificar devEui, joinEui, appKey byte a byte |
| Credenciales incorrectas en gateway | Verificar que coincidan con el firmware |
| DevNonce repetido | Borrar dispositivo en el NS y crearlo de nuevo |
| Antena no conectada | Conectar antena al U.FL |
| Distancia al gateway | Acercar la CubeCell al UG56 |
| Sub-banda incorrecta | Verificar canales activos en el UG56 (sub-banda 2 recomendada) |

### El join funciona pero no se envian datos

| Causa posible | Solucion |
|---------------|----------|
| CRC/MIC mismatch | Verificar que nwkKey/appKey sean correctos |
| FPort incorrecto | Usar puerto 1 por defecto |
| Payload demasiado largo | RadioLib permite hasta 222 bytes (maximo LoRaWAN: 51 bytes en DR2) |
| ADR desajustado | Deshabilitar ADR temporalmente con `node.setADR(false)` |

### No se ven paquetes nuevos en el gateway

| Causa posible | Solucion |
|---------------|----------|
| Sesion perdida | El firmware reintenta join automaticamente |
| Gateway sin conexion | Verificar la conexion a Internet del UG56 |
| CubeCell en bootloop | Abrir monitor serie para ver si hay errores |
| Intervalo de envio muy largo | Reducir `INTERVALO_MS` a 10000 (10s) para pruebas |

### El monitor serie no muestra nada

| Causa posible | Solucion |
|---------------|----------|
| Puerto incorrecto | Verificar con `arduino-cli board list` |
| Baudrate incorrecto | Debe ser 115200 baud |
| Driver CP2102 no instalado | Instalar driver USB-Serial CP2102 |

---

## Tabla de compatibilidad con otras placas Arduino

| Placa | MCU | Radio LoRa | Compatible | Notas |
|-------|-----|------------|------------|-------|
| **Heltec CubeCell HTCC-AB01** | ASR6502 | SX1262 integrado | **Si** | Placa objetivo, probada y verificada |
| Heltec CubeCell HTCC-AB02 | ASR6502 | SX1262 integrado | **Si** | Similar a AB01, mismo core |
| Heltec CubeCell HTCC-AC01 | ASR6501 | SX1262 integrado | **No** | ASR6501 tiene timer bug distinto |
| Heltec Wireless Stick V3 | ESP32-S3 | SX1262 | **Si** | Requiere pines diferentes |
| Heltec Wireless Stick V2 | ESP32 | SX1276 | **Parcial** | Usa SX1276 (no SX1262) |
| LilyGo T3_S3 | ESP32-S3 | SX1262 | **Si** | Pines: NSS=8, DIO1=14, RST=9, BUSY=13 |
| M5Stack LoRaWAN Module | ESP32 | SX1262 | **Parcial** | Requiere adaptacion de pines |
| Arduino MKR WAN 1310 | SAMD21 | CMWX1ZZABZ | **No** | Stack LoRaWAN diferente |
| SparkFun LoRaWAN Thing Plus | SAMD21 | RM1261 | **No** | Stack LoRaWAN diferente |

### Adaptacion para otras placas con SX1262

Para usar este firmware en otra placa con SX1262, cambiar solo los pines SPI:

```cpp
Module mod(NSS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN);
```

Y la banda segun la region:

```cpp
const LoRaWANBand_t Region = AU915; // o EU868, US915, etc.
```

---

## Versionado

| Version | Fecha | Cambios |
|---------|-------|---------|
| v1.0 | 2026-07-27 | Firmware inicial con stack Heltec (no funcional por timer bug) |
| **v1.1** | **2026-07-28** | **Migracion a RadioLib 7.7.1, OTAA funcional, payload HEX configurable, workaround millis sleep** |
| v1.2 | Proximo | Plan: deep sleep, sensores, downlink handling |

---

## Referencias

- [RadioLib Documentation](https://jgromes.github.io/RadioLib/)
- [Heltec CubeCell HTCC-AB01 Official](https://heltec.org/project/htcc-ab01/)
- [Milesight UG56 User Guide](https://www.milesight.com/docs/)
- [LoRaWAN Specification 1.0.4](https://lora-alliance.org/resource_hub/lorawan-104-specification-package/)
- [RadioLib Issue #1807 — CubeCell timer scaling](https://github.com/jgromes/RadioLib/issues/1807)
