# Datasheet — CubeCell LoRaWAN Bridge v1.3

## Resumen

Firmware para **Heltec CubeCell HTCC-AB01** que implementa un puente serial-LoRaWAN **bidireccional**. El dispositivo transmite datos del serial al gateway (uplink) y recibe comandos del gateway imprimiéndolos en el serial (downlink). Incluye heartbeat automático cada 30s para mantener la ventana RX abierta.

---

## Especificaciones tecnicas

### Microcontrolador

| Parametro | Valor |
|-----------|-------|
| Modelo | ASR6502 (ARM Cortex M0+) |
| Frecuencia | 48 MHz |
| Flash | 128 KB (63% usado v1.3) |
| SRAM | 16 KB |
| ADC | 12 bits |
| UART | 2 |

### Radio

| Parametro | Valor |
|-----------|-------|
| Modelo | SX1262 |
| Potencia Tx | hasta +22 dBm |
| Sensibilidad | hasta -148 dBm |
| Interface | SPI (NSS=GPIO35, DIO1=GPIO38, RST=GPIO47, BUSY=GPIO39) |

### LoRaWAN

| Parametro | Valor |
|-----------|-------|
| Banda | AU915 sub-banda 2 |
| Activacion | OTAA Clase A |
| Puerto | 1 |
| SF | 7-10 (ADR habilitado) |
| Payload maximo | 51 bytes |
| Heartbeat | Cada 30s, payload `0xBB` (1 byte) |
| Downlink | Se imprime en serial como `[DOWNLINK] XX YY ZZ → "texto"` |

### Serial

| Parametro | Valor |
|-----------|-------|
| Baudrate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Prompt | `> ` |
| Formato entrada | HEX (ej: `01 02 03`) |
| Formato downlink | `[DOWNLINK] <bytes> → "<ascii>"` |

---

## Heartbeat mecanismo

Cada **30 segundos** la CubeCell envía un uplink de 1 byte (`0xBB`) para:

1. Notificar al gateway que sigue vivo
2. **Abrir la ventana RX** (Clase A solo recibe downlink tras un uplink)
3. Permitir que comandos pendientes en el NS lleguen al nodo

Si el usuario envía un comando manual, ese uplink también abre ventana RX y se resetea el contador del heartbeat.

---

## Formato del downlink

El firmware imprime el downlink recibido en dos formatos simultáneamente:

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
```

| Parte | Significado |
|-------|-------------|
| `[DOWNLINK]` | Marcador de comando recibido |
| `68 6F 6C 61` | Bytes en hexadecimal |
| `→ "hola"` | Representacion ASCII (`.` si no es imprimible) |
| `> ` | Prompt, listo para siguiente comando |

---

## Compatibilidad

| Placa | Radio | Compatible | Cambios requeridos |
|-------|-------|------------|-------------------|
| HTCC-AB01 | SX1262 | **Si** | Ninguno |
| HTCC-AB02 | SX1262 | **Si** | Ninguno |
| Wireless Stick V3 | SX1262 | **Si** | Pines SPI diferentes |
| LilyGo T3_S3 | SX1262 | **Si** | Pines y baudrate |

---

## Troubleshooting

| Problema | Causa | Solucion |
|----------|-------|----------|
| No llega downlink | NS no envio comando | Enviar downlink desde web del UG56 |
| Downlink tarda | Heartbeat cada 30s | Esperar max 30s, enviar comando manual |
| `Join FAILED: -1116` | DevNonce repetido | Borrar y recrear dispositivo en UG56 |
| `[DOWNLINK] 00` | MAC command del NS | Normal. Ignorar si no es comando de usuario |
| Puerto serial ocupado | cutecom abierto | Cerrar cutecom o usar otro puerto |
