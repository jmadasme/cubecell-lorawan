# Bitácora de Pruebas — HTCC_AB02A_LoRaWAN (v1.1)

> Registro cronológico de las pruebas realizadas para validar el payload decoder
> del nodo sensor LoRaWAN contra el gateway **Milesight** (NS embebido).
> Proyecto local: `proyectos/HTCC_AB02A_LoRaWAN`.

---

## Resumen ejecutivo

| Aspecto | Estado |
|---------|--------|
| Firmware v1.1 (GPIO + ADC) cargado en tarjeta | ✅ Confirmado por payload |
| Payload uplink llega al gateway | ✅ Confirmado (hex válido de 7 bytes) |
| Decoder corregido a formato Milesight (ChirpStack v3) | ✅ Validado localmente con Node.js |
| Decoder mostrando JSON en el gateway | ✅ RESUELTO (confirmado en campo) |
| Publicación en GitHub (privado) | ✅ Repositorio actualizado |

---

## Línea de tiempo de la sesión

### 1. Diagnóstico inicial: decoder no funcionaba

- **Síntoma**: en el `Packet Details` del gateway, el campo **JSON aparecía vacío** (`-`).
- **Payload recibido** (válido, 7 bytes):
  ```
  Payload(hex) = 3f000000000d00
  ```
- **Conclusión**: el firmware funciona (el payload llega correcto), el problema estaba
  en el **formato del decoder**.

### 2. Causa raíz del error

El decoder original estaba en formato **ChirpStack v4**:

```javascript
function Decode(fPort, bytes, variables) {
    return { data: {...}, errors: [...] };   // ❌ formato v4
}
function Encode(fPort, obj, variables) {
    return { bytes: [...] };                 // ❌ formato v4
}
```

Pero el **NS embebido de Milesight** (UG56/UG65/UG67) usa el formato **ChirpStack v3**
(fuente: artículo oficial *"How to Use Payload Codec on Milesight Gateway"*):

| Aspecto | v4 (incorrecto) | v3 / Milesight (correcto) |
|---------|-----------------|---------------------------|
| Firma Decode | `Decode(fPort, bytes, variables)` | `Decode(fPort, bytes)` |
| Retorno Decode | `{data, errors}` | objeto JSON **directo** |
| Firma Encode | `Encode(fPort, obj, variables)` | `Encode(fPort, obj)` |
| Retorno Encode | `{bytes: [...]}` | array de bytes **directo** |
| JavaScript | ES6 (arrow, let/const) | **ES5** estricto |

Por eso el campo JSON quedaba vacío: Milesight no reconocía la estructura `{data, errors}`.

### 3. Corrección aplicada

Se corrigieron 3 archivos (commit `1434642`):

- `decoder.js` — reescrito a formato Milesight/ChirpStack v3 + ES5.
- `docs/payload_decoder.txt` — mismo contenido en texto plano.
- `docs/datasheet.md` — sección del decoder actualizada.

### 4. Validación local con Node.js

Pruebas ejecutadas localmente contra el decoder corregido:

**Caso 1 — Payload real del gateway** (`3f000000000d00`):
```json
{
  "digital_1": 1, "GPIO5": 1, "digital_2": 1, "GPIO6": 1,
  "digital_3": 1, "GPIO7": 1, "digital_4": 1, "GPIO8": 1,
  "digital_5": 1, "GPIO9": 1, "digital_6": 1, "GPIO10": 1,
  "digitales": 63,
  "adc2_mV": 0, "adc2_V": 0,
  "adc3_mV": 0, "adc3_V": 0,
  "bateria_mV": 3328, "bateria_V": 3.33
}
```

**Caso 2 — Payload con ADC activos** (`0504B4033E0D00`):
```json
{
  "digitales": 5,
  "GPIO5": 1, "GPIO7": 1,
  "adc2_mV": 1204, "adc2_V": 1.2,
  "adc3_mV": 830,  "adc3_V": 0.83,
  "bateria_mV": 3328, "bateria_V": 3.33
}
```

**Caso 3 — Encode (downlink GPIO1)**:
```
Encode(1, {gpio1: 1})       → [1]   (ON)
Encode(1, {gpio1: "OFF"})   → [0]   (OFF)
Encode(1, {gpio1: "toggle"})→ [2]   (TOGGLE)
```

✅ Todos los casos validados correctamente.

### 5. Confusión entre decoders (v1.1 vs v1.2)

Durante la prueba apareció **"temperatura"** en el resultado. Causa: se estaba usando el
decoder del proyecto **v1.2 (DS18B20)** en lugar del v1.1. Los dos proyectos tienen
decoders distintos con el mismo nombre de archivo `decoder.js`:

| Proyecto | Firmware | Payload | Campos |
|----------|----------|---------|--------|
| `HTCC_AB02A_LoRaWAN` (v1.1) | GPIO + ADC | 7 bytes | `GPIO5..10`, `adc2_mV`, `adc3_mV`, `bateria_mV` |
| `HTCC_AB02A_DS18B20` (v1.2) | Temperatura | 4 bytes | `temperatura_C`, `bateria_mV` |

**Verificación del firmware cargado**: el payload recibido (`3f000000000d00`, 7 bytes,
formato digitales+ADC+batería) confirma que la tarjeta tiene el firmware **v1.1**, no el
del DS18B20 (que enviaría 4 bytes con temperatura).

### 6. Hipótesis de caché del gateway (pendiente de confirmar)

El decoder corregido fue pegado varias veces **manteniendo el mismo nombre**. Se sospecha
que el gateway cachea la instancia del codec por nombre y **no refresca el contenido
editado**, por lo que seguía mostrando JSON vacío.

**Acción tomada**: crear un codec **nuevo con nombre distinto** (`AB02A_GPIO_ADC_v2`),
probarlo en el *Payload Codec Test* y reasignarlo al dispositivo.

**Estado**: ⏳ a la espera del siguiente uplink (~10 min) para confirmar si el JSON ya
se decodifica correctamente.

### 7. Error SyntaxError: Invalid or unexpected token (causa final)

Al pegar el decoder se produjo `SyntaxError: Invalid or unexpected token`. Causa: los
caracteres **Unicode** (acentos, `°`, `→`, etc.) se corrompían al copiar/pegar desde
WhatsApp/chat, rompiendo el JavaScript en el gateway.

**Solución**: reescribir el decoder a **100% ASCII puro** (sin acentos, sin símbolos,
sin comillas inteligentes). Verificado: `grep` no encuentra caracteres no-ASCII y
`node --check` valida la sintaxis.

### 8. RESUELTO — JSON decodificado correctamente ✅

Tras pegar el decoder ASCII puro, el siguiente uplink se decodificó correctamente:

```json
{
  "digitales": 63,
  "GPIO5": 1, "GPIO6": 1, "GPIO7": 1, "GPIO8": 1, "GPIO9": 1, "GPIO10": 1,
  "adc2_mV": 0, "adc2_V": 0,
  "adc3_mV": 1, "adc3_V": 0,
  "bateria_mV": 3328, "bateria_V": 3.33
}
```

Cadena de problemas resuelta:
1. Formato v4 → v3 (retorno directo).
2. Decoder equivocado (DS18B20 en vez de GPIO/ADC).
3. Caracteres Unicode corrompidos por copy-paste → decoder ASCII puro.

---

## Payloads observados en el gateway (esta sesión)

| Fcnt | Payload hex | Digitales | ADC2 | ADC3 | Batería | JSON |
|------|-------------|-----------|------|------|---------|------|
| 4  | `3f000000000d00` | 0x3F (6 altos) | 0 mV | 0 mV | 3328 mV | vacío (decoder v4) |
| 11 | `3f000500000d00` | 0x3F (6 altos) | 5 mV | 0 mV | 3328 mV | vacío (decoder v4) |
| 13 | `3f000000000d00` | 0x3F (6 altos) | 0 mV | 0 mV | 3328 mV | vacío (decoder v4) |

---

## Checklist para dar por resuelto el decoder

- [x] Decoder v1.1 pegado en `Payload Codec → Payload Decoder Function`.
- [x] Codec creado con nombre descriptivo (p. ej. `AB02A_GPIO_ADC`).
- [x] `Payload Codec Test` con `3f000000000d00` muestra GPIO + ADC (sin temperatura).
- [x] Codec **asignado al dispositivo** en `Network Server → Device` (DevEUI `70B3D56F27CEE98B`).
- [x] Campo JSON en `Packet Details` ya no está vacío.

---

## Estado de GitHub

| Repositorio | Visibilidad | Último commit relevante |
|-------------|-------------|-------------------------|
| `wanderc137/HTCC_AB02A_LoRaWAN` | PRIVATE | `1434642` — Corregir payload decoder a formato Milesight NS embebido (ChirpStack v3) |
