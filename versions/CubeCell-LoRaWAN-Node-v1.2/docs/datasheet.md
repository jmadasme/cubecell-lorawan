# Datasheet — CubeCell Serial-to-LoRaWAN Bridge v1.2

## Resumen

Firmware para **Heltec CubeCell HTCC-AB01** que implementa un puente Serial-to-LoRaWAN usando **RadioLib 7.7.1**. El dispositivo se conecta por USB al PC y escucha el puerto serial. Cuando recibe una linea de texto hexadecimal, la convierte a bytes y la envia **inmediatamente** al gateway **Milesight UG56** via LoRaWAN (OTAA, AU915 sub-banda 2, Clase A).

---

## Especificaciones tecnicas

### Microcontrolador

| Parametro | Valor |
|-----------|-------|
| Modelo | ASR6502 (ARM Cortex M0+) |
| Frecuencia | 48 MHz |
| Flash | 128 KB (62% usado en v1.2) |
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
| Puerto (FPort) | 1 |
| ADR | Habilitado |
| SF | 10 (DR2 en AU915) |
| BW | 125 kHz |
| CR | 4/5 |
| Payload maximo | 51 bytes (limite DR2 para SF10) |
| Intervalo | No aplica. El envio es manual e inmediato. |

### Modo de operacion

| Parametro | Valor |
|-----------|-------|
| Entrada | Puerto serial USB (CP2102) a 115200 baud, 8N1 |
| Formato de entrada | Texto hexadecimal (ej: `01 01 01` o `DEADBEEF`) |
| Separador | Espacios (opcional). Se ignoran espacios y se toman pares de hex. |
| Confirmacion | `>> Enviando: XX YY ZZ → OK` en el mismo puerto serial |
| Caracteres validos | 0-9, A-F, a-f. Cualquier otro produce error. |
| Comportamiento | Espera en el prompt `> ` hasta recibir una linea completa (terminada en newline). |
| Buffer | 256 bytes de entrada. Lineas mas largas se truncan. |
| Stale flush | Datos parciales sin newline por >500ms se descartan automaticamente. |

### Alimentacion

| Parametro | Valor |
|-----------|-------|
| Voltaje USB | 5 VDC |
| Voltaje operacion | 2.2 - 3.6 V (regulado a 3.3V) |
| Consumo Tx | ~80 mA (@ +14 dBm, SF10) |
| Consumo idle | ~25 mA (spin-loop esperando serial) |
| Consumo sleep | ~3.5 µA (deep sleep, no implementado en v1.2) |

---

## Librerias requeridas

### RadioLib 7.7.1 (obligatorio)

```bash
arduino-cli lib install "RadioLib@7.7.1"
```

### Core CubeCell (obligatorio)

```bash
arduino-cli core update-index
arduino-cli core install CubeCell:CubeCell
```

URL para Arduino IDE: `https://resource.heltec.cn/download/package_CubeCell_index.json`

---

## Protocolo de comunicacion serial

### Configuracion del puerto

| Parametro | Valor |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| DTR | No debe resettear la placa (desmarcar en cutecom) |

### Formato de comando

```
<hex_bytes> \n
```

Donde `<hex_bytes>` son pares de caracteres hexadecimales opcionalmente separados por espacios.

### Respuestas

| Escenario | Respuesta serial |
|---|---|
| Comando valido, envio exitoso | `>> Enviando: <bytes> → OK\n> ` |
| Comando valido, envio + downlink | `>> Enviando: <bytes> → OK + downlink (N bytes)\n> ` |
| Comando valido, error de envio | `>> Enviando: <bytes> → ERROR <codigo> (<texto>)\n> ` |
| Hex invalido | `?? Error: escribe hex valido. Ej: DE AD BE EF\n> ` |
| Sesion perdida, rejoin exitoso | `Re-joining...\n+++ RE-JOINED +++\n> ` |
| Sesion perdida, rejoin falla | `Re-joining...\nRe-Join FAILED: <codigo>\n!! Sin sesion LoRaWAN, reintentando...` |

### Codigos de error

| Codigo | Significado |
|--------|-------------|
| `-1101` | NOT_JOINED: sesion LoRaWAN perdida |
| `-1116` | NO_JOIN_ACCEPT: el NS rechazo el join |
| `-1004` | CHIP_NOT_FOUND: fallo de comunicacion con el SX1262 |
| `-1005` | PACKET_TOO_LONG: payload supera el maximo permitido |
| `-1007` | MIC_MISMATCH: error de integridad en downlink |

---

## Guia de solucion de problemas

### El join falla repetidamente (-1116)

| Causa | Solucion |
|-------|----------|
| Credenciales incorrectas | Verificar devEui, joinEui, appKey byte a byte en firmware y UG56 |
| DevNonce repetido | Borrar dispositivo en UG56 y crearlo de nuevo |
| Antena no conectada | Conectar antena al U.FL |
| Distancia | Acercar al gateway (RSSI debe ser > -120) |
| Sub-banda incorrecta | Verificar que el UG56 tenga activos los canales 8-15 |

### Escribi un comando y no paso nada

| Causa | Solucion |
|-------|----------|
| No presionaste Enter | El firmware espera un newline (`\n` o `\r`) para procesar |
| Buffer lleno | El firmware tiene buffer de 256 bytes. Lineas mas largas se truncan |
| Terminal envio CR+LF | Ambos funcionan, pero si solo enviaste CR sin LF, puede fallar. Probar con "Newline" en Arduino IDE |

### Los datos llegan al gateway pero con bytes extra

| Causa | Solucion |
|-------|----------|
| Buffer serial sucio | El firmware descarta automaticamente datos con >500ms de inactividad. Cerrar y reabrir la terminal ayuda. |

### La placa no aparece en el puerto serial

| Causa | Solucion |
|-------|----------|
| Driver CP2102 no instalado | `sudo apt install linux-modules-extra-$(uname -r)` |
| Puerto ocupado | `sudo lsof /dev/ttyUSB0` para ver que proceso lo usa |
| Permisos | El usuario debe estar en el grupo `dialout`: `sudo usermod -aG dialout $USER` |

---

## Tabla de compatibilidad con otras placas

| Placa | MCU | Radio | Compatible | Adaptacion necesaria |
|-------|-----|-------|------------|---------------------|
| **HTCC-AB01** | ASR6502 | SX1262 | **Si** | Ninguna. Placa objetivo. |
| HTCC-AB02 | ASR6502 | SX1262 | **Si** | Misma configuracion de pines. |
| HTCC-AC01 | ASR6501 | SX1262 | **No** | Timer bug diferente. |
| Wireless Stick V3 | ESP32-S3 | SX1262 | **Si** | Cambiar pines SPI y baudrate. |
| LilyGo T3_S3 | ESP32-S3 | SX1262 | **Si** | Pines: NSS=8, DIO1=14, RST=9, BUSY=13. |
| M5Stack LoRaWAN | ESP32 | SX1262 | **Parcial** | Requiere adaptacion de pines y region. |

### Adaptacion para otras placas

```cpp
// Cambiar solo estos pines SPI segun la placa
Module mod(NSS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN);

// Y la banda regional
const LoRaWANBand_t Region = AU915;  // o EU868, US915, etc.
```

---

## Versionado

| Version | Fecha | Cambios |
|---------|-------|---------|
| v1.0 | 2026-07-27 | Firmware inicial con stack Heltec. No funcional (timer bug). |
| v1.1 | 2026-07-28 | RadioLib 7.7.1, OTAA funcional, payload fijo cada 60s. |
| **v1.2** | **2026-07-28** | **Serial-to-LoRaWAN Bridge. Modo interactivo, stale buffer flush, rejoin automatico.** |

---

## Referencias

- [RadioLib 7.7.1 Documentation](https://jgromes.github.io/RadioLib/)
- [Heltec CubeCell HTCC-AB01](https://heltec.org/project/htcc-ab01/)
- [Milesight UG56](https://www.milesight.com/iot/product/lorawan-gateway/ug56)
- [RadioLib Issue #1807](https://github.com/jgromes/RadioLib/issues/1807)
- [LoRaWAN Specification 1.0.4](https://lora-alliance.org/resource_hub/lorawan-104-specification-package/)
