/*
 * decoder.js — Cubecell-htcc-ab01-2 (HTCC-AB01)
 * ============================================================================
 * Payload uplink (fijo, 5 bytes, puerto 1):
 *   [0]    0x01 = OK (versión)
 *   [1:4]  número aleatorio uint32 big-endian
 *
 * Comandos downlink (puerto 1):
 *   { "set_sf": 8 }        -> 0x01 0x08        (SF 7..9)
 *   { "interval_min": 5 }  -> 0x02 0x00 0x05   (1..1440 min)
 *   { "query_rssi": true } -> 0x03 0x00
 *
 * FORMATO PRINCIPAL (Milesight UG56 / ChirpStack v4):
 *   decodeUplink(input)  -> { data, warnings, errors }
 *   encodeDownlink(input)-> { bytes, fPort, warnings }
 *
 * COMPATIBILIDAD LEGADA (ChirpStack v3):
 *   Decode(fPort, bytes) -> objeto JSON directo
 *   Encode(fPort, obj)   -> array de bytes directo
 *
 * ES5 puro: sin let/const, sin arrow functions.
 * ============================================================================
 */

/* ------------------------- UPLINK ------------------------- */
function decodeUplink(input) {
  var bytes = (input && input.bytes) ? input.bytes : [];
  var fPort = (input && input.fPort !== undefined) ? input.fPort : 1;
  var data = {};
  var warnings = [];
  var errors = [];

  if (fPort !== 1) {
    warnings.push("Puerto inesperado: " + fPort + " (se esperaba 1)");
  }

  if (bytes.length < 5) {
    errors.push("Payload demasiado corto: " + bytes.length + " B (se esperan 5)");
    return { data: data, warnings: warnings, errors: errors };
  }

  data.version = bytes[0];
  data.random = ((bytes[1] << 24) | (bytes[2] << 16) | (bytes[3] << 8) | bytes[4]) >>> 0;

  if (bytes[0] !== 0x01) {
    warnings.push("Version de payload desconocida: 0x" + bytes[0].toString(16));
  }

  return { data: data, warnings: warnings, errors: errors };
}

/* ------------------------ DOWNLINK ------------------------ */
function encodeDownlink(input) {
  var obj = (input && input.data) ? input.data : {};
  var fPort = (input && input.fPort !== undefined) ? input.fPort : 1;
  var warnings = [];
  var bytes = [];

  if (obj.set_sf !== undefined) {
    var sf = Number(obj.set_sf);
    if (sf >= 7 && sf <= 9) {
      bytes = [0x01, sf & 0xFF];
    } else {
      warnings.push("set_sf fuera de rango (7..9): " + sf);
    }
  } else if (obj.interval_min !== undefined) {
    var minutes = Number(obj.interval_min);
    if (minutes >= 1 && minutes <= 1440) {
      bytes = [0x02, (minutes >> 8) & 0xFF, minutes & 0xFF];
    } else {
      warnings.push("interval_min fuera de rango (1..1440): " + minutes);
    }
  } else if (obj.query_rssi === true) {
    bytes = [0x03, 0x00];
  } else {
    warnings.push("Comando desconocido: use set_sf, interval_min o query_rssi");
  }

  return { bytes: bytes, fPort: fPort, warnings: warnings };
}

/* --------------------- COMPATIBILIDAD v3 --------------------- */
/* ChirpStack v3 espera un objeto JSON directo (sin envoltorio).             */
function Decode(fPort, bytes, variables) {
  var r = decodeUplink({ bytes: bytes, fPort: fPort });
  return r.data;
}

/* ChirpStack v3 espera el array de bytes directo.                           */
function Encode(fPort, obj, variables) {
  var r = encodeDownlink({ data: obj, fPort: fPort });
  return r.bytes;
}
