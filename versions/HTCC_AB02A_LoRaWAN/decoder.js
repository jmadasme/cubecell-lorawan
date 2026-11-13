/*
 * HTCC_AB02A_LoRaWAN - Payload Decoder (Milesight NS embebido)
 * =============================================================
 * Uso: pegar en Network Server -> Payload Codec -> "Payload Decoder Function".
 * Compatible con gateways Milesight UG56/UG65/UG67 (NS embebido).
 *
 * IMPORTANTE (formato Milesight / ChirpStack v3):
 *   - JavaScript ES5 (sin let/const, sin arrow functions).
 *   - Decode(fPort, bytes) -> retorna objeto JSON DIRECTO (NO {data, errors}).
 *   - Encode(fPort, obj)   -> retorna array de bytes DIRECTO (NO {bytes: [...]}).
 *   - SOLO caracteres ASCII (sin acentos ni simbolos especiales).
 *
 * Formato uplink (fPort 1, 7 bytes, big-endian):
 *   [0]    digitales: bit0=GPIO5 bit1=GPIO6 bit2=GPIO7
 *                     bit3=GPIO8 bit4=GPIO9 bit5=GPIO10
 *   [1:2]  adc2  (P2_1)  mV  uint16 BE
 *   [3:4]  adc3  (P2_2)  mV  uint16 BE
 *   [5:6]  bateria       mV  uint16 BE
 *
 * Downlink (fPort 1), primer byte comanda GPIO1/P6_0:
 *   0x01 = ON | 0x00 = OFF | 0x02 = TOGGLE
 * =============================================================
 */

function Decode(fPort, bytes) {
    var decoded = {};

    if (fPort === 1 && bytes.length >= 7) {

        /* [0] Entradas digitales (bit0..bit5) */
        var bits = bytes[0];
        var gpioNames = ["GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10"];
        for (var i = 0; i < 6; i++) {
            decoded["digital_" + (i + 1)] = (bits >> i) & 0x01;
            decoded[gpioNames[i]] = (bits >> i) & 0x01;
        }
        decoded["digitales"] = bits;

        /* [1:2] ADC2, [3:4] ADC3, [5:6] Bateria - mV uint16 BE */
        var adc2mv = (bytes[1] << 8) | bytes[2];
        var adc3mv = (bytes[3] << 8) | bytes[4];
        var battmv = (bytes[5] << 8) | bytes[6];

        decoded["adc2_mV"] = adc2mv;
        decoded["adc2_V"] = round2(adc2mv / 1000.0);

        decoded["adc3_mV"] = adc3mv;
        decoded["adc3_V"] = round2(adc3mv / 1000.0);

        decoded["bateria_mV"] = battmv;
        decoded["bateria_V"] = round2(battmv / 1000.0);
    }

    return decoded;
}

function Encode(fPort, obj) {
    var bytes = [];

    if (fPort === 1 && obj !== null && typeof obj === "object") {
        var cmd = null;
        if (obj.gpio1 !== undefined && obj.gpio1 !== null) {
            cmd = obj.gpio1;
        } else if (obj.output !== undefined && obj.output !== null) {
            cmd = obj.output;
        }

        if (typeof cmd === "string") {
            cmd = cmd.toLowerCase();
        }

        if (cmd === 1 || cmd === "on" || cmd === "high") {
            bytes.push(0x01);
        } else if (cmd === 0 || cmd === "off" || cmd === "low") {
            bytes.push(0x00);
        } else if (cmd === 2 || cmd === "toggle") {
            bytes.push(0x02);
        }
    }

    return bytes;
}

/* Helpers */
function round2(v) {
    return Math.round(v * 100) / 100;
}
