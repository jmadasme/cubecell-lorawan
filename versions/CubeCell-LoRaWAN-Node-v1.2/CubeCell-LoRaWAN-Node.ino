/*
 * CubeCell-LoRaWAN-Node.ino  —  v1.2 Serial-to-LoRaWAN Bridge
 * ===================================================================
 * Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262)
 * Region: AU915 sub-banda 2, OTAA Clase A
 * RadioLib 7.7.1
 *
 * WORKAROUND: millis() sleep + scanGuard=200 para timer bug ASR6502.
 *
 * === QUE HACE ===
 * Escribe HEX en el Monitor Serie (115200 baud) y la CubeCell lo
 * envia INMEDIATAMENTE al gateway Milesight UG56 via LoRaWAN.
 *
 * Ejemplo en Monitor Serie:
 *   > DE AD BE EF   ← tu escribes y presionas Enter
 *   >> Enviado: DE AD BE EF (RSSI -39)
 *   >
 *   > 01 02 03
 *   >> Enviado: 01 02 03
 *   >
 *
 * El gateway recibe el payload en hex y lo publica por MQTT.
 * Tu servidor decodifica.
 * ===================================================================
 */

#include <RadioLib.h>

/* ===================================================================
 *  PINES SX1262 (conexion interna con ASR6502)
 *  P4_3=35(NSS), P4_6=38(DIO1), P5_7=47(RST), P4_7=39(BUSY)
 * =================================================================== */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

/* ===================================================================
 *  CREDENCIALES LORAWAN (OTAA) — Dispositivo JAM101
 *  Creado en UG56 el 28/07/2026
 * =================================================================== */
uint64_t joinEui = 0x24E124C0002ADD01;
uint64_t devEui  = 0x24F124725F486022;
uint8_t appKey[] = { 0xA7, 0xF1, 0x9C, 0x4E, 0x82, 0xD5, 0xC0, 0x0F,
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 };
uint8_t nwkKey[] = { 0xA7, 0xF1, 0x9C, 0x4E, 0x82, 0xD5, 0xC0, 0x0F,
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 }; // = appKey (modo 1.0.x)

const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* Payload buffer: maximo 51 bytes (limite DR2/DR3 AU915) */
#define MAX_PAYLOAD 51
uint8_t txBuffer[MAX_PAYLOAD];
size_t  txLen = 0;

/* Buffer para linea serial */
#define SERIAL_BUF 256
char serialLine[SERIAL_BUF];
size_t serialPos = 0;
uint32_t lastCharMs = 0;     /* timestamp del ultimo caracter recibido */
#define STALE_MS 500          /* si pasan >500ms sin datos, se descarta el buffer */

/* ===================================================================
 *  WORKAROUND: Sleep basado en millis()
 *  millis() usa RTC WCO 32.768 kHz → preciso en CubeCell.
 *  delay() usa HF clock → impreciso (~1.2% error).
 * =================================================================== */
static void radioSleepDelay(RadioLibTime_t ms) {
    if (ms == 0) return;
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        /* NOP */
    }
}

/* ===================================================================
 *  DIAGNOSTICO
 * =================================================================== */
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

static const char* stateDecode(int16_t s) {
    switch(s) {
        case RADIOLIB_ERR_NONE:            return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:  return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_PACKET_TOO_LONG: return "PACKET_TOO_LONG";
        case RADIOLIB_ERR_RX_TIMEOUT:      return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:    return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED: return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:  return "NO_JOIN_ACCEPT";
        case RADIOLIB_LORAWAN_NEW_SESSION: return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
        default:                           return "UNKNOWN";
    }
}

/* ===================================================================
 *  PARSER HEX: convierte texto "DE AD BE EF" o "DEADBEEF" a bytes
 *  Retorna numero de bytes parseados, o 0 si error.
 * =================================================================== */
static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut) {
    size_t outLen = 0;
    char   clean[256];
    size_t cleanLen = 0;

    /* Copiar solo caracteres hex validos (0-9, A-F, a-f), ignorar resto */
    for (size_t i = 0; input[i] != '\0' && cleanLen < sizeof(clean) - 1; i++) {
        char c = input[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            clean[cleanLen++] = c;
        }
    }
    clean[cleanLen] = '\0';

    /* Debe tener cantidad par de caracteres hex */
    if (cleanLen == 0 || cleanLen % 2 != 0) {
        return 0;
    }

    /* Convertir pares a bytes */
    for (size_t i = 0; i < cleanLen && outLen < maxOut; i += 2) {
        char byteStr[3] = { clean[i], clean[i+1], '\0' };
        output[outLen++] = (uint8_t)strtoul(byteStr, NULL, 16);
    }

    return outLen;
}

/* ===================================================================
 *  REINTENTAR JOIN (si se pierde la sesion)
 * =================================================================== */
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    debug("Re-joining...\n");
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("+++ RE-JOINED +++\n");
        return true;
    }
    debug("Re-Join FAILED: %d (%s)\n", state, stateDecode(state));
    return false;
}

/* ===================================================================
 *  ENVIAR PAYLOAD POR LORAWAN
 * =================================================================== */
static bool sendUplink(const uint8_t* data, size_t len) {
    if (len == 0) return false;

    /* Mostrar lo que se envia */
    debug(">> Enviando: ");
    for (size_t i = 0; i < len; i++) {
        debug("%02X ", data[i]);
    }

    int state = node.sendReceive(data, len);
    if (state == RADIOLIB_ERR_NONE) {
        debug("→ OK (sin downlink)\n");
        return true;
    } else if (state > 0) {
        debug("→ OK + downlink (%d bytes)\n", state);
        return true;
    } else {
        debug("→ ERROR %d (%s)\n", state, stateDecode(state));
        return false;
    }
}

/* ===================================================================
 *  PROCESAR UNA LINEA COMPLETA DEL SERIAL
 * =================================================================== */
static void processSerialLine(const char* line) {
    /* Ignorar lineas vacias */
    if (line[0] == '\0') return;

    /* Intentar parsear como hex */
    txLen = parseHexString(line, txBuffer, MAX_PAYLOAD);
    if (txLen == 0) {
        debug("?? Error: escribe hex valido. Ej: DE AD BE EF\n> ");
        Serial.flush();
        return;
    }

    /* Enviar inmediatamente (si hay sesion) */
    if (!ensureJoined()) {
        debug("!! Sin sesion LoRaWAN, reintentando...\n");
        radioSleepDelay(5000);
        if (!ensureJoined()) {
            debug("!! No se pudo recuperar sesion\n> ");
            Serial.flush();
            return;
        }
    }

    sendUplink(txBuffer, txLen);
    debug("> ");
    Serial.flush();
}

/* ===================================================================
 *  SETUP
 * =================================================================== */
void setup() {
    Serial.begin(115200);
    delay(3000);
    debug("\n=== CubeCell Serial-to-LoRaWAN Bridge v1.2 ===\n");

    /* Inicializar radio */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("Radio init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Radio OK\n");

    /* Inicializar nodo OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("Node init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Node OK\n");

    /* Workaround */
    node.setSleepFunction(radioSleepDelay);
    node.scanGuard = 200;
    debug("scanGuard=%dms, millis sleep\n", node.scanGuard);

    /* Join inicial */
    debug("Joining...\n");
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("+++ JOINED +++\n");
    } else {
        debug("Join FAILED: %d (%s)\n", state, stateDecode(state));
    }

    /* Prompt */
    debug("=== Listo. Escribe HEX y presiona Enter ===\n> ");
    Serial.flush();
}

/* ===================================================================
 *  LOOP
 * =================================================================== */
void loop() {
    /* Leer caracteres del Serial */
    while (Serial.available() > 0) {
        char c = Serial.read();
        lastCharMs = millis();

        if (c == '\n' || c == '\r') {
            /* Fin de linea */
            if (serialPos > 0) {
                serialLine[serialPos] = '\0';
                processSerialLine(serialLine);
                serialPos = 0;
            }
        } else if (serialPos < SERIAL_BUF - 1) {
            serialLine[serialPos++] = c;
        }
    }

    /* Stale buffer flush: si hay datos sin completar y paso >500ms
     * desde el ultimo caracter, es basura de una conexion anterior.
     * Esto evita que se acumulen restos del buffer serial. */
    if (serialPos > 0 && (millis() - lastCharMs) > STALE_MS) {
        serialPos = 0;
    }

    /* Pequena pausa para no saturar el CPU */
    delay(10);
}
