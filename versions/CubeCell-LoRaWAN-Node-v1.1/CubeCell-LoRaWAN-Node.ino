/*
 * CubeCellRadioLib.ino
 * Nodo LoRaWAN para HTCC-AB01 usando RadioLib 7.7.1
 * Region: AU915 sub-banda 2, OTAA, Clase A
 *
 * WORKAROUND: Reemplaza delay() por sleep basado en millis()
 * millis() es preciso en CubeCell (usa RTC WCO 32.768 kHz)
 *
 * === COMO USAR ===
 * 1. Credenciales OTAA abajo (devEui, appKey)
 * 2. payload[] = los bytes HEX que quieras enviar
 * 3. Compilar y subir
 * 4. El gateway recibe el HEX y lo publica por MQTT
 * 5. Tu servidor decodifica el MQTT
 */

#include <RadioLib.h>

/* ===================================================================
 *  PINES CUBECELL (HTCC-AB01 / ASR6502)
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
                     0x3E, 0x9A, 0x14, 0xC8, 0xD7, 0xB2, 0x5F, 0x61 }; // = appKey

/* Region y sub-banda */
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* ===================================================================
 *  PAYLOAD A ENVIAR (cambia estos bytes a lo que necesites)
 *
 *  Ejemplos:
 *    { 0xDE, 0xAD, 0xBE, 0xEF }        // 4 bytes
 *    { 0x01, 0x02, 0x03 }               // 3 bytes
 *    { 0xAA }                            // 1 byte
 * =================================================================== */
const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
const size_t payloadLen = sizeof(payload);

const unsigned long INTERVALO_MS = 60000; // cada 60 segundos

/* ===================================================================
 *  WORKAROUND: Sleep basado en millis() (preciso en CubeCell)
 *  millis() usa el RTC de cuarzo (WCO 32.768 kHz) y es exacto.
 *  delay() usa el clock HF y es ~1.2% inexacto.
 * =================================================================== */
static void radioSleepDelay(RadioLibTime_t ms) {
    if (ms == 0) return;
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        /* NOP - spin preciso */
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
 *  SETUP
 * =================================================================== */
void setup() {
    Serial.begin(115200);
    delay(3000);
    debug("=== CubeCell + RadioLib AU915 ===\n");

    /* Inicializar radio SX1262 */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("Radio init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Radio OK\n");

    /* Inicializar nodo LoRaWAN OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("Node init FAILED: %d\n", state);
        while (true) delay(1);
    }
    debug("Node OK\n");

    /* Aplicar workaround #1807: reemplazar delay por millis() sleep */
    node.setSleepFunction(radioSleepDelay);
    /* Aumentar scanGuard para compensar imprecision del clock */
    node.scanGuard = 200;
    debug("Workaround aplicado: millis sleep + scanGuard=%dms\n", node.scanGuard);

    /* Unirse a la red */
    debug("Joining...\n");
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("+++ JOINED +++\n");
    } else {
        debug("Join FAILED: %d (%s)\n", state, stateDecode(state));
    }

    /* Mostrar payload que se enviara */
    debug("Payload (%d bytes): ", payloadLen);
    for (size_t i = 0; i < payloadLen; i++) {
        debug("%02X ", payload[i]);
    }
    debug("\n");
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
 *  LOOP
 * =================================================================== */
void loop() {
    /* Si perdio la sesion, reintentar join */
    if (!ensureJoined()) {
        debug("Sin sesion, reintento en %lu ms...\n", INTERVALO_MS);
        radioSleepDelay(INTERVALO_MS);
        return;
    }

    debug("Enviando uplink...\n");

    int state = node.sendReceive(payload, payloadLen);
    if (state == RADIOLIB_ERR_NONE) {
        debug("Uplink enviado OK\n");
    } else if (state > 0) {
        debug("Downlink recibido! (%d bytes)\n", state);
    } else {
        debug("Error uplink: %d (%s)\n", state, stateDecode(state));
    }

    debug("Esperando %lu ms...\n", INTERVALO_MS);
    radioSleepDelay(INTERVALO_MS);
}
