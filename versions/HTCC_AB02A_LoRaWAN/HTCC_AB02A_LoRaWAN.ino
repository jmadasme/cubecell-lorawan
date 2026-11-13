/*
 * HTCC_AB02A_LoRaWAN.ino — Nodo Sensor LoRaWAN Low-Power (v3.0, RadioLib)
 * ===================================================================
 * Heltec CubeCell HTCC-AB02A (ASR6502 + SX1262)
 * Region: AU915 sub-banda 2 | OTAA | Clase A
 * Librería: RadioLib 7.7.1 (NO stack nativo LoRaWan_APP.h)
 *
 * OBJETIVO v3.0:
 *   - Join OTAA contra gateway Milesight UG56 (NS embebido, AU915 SB2).
 *   - Ciclo de 10 min: dormir (deep sleep real) -> despertar -> capturar
 *     entradas digitales + analógicas -> uplink no confirmado -> dormir.
 *   - 1 salida digital controlable por downlink (GPIO1/P6_0).
 *   - 6 entradas digitales con pull-up (GPIO5-10).
 *   - 2 entradas analógicas (ADC2/P2_1, ADC3/P2_2), voltaje en mV.
 *
 * PAYLOAD v3.0 (puerto 1, big-endian, 7 bytes):
 *   [0]    digitales: bit0=GPIO5 bit1=GPIO6 bit2=GPIO7
 *                      bit3=GPIO8 bit4=GPIO9 bit5=GPIO10
 *   [1:2]  adc2 (P2_1)  mV uint16 BE
 *   [3:4]  adc3 (P2_2)  mV uint16 BE
 *   [5:6]  batería      mV uint16 BE (getBatteryVoltage)
 *
 * DOWNLINK (puerto 1), primer byte comanda GPIO1/P6_0:
 *   0x01 -> ON (HIGH)
 *   0x00 -> OFF (LOW)
 *   0x02 -> TOGGLE
 *
 * BAJO CONSUMO:
 *   - Deep sleep del MCU: lowPowerHandler() (CySysPmDeepSleep) + RTC.
 *   - Radio SX1262 en sleep: radio.sleep() (~0.6 µA).
 *   - Despertar por TimerEvent_t (RTC) a los 600000 ms.
 *
 * ¿POR QUÉ RADIOLIB? (ver v2.x) — el stack nativo falla el join por
 *   el bug de timers ~×100. RadioLib 7.7.1 ya verificada en campo.
 *
 * ===================================================================
 */

#include <RadioLib.h>
#include "Arduino.h"

/* ------------------------------------------------------------------ */
/* Pines SX1262 (internos del SiP ASR6502, no reasignables)            */
/*   NSS=P4_3=35, DIO1=P4_6=38, RST=P5_7=47, BUSY=P4_7=39             */
/* ------------------------------------------------------------------ */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

/* ------------------------------------------------------------------ */
/* Credenciales OTAA (big-endian)                                      */
/* ------------------------------------------------------------------ */
uint64_t joinEui = 0x24E124C0002ADD01;              /* JoinEUI/AppEUI */
uint64_t devEui  = 0x70B3D56F27CEE98B;              /* DevEUI         */
uint8_t appKey[] = { 0x89, 0xF5, 0xA1, 0xA2, 0x4D, 0x63, 0x47, 0x57,
                     0x91, 0xF8, 0xBF, 0x33, 0xEA, 0x03, 0x14, 0x86 };
uint8_t nwkKey[] = { 0x89, 0xF5, 0xA1, 0xA2, 0x4D, 0x63, 0x47, 0x57,
                     0x91, 0xF8, 0xBF, 0x33, 0xEA, 0x03, 0x14, 0x86 };

/* Región AU915, sub-banda 2 */
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* ------------------------------------------------------------------ */
/* Configuración de GPIOs                                              */
/* ------------------------------------------------------------------ */
const uint8_t OUTPUT_PIN = GPIO1;     /* P6_0 — salida controlable    */

const uint8_t DIG_PINS[] = { GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10 };
const uint8_t DIG_COUNT = sizeof(DIG_PINS) / sizeof(DIG_PINS[0]);

const uint8_t ADC2_PIN = ADC2;        /* P2_1 */
const uint8_t ADC3_PIN = ADC3;        /* P2_2 */

/* ------------------------------------------------------------------ */
/* Ciclo de operación                                                  */
/* ------------------------------------------------------------------ */
#define SLEEP_INTERVAL_MS 600000UL     /* 10 minutos = 600000 ms      */

uint8_t  downlinkData[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE]; /* 242 B     */
size_t   downlinkLen = 0;

static bool joinedReported = false;

/* ------------------------------------------------------------------ */
/* Low-power nativo: TimerEvent_t (RTC) + lowPowerHandler()            */
/* ------------------------------------------------------------------ */
TimerEvent_t sleepTimer;
static volatile bool sleepExpired = false;

static void onSleepTimerExpired(void)
{
    sleepExpired = true;
}

/* Duerme el MCU en deep sleep hasta que expire el timer RTC.
 * NO retorna hasta que pasen los milisegundos indicados.              */
static void deepSleepMillis(uint32_t ms)
{
    sleepExpired = false;
    TimerInit(&sleepTimer, onSleepTimerExpired);
    TimerSetValue(&sleepTimer, ms);
    TimerStart(&sleepTimer);

    /* Dormir el radio antes del deep sleep del MCU */
    radio.sleep();

    while (!sleepExpired) {
        lowPowerHandler();   /* CySysPmDeepSleep() hasta IRQ del RTC */
    }

    TimerStop(&sleepTimer);
    /* Al despertar, re-activar el radio (SX1262 en standby) */
    radio.standby();
}

/* ------------------------------------------------------------------ */
/* Diagnóstico                                                         */
/* ------------------------------------------------------------------ */
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

static const char* stateDecode(int16_t s) {
    switch (s) {
        case RADIOLIB_ERR_NONE:              return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:    return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_PACKET_TOO_LONG:   return "PACKET_TOO_LONG";
        case RADIOLIB_ERR_RX_TIMEOUT:        return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:      return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED:return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:    return "NO_JOIN_ACCEPT";
        case RADIOLIB_LORAWAN_NEW_SESSION:   return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED: return "SESSION_RESTORED";
        default:                             return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Lecturas                                                            */
/* ------------------------------------------------------------------ */

/* Batería interna en mV */
static uint16_t readBatteryMv(void) {
    uint16_t mv = getBatteryVoltage();
    debug("[BATT] %u mV\r\n", mv);
    return mv;
}

/* Estado digital de las 6 entradas, empaquetado en bits */
static uint8_t readDigitalBits(void) {
    uint8_t bits = 0;
    for (uint8_t i = 0; i < DIG_COUNT; i++) {
        int v = digitalRead(DIG_PINS[i]);
        if (v == HIGH) bits |= (1 << i);
    }
    debug("[GPIO] bits=0x%02X\r\n", bits);
    return bits;
}

/* Voltaje analógico en mV (rango 0-2400 mV del ASR650x) */
static uint16_t readAnalogMv(uint8_t pin) {
    float mv = analogReadmV(pin);
    if (mv < 0) mv = 0;
    uint16_t v = (uint16_t)mv;
    debug("[ADC] pin=%d -> %u mV\r\n", pin, v);
    return v;
}

/* ------------------------------------------------------------------ */
/* Enviar uplink de telemetría (7 bytes, puerto 1) + capturar downlink */
/* ------------------------------------------------------------------ */
static int sendSensorReport(void) {
    uint8_t  dg  = readDigitalBits();
    uint16_t ad2 = readAnalogMv(ADC2_PIN);
    uint16_t ad3 = readAnalogMv(ADC3_PIN);
    uint16_t bat = readBatteryMv();

    uint8_t payload[7];
    payload[0] = dg;
    payload[1] = (uint8_t)(ad2 >> 8);
    payload[2] = (uint8_t)(ad2 & 0xFF);
    payload[3] = (uint8_t)(ad3 >> 8);
    payload[4] = (uint8_t)(ad3 & 0xFF);
    payload[5] = (uint8_t)(bat >> 8);
    payload[6] = (uint8_t)(bat & 0xFF);

    debug("[UPLINK] port=1 payload=");
    for (uint8_t i = 0; i < 7; i++) {
        debug("%02X", payload[i]);
    }
    debug(" (7 B)\r\n");

    downlinkLen = sizeof(downlinkData);
    int state = node.sendReceive(payload, sizeof(payload), 1,
                                 downlinkData, &downlinkLen,
                                 false, NULL, NULL);

    if (state == RADIOLIB_ERR_NONE) {
        debug("[UPLINK] OK (sin downlink)\r\n");
    } else if (state > 0) {
        debug("[RX] %u bytes: ", (unsigned)downlinkLen);
        for (size_t i = 0; i < downlinkLen; i++) {
            debug("%02X", downlinkData[i]);
        }
        debug("\r\n");
        handleDownlink(downlinkData, downlinkLen);
    } else {
        debug("[UPLINK] ERROR %d (%s)\r\n", state, stateDecode(state));
    }

    return state;
}

/* ------------------------------------------------------------------ */
/* Procesar downlink: comandar GPIO1 (P6_0)                            */
/* ------------------------------------------------------------------ */
static void handleDownlink(const uint8_t* data, size_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];
    switch (cmd) {
        case 0x01:
            digitalWrite(OUTPUT_PIN, HIGH);
            debug("[RX] GPIO1 -> ON\r\n");
            break;
        case 0x00:
            digitalWrite(OUTPUT_PIN, LOW);
            debug("[RX] GPIO1 -> OFF\r\n");
            break;
        case 0x02:
            digitalWrite(OUTPUT_PIN, !digitalRead(OUTPUT_PIN));
            debug("[RX] GPIO1 -> TOGGLE\r\n");
            break;
        default:
            debug("[RX] comando desconocido: 0x%02X\r\n", cmd);
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Re-join automático                                                  */
/* ------------------------------------------------------------------ */
static bool ensureJoined(void) {
    if (node.isActivated()) return true;
    debug("[REJOIN] Intentando re-join...\r\n");
    int state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[REJOIN] +++ EXITOSO +++\r\n");
        joinedReported = true;
        return true;
    }
    debug("[REJOIN] Fallo: %d (%s)\r\n", state, stateDecode(state));
    return false;
}

/* ------------------------------------------------------------------ */
/* SETUP                                                               */
/* ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    delay(3000);

    debug("\r\n[INIT] HTCC-AB02A nodo sensor v3.0 (RadioLib) — AU915 SB2\r\n");
    debug("[INIT] DevEUI=%016llX JoinEUI=%016llX\r\n",
          (unsigned long long)devEui, (unsigned long long)joinEui);

    /* Configurar GPIOs */
    pinMode(OUTPUT_PIN, OUTPUT);
    digitalWrite(OUTPUT_PIN, LOW);   /* estado inicial: apagado */
    for (uint8_t i = 0; i < DIG_COUNT; i++) {
        pinMode(DIG_PINS[i], INPUT_PULLUP);
    }
    pinMode(ADC2_PIN, ANALOG);
    pinMode(ADC3_PIN, ANALOG);
    debug("[INIT] GPIOs: out=P6_0, in=P3_4/P3_6/P3_7/P7_0/P7_1/P7_2, adc=P2_1/P2_2\r\n");

    /* Inicializar radio */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Radio FAILED: %d (%s)\r\n", state, stateDecode(state));
        while (true) delay(1);
    }
    debug("[INIT] Radio OK\r\n");

    /* Inicializar nodo OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Node FAILED: %d (%s)\r\n", state, stateDecode(state));
        while (true) delay(1);
    }
    debug("[INIT] Node OK\r\n");

    /* Workaround bug de timers (esperas de radio) */
    node.scanGuard = 200;
    debug("[INIT] scanGuard=%d ms\r\n", node.scanGuard);

    /* Join OTAA inicial */
    debug("[JOIN] Iniciando OTAA (AU915 SB2)...\r\n");
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[JOIN] +++ JOINED +++\r\n");
        joinedReported = true;
    } else {
        debug("[JOIN] Fallo: %d (%s)\r\n", state, stateDecode(state));
    }

    debug("[INIT] Ciclo de %lu ms (10 min). Listo.\r\n",
          (unsigned long)SLEEP_INTERVAL_MS);
    Serial.flush();
}

/* ------------------------------------------------------------------ */
/* LOOP                                                                */
/* ------------------------------------------------------------------ */
void loop() {
    /* Re-join si se pierde la sesión */
    if (!node.isActivated()) {
        if (!ensureJoined()) {
            /* Backoff: dormir 10 s y reintentar */
            deepSleepMillis(10000);
            return;
        }
    }

    /* Reportar sensores (estado del instante) */
    sendSensorReport();

    /* Volver a dormir 10 minutos (deep sleep real) */
    debug("[SLEEP] Durmiendo %lu ms...\r\n", (unsigned long)SLEEP_INTERVAL_MS);
    Serial.flush();
    deepSleepMillis(SLEEP_INTERVAL_MS);
}
