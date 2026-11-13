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
 * Formato de retorno ChirpStack v4:
 *   Decode -> { data: {...}, errors: [...] }
 *   Encode -> { bytes: [...] }
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
