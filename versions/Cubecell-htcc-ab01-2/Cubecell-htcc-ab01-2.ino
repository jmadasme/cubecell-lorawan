/*
 * Cubecell-htcc-ab01-2.ino — Nodo LoRaWAN de prueba/evolución (v1.0)
 * ==============================================================================
 * Heltec CubeCell HTCC-AB01 (ASR6502 + SX1262) — revisión reciente (sin holder)
 * Región: AU915 sub-banda 2 | OTAA | Clase A | RadioLib 7.7.1
 * Gateway: Milesight UG56 (Network Server embebido)
 *
 * PAYLOAD (fijo, 5 bytes, puerto 1):
 *   [0]    0x01 = OK (versión)
 *   [1:4]  número aleatorio uint32 big-endian
 *
 * DOWNLINK (whitelist estricta, puerto 1):
 *   0x01 <sf>        cambia SF (7..9 -> DR5..DR3) y persiste en EEPROM
 *   0x02 <hi> <lo>   cambia intervalo en segundos (30 a 65535, uint16 BE) y persiste
 *   0x03 [0x00]      consulta RSSI del último downlink (se registra por serial)
 *
 * HERRAMIENTAS / LECCIONES DE CAMPO APLICADAS:
 *   - Deep sleep real: TimerEvent_t (RTC) + lowPowerHandler()
 *   - scanGuard=200 (workaround bug de timers del core CubeCell)
 *   - ADR OFF + setDwellTime(false) tras cada join (AU915, dwell time 400 ms)
 *   - Límite SF 7..9 en downlink (SF >= 10 "brickearía" el nodo por dwell time)
 *   - RSSI real con radio.getRSSI() (LoRaWANEvent_t.power NO es RSSI en 7.7.1)
 *   - Persistencia de sesión LoRaWAN en EEPROM emulada (nonces + session):
 *     sobrevive a power-cycle y evita re-joins rechazados por DevNonce
 *     reiniciado (error -1116 del UG56). Los nonces se guardan tras CADA
 *     intento de join; la sesión completa tras un join exitoso y luego de
 *     forma periódica (cada SESS_SAVE_EVERY_UPLINKS uplinks) para no
 *     desgastar la flash.
 *   - Recuperación: NVIC_SystemReset() ante fallo de init de radio/nodo.
 * ==============================================================================
 */

#include <RadioLib.h>
#include <EEPROM.h>
#include "Arduino.h"
#include "credentials.h"

/* Diagnóstico */
#define debug(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Constantes de configuración (PLAN §9)                               */
/* ------------------------------------------------------------------ */
#define TX_INTERVAL_DEFAULT_SEC   30    /* intervalo por defecto: 30 segundos */
#define SF_MIN                    7
#define SF_MAX                    9
#define INTERVAL_SEC_MIN          30
#define INTERVAL_SEC_MAX          65535

/* Payload */
#define PAYLOAD_VERSION_OK        0x01
#define PAYLOAD_LEN               5

/* Downlink */
#define DL_CMD_SF                 0x01
#define DL_CMD_INTERVAL           0x02
#define DL_CMD_QUERY_RSSI         0x03

/* EEPROM (emulada sobre flash, retenida en power-cycle) */
#define CFG_EEPROM_ADDR           0
#define CFG_MAGIC0                0xC0
#define CFG_MAGIC1                0xC1
#define SESS_EEPROM_ADDR          64
#define SESS_MAGIC0               0x5A   /* 'Z' — bloque de sesión LoRaWAN  */
#define SESS_MAGIC1               0x5B   /* '['                             */
#define EEPROM_ARENA_SIZE         512    /* 2 filas de flash (max core: 768) */

/* Layout bloque de sesión @ SESS_EEPROM_ADDR:
 *   +0  : SESS_MAGIC0
 *   +1  : SESS_MAGIC1
 *   +2  : nonces  (RADIOLIB_LORAWAN_NONCES_BUF_SIZE  = 14 B)
 *   +16 : session (RADIOLIB_LORAWAN_SESSION_BUF_SIZE = 302 B)
 * Total: 2 + 14 + 302 = 318 B -> 64 + 318 = 382 < 512 OK            */
#define SESS_NONCES_OFF           (SESS_EEPROM_ADDR + 2)
#define SESS_SESSION_OFF          (SESS_NONCES_OFF + RADIOLIB_LORAWAN_NONCES_BUF_SIZE)

#if (SESS_SESSION_OFF + RADIOLIB_LORAWAN_SESSION_BUF_SIZE) > EEPROM_ARENA_SIZE
#error "Bloque de sesion no cabe en la EEPROM emulada"
#endif

/* Guardado periódico de sesión: cada N uplinks exitosos (límite de desgaste
 * de flash; la sesión siempre se guarda completa tras un join). */
#define SESS_SAVE_EVERY_UPLINKS   32

#define RSSI_UNAVAILABLE          ((int16_t)0x8000)

/* ------------------------------------------------------------------ */
/* Pines SX1262 (internos del SiP ASR6502, no reasignables)            */
/*   NSS=P4_3=35, DIO1=P4_6=38, RST=P5_7=47, BUSY=P4_7=39             */
/* ------------------------------------------------------------------ */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

/* LoRaWAN: AU915 sub-banda 2 */
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;
LoRaWANNode node(&radio, &Region, subBand);

/* ------------------------------------------------------------------ */
/* Configuración persistida                                            */
/* ------------------------------------------------------------------ */
struct NodeConfig {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  sf;              /* 7..9 */
    uint32_t intervalSec;     /* 30..86400 */
};

static NodeConfig gCfg;

/* RSSI (dBm) del último downlink; 0x8000 = aún sin downlink */
static int16_t gLastRssi = RSSI_UNAVAILABLE;

/* Uplinks exitosos desde el último guardado de sesión */
static uint16_t gUplinksSinceSave = 0;

/* Buffers de downlink (globales para no ocupar pila) */
static uint8_t downlinkData[256];
static size_t  downlinkLen = 0;

/* ------------------------------------------------------------------ */
/* Diagnóstico                                                         */
/* ------------------------------------------------------------------ */
static const char* stateDecode(int16_t s) {
    switch (s) {
        case RADIOLIB_ERR_NONE:                return "OK";
        case RADIOLIB_ERR_CHIP_NOT_FOUND:      return "CHIP_NOT_FOUND";
        case RADIOLIB_ERR_PACKET_TOO_LONG:     return "PACKET_TOO_LONG";
        case RADIOLIB_ERR_RX_TIMEOUT:          return "RX_TIMEOUT";
        case RADIOLIB_ERR_MIC_MISMATCH:        return "MIC_MISMATCH";
        case RADIOLIB_ERR_NETWORK_NOT_JOINED:  return "NOT_JOINED";
        case RADIOLIB_ERR_NO_JOIN_ACCEPT:      return "NO_JOIN_ACCEPT";
        case RADIOLIB_ERR_CHECKSUM_MISMATCH:   return "CHECKSUM_MISMATCH";
        case RADIOLIB_ERR_NONCES_DISCARDED:    return "NONCES_DISCARDED";
        case RADIOLIB_ERR_SESSION_DISCARDED:   return "SESSION_DISCARDED";
        case RADIOLIB_LORAWAN_NEW_SESSION:     return "NEW_SESSION";
        case RADIOLIB_LORAWAN_SESSION_RESTORED:return "SESSION_RESTORED";
        default:                               return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Mapeo SF -> DR de AU915 (BW125)                                     */
/*   SF12->DR0, SF11->DR1, SF10->DR2, SF9->DR3, SF8->DR4, SF7->DR5    */
/* ------------------------------------------------------------------ */
static uint8_t drFromSf(uint8_t sf)
{
    switch (sf) {
        case 12: return 0;
        case 11: return 1;
        case 10: return 2;
        case 9:  return 3;
        case 8:  return 4;
        case 7:  return 5;
        default: return 5;
    }
}

/* ------------------------------------------------------------------ */
/* Configuración persistida (EEPROM emulada)                           */
/* ------------------------------------------------------------------ */
static void saveConfig(void)
{
    EEPROM.put(CFG_EEPROM_ADDR, gCfg);
    EEPROM.commit();
    debug("[CFG] EEPROM guardada: sf=%u interval=%u sec\r\n",
          (unsigned)gCfg.sf, (unsigned)gCfg.intervalSec);
}

static void loadConfig(void)
{
    EEPROM.begin(EEPROM_ARENA_SIZE);
    EEPROM.get(CFG_EEPROM_ADDR, gCfg);

    if (gCfg.magic0 != CFG_MAGIC0 || gCfg.magic1 != CFG_MAGIC1) {
        /* Primer arranque o EEPROM vacía -> defaults (SF7, 30 sec) */
        gCfg.magic0      = CFG_MAGIC0;
        gCfg.magic1      = CFG_MAGIC1;
        gCfg.sf          = 7;
        gCfg.intervalSec = TX_INTERVAL_DEFAULT_SEC;
        saveConfig();
    }

    /* Sanidad por si la EEPROM tiene basura con magic válido */
    if (gCfg.sf < SF_MIN || gCfg.sf > SF_MAX) gCfg.sf = 7;
    if (gCfg.intervalSec < INTERVAL_SEC_MIN || gCfg.intervalSec > INTERVAL_SEC_MAX) {
        gCfg.intervalSec = TX_INTERVAL_DEFAULT_SEC;
    }

    debug("[CFG] EEPROM: sf=%u interval=%u sec\r\n",
          (unsigned)gCfg.sf, (unsigned)gCfg.intervalSec);
}

/* ------------------------------------------------------------------ */
/* Persistencia de sesión LoRaWAN (nonces + session)                   */
/* ------------------------------------------------------------------ */
static void saveNoncesToEeprom(void)
{
    uint8_t* nonces = node.getBufferNonces();
    if (nonces == NULL) return;

    EEPROM.write(SESS_EEPROM_ADDR, SESS_MAGIC0);
    EEPROM.write(SESS_EEPROM_ADDR + 1, SESS_MAGIC1);
    for (uint16_t i = 0; i < RADIOLIB_LORAWAN_NONCES_BUF_SIZE; i++) {
        EEPROM.write(SESS_NONCES_OFF + i, nonces[i]);
    }
    EEPROM.commit();
    debug("[SESS] Nonces guardados en EEPROM (%u B)\r\n",
          (unsigned)RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
}

static void saveSessionToEeprom(void)
{
    uint8_t* nonces  = node.getBufferNonces();
    uint8_t* session = node.getBufferSession();
    if (nonces == NULL || session == NULL) return;

    EEPROM.write(SESS_EEPROM_ADDR, SESS_MAGIC0);
    EEPROM.write(SESS_EEPROM_ADDR + 1, SESS_MAGIC1);
    for (uint16_t i = 0; i < RADIOLIB_LORAWAN_NONCES_BUF_SIZE; i++) {
        EEPROM.write(SESS_NONCES_OFF + i, nonces[i]);
    }
    for (uint16_t i = 0; i < RADIOLIB_LORAWAN_SESSION_BUF_SIZE; i++) {
        EEPROM.write(SESS_SESSION_OFF + i, session[i]);
    }
    EEPROM.commit();
    gUplinksSinceSave = 0;
    debug("[SESS] Sesion completa guardada en EEPROM (%u B)\r\n",
          (unsigned)(RADIOLIB_LORAWAN_NONCES_BUF_SIZE + RADIOLIB_LORAWAN_SESSION_BUF_SIZE));
}

/* Guardado periódico para refrescar los frame counters sin desgastar flash */
static void saveSessionThrottled(void)
{
    if (++gUplinksSinceSave >= SESS_SAVE_EVERY_UPLINKS) {
        saveSessionToEeprom();
    }
}

/* Intenta cargar el bloque de sesión desde EEPROM en el nodo RadioLib.
 * Debe llamarse DESPUÉS de beginOTAA() y ANTES de activateOTAA().
 * Devuelve true si ambos buffers fueron aceptados (activateOTAA()
 * devolverá entonces SESSION_RESTORED sin tráfico de join).            */
static bool loadSession(void)
{
    if (EEPROM.read(SESS_EEPROM_ADDR) != SESS_MAGIC0 ||
        EEPROM.read(SESS_EEPROM_ADDR + 1) != SESS_MAGIC1) {
        debug("[SESS] Sin bloque de sesion en EEPROM (primer arranque)\r\n");
        return false;
    }

    static uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    static uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

    for (uint16_t i = 0; i < RADIOLIB_LORAWAN_NONCES_BUF_SIZE; i++) {
        nonces[i] = EEPROM.read(SESS_NONCES_OFF + i);
    }
    for (uint16_t i = 0; i < RADIOLIB_LORAWAN_SESSION_BUF_SIZE; i++) {
        session[i] = EEPROM.read(SESS_SESSION_OFF + i);
    }

    int16_t st = node.setBufferNonces(nonces);
    if (st != RADIOLIB_ERR_NONE) {
        debug("[SESS] setBufferNonces=%d (%s); se hara join nuevo\r\n",
              st, stateDecode(st));
        return false;
    }
    st = node.setBufferSession(session);
    if (st != RADIOLIB_ERR_NONE) {
        debug("[SESS] setBufferSession=%d (%s); no restaurable, join nuevo\r\n",
              st, stateDecode(st));
        return false;
    }
    debug("[SESS] Bloque de sesion cargado; restaurando sin re-join...\r\n");
    return true;
}

/* ------------------------------------------------------------------ */
/* Aplicar DR del SF configurado                                       */
/* ------------------------------------------------------------------ */
static void applyDrFromConfig(void)
{
    int16_t s = node.setDatarate(drFromSf(gCfg.sf));
    debug("[DR] setDatarate(%u)=%d maxPayload=%u B\r\n",
          (unsigned)drFromSf(gCfg.sf), s, (unsigned)node.getMaxPayloadLen());
}

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

/* Recuperación ante fallo irrecuperable de init: soft-reset del core ARM.
 * Respaldo: deep sleep en vez de bucle ocupado (nunca while(true) delay(1)). */
static void fatalErrorReset(void)
{
    debug("[INIT] Auto-reset en 1 s...\r\n");
    Serial.flush();
    delay(1000);
    NVIC_SystemReset();       /* soft-reset del core ARM */
    while (true) {
        radio.sleep();
        deepSleepMillis(10000);
    }
}

/* ------------------------------------------------------------------ */
/* Re-join automático + ADR off + DR según config                      */
/* ------------------------------------------------------------------ */
static bool ensureJoined(void)
{
    if (node.isActivated()) return true;

    debug("[REJOIN] Iniciando join OTAA (AU915 SB2)...\r\n");
    int16_t state = node.activateOTAA();

    /* Persistir nonces SIEMPRE (éxito o fallo): el DevNonce debe avanzar
     * monótonamente aunque el intento falle, para que un corte de energía
     * no reutilice DevNonce (error -1116 del UG56). */
    saveNoncesToEeprom();

    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[REJOIN] +++ OK +++ (%s)\r\n", stateDecode(state));
        node.setADR(false);
        node.setDwellTime(false);   /* defensa: evitar bloqueo por dwell time AU915 */
        applyDrFromConfig();
        return true;
    }
    debug("[REJOIN] Fallo: %d (%s)\r\n", state, stateDecode(state));
    return false;
}

/* ------------------------------------------------------------------ */
/* Payload: [0x01 | uint32 BE random]                                  */
/* ------------------------------------------------------------------ */
static size_t buildPayload(uint8_t* out)
{
    uint32_t r = ((uint32_t)random() << 16) ^ (uint32_t)random();
    out[0] = PAYLOAD_VERSION_OK;
    out[1] = (uint8_t)(r >> 24);
    out[2] = (uint8_t)(r >> 16);
    out[3] = (uint8_t)(r >> 8);
    out[4] = (uint8_t)(r);
    return PAYLOAD_LEN;
}

/* ------------------------------------------------------------------ */
/* Downlink — whitelist estricta de comandos                           */
/*  0x01 <sf>        -> cambiar SF (7..9) + persistir                  */
/*  0x02 <hi> <lo>   -> cambiar intervalo en segundos (30 a 65535) + persistir */
/*  0x03 [0x00]      -> consulta RSSI del último downlink (log serial) */
/* ------------------------------------------------------------------ */
static void handleDownlink(const uint8_t* data, size_t len)
{
    if (len < 1) return;

    uint8_t cmd = data[0];

    if (cmd == DL_CMD_SF) {
        if (len < 2) {
            debug("[DL] SF: faltan datos; ignorado\r\n");
            return;
        }
        uint8_t sf = data[1];
        if (sf >= SF_MIN && sf <= SF_MAX) {
            gCfg.sf = sf;
            node.setADR(false);
            applyDrFromConfig();
            saveConfig();
            debug("[DL] SF cambiado a %u (DR%u)\r\n",
                  (unsigned)sf, (unsigned)drFromSf(sf));
        } else {
            debug("[DL] SF %u fuera de rango (7..9); ignorado\r\n", (unsigned)sf);
        }
        return;
    }

    if (cmd == DL_CMD_INTERVAL) {
        if (len < 3) {
            debug("[DL] Intervalo: faltan datos; ignorado\r\n");
            return;
        }
        uint16_t seconds = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        if (seconds >= INTERVAL_SEC_MIN && seconds <= INTERVAL_SEC_MAX) {
            gCfg.intervalSec = seconds;
            saveConfig();
            debug("[DL] Intervalo cambiado a %u sec\r\n", (unsigned)seconds);
        } else {
            debug("[DL] Intervalo %u sec fuera de rango (30..65535); ignorado\r\n",
                  (unsigned)seconds);
        }
        return;
    }

    if (cmd == DL_CMD_QUERY_RSSI) {
        if (gLastRssi == RSSI_UNAVAILABLE) {
            debug("[DL] Consulta RSSI: sin medicion disponible aun\r\n");
        } else {
            debug("[DL] Consulta RSSI: ultimo downlink RSSI=%d dBm\r\n",
                  (int)gLastRssi);
        }
        return;
    }

    debug("[DL] Comando 0x%02X desconocido; ignorado\r\n", cmd);
}

/* ------------------------------------------------------------------ */
/* Uplink + ventana RX + downlink + captura RSSI                       */
/* ------------------------------------------------------------------ */
static int sendReport(const uint8_t* payload, size_t payloadLen)
{
    debug("[UPLINK] port=1 len=%u payload=", (unsigned)payloadLen);
    for (size_t i = 0; i < payloadLen; i++) {
        debug("%02X", payload[i]);
    }
    debug(" (sf=%u max=%u B)\r\n", (unsigned)gCfg.sf,
          (unsigned)node.getMaxPayloadLen());

    LoRaWANEvent_t evtUp, evtDown;

    downlinkLen = sizeof(downlinkData);
    int state = node.sendReceive(payload, payloadLen, 1,
                                 downlinkData, &downlinkLen,
                                 false, &evtUp, &evtDown);

    if (state == RADIOLIB_ERR_NONE) {
        debug("[UPLINK] OK (sin downlink)\r\n");
        saveSessionThrottled();
    } else if (state > 0) {
        /* Capturar el RSSI real del downlink recibido.
         * OJO: LoRaWANEvent_t.power en RadioLib 7.7.1 NO es el RSSI. El RSSI
         * real se lee de la capa PHY con radio.getRSSI() ANTES de que el
         * radio entre en sleep. */
        gLastRssi = (int16_t)radio.getRSSI();
        debug("[RX] %u bytes: ", (unsigned)downlinkLen);
        for (size_t i = 0; i < downlinkLen; i++) {
            debug("%02X", downlinkData[i]);
        }
        debug(" (RSSI=%d dBm)\r\n", (int)gLastRssi);
        handleDownlink(downlinkData, downlinkLen);
        saveSessionThrottled();
    } else {
        debug("[UPLINK] ERROR %d (%s)\r\n", state, stateDecode(state));
    }

    return state;
}

/* ------------------------------------------------------------------ */
/* SETUP                                                               */
/* ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    delay(3000);   /* espera al monitor serial */

    debug("\r\n[INIT] Cubecell-htcc-ab01-2 — HTCC-AB01 (RadioLib) — AU915 SB2\r\n");
    debug("[INIT] DevEUI=%016llX JoinEUI=%016llX\r\n",
          (unsigned long long)devEui, (unsigned long long)joinEui);

    /* Semilla del PRNG: ruido del ADC flotante + tiempo de arranque */
    uint32_t seed = micros() ^ ((uint32_t)millis() << 16);
    seed ^= ((uint32_t)analogRead(ADC) << 8);
    seed ^= (uint32_t)analogRead(ADC);
    randomSeed(seed);
    debug("[INIT] PRNG seed=0x%08X\r\n", (unsigned)seed);

    /* Configuración persistida (SF e intervalo) */
    loadConfig();

    /* Salida conmutada apagada (no hay sensor externo en esta fase) */
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);

    /* Inicializar radio */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Radio FAILED: %d (%s)\r\n", state, stateDecode(state));
        fatalErrorReset();
    }
    debug("[INIT] Radio OK\r\n");

    /* Inicializar nodo OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Node FAILED: %d (%s)\r\n", state, stateDecode(state));
        fatalErrorReset();
    }
    debug("[INIT] Node OK\r\n");

    /* Workaround bug de timers (esperas de radio) */
    node.scanGuard = 200;
    debug("[INIT] scanGuard=%d ms\r\n", node.scanGuard);

    /* Restaurar sesión persistida (si existe) y activar */
    bool hadSavedSession = loadSession();

    debug("[JOIN] %s (AU915 SB2)...\r\n",
          hadSavedSession ? "Restaurando sesion" : "Iniciando OTAA");

    state = node.activateOTAA();

    if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        debug("[JOIN] +++ SESION RESTAURADA +++ (sin re-join) DevAddr=0x%08X\r\n",
              (unsigned)node.getDevAddr());
    } else if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        debug("[JOIN] +++ JOINED +++ DevAddr=0x%08X\r\n",
              (unsigned)node.getDevAddr());
        /* Persistir la sesión para sobrevivir a reset/power-cycle */
        saveSessionToEeprom();
    } else {
        debug("[JOIN] Fallo: %d (%s)\r\n", state, stateDecode(state));
        /* Persistir nonces aunque el join falle (DevNonce avanza) */
        saveNoncesToEeprom();
    }

    /* Tras join/restauración: ADR off, dwell time off y DR de la config */
    if (node.isActivated()) {
        node.setADR(false);
        node.setDwellTime(false);
        applyDrFromConfig();
    }

    debug("[INIT] Ciclo de %u sec (%lu ms). Listo.\r\n",
          (unsigned)gCfg.intervalSec,
          (unsigned long)gCfg.intervalSec * 1000UL);
    Serial.flush();
}

/* ------------------------------------------------------------------ */
/* LOOP                                                                */
/* ------------------------------------------------------------------ */
void loop() {
    /* Re-join si se pierde la sesión */
    if (!node.isActivated()) {
        if (!ensureJoined()) {
            deepSleepMillis(10000);   /* backoff y reintento */
            return;
        }
        saveSessionToEeprom();
    }

    /* Payload: 5 bytes [0x01 | uint32 BE random] */
    uint8_t payload[PAYLOAD_LEN];
    buildPayload(payload);

    /* Uplink por puerto 1 + ventana RX (downlink + RSSI) */
    sendReport(payload, PAYLOAD_LEN);

    /* Deep sleep según intervalo configurado (segundos) */
    uint32_t sleepMs = (uint32_t)gCfg.intervalSec * 1000UL;
    debug("[SLEEP] Durmiendo %lu ms (%u sec)...\r\n",
          (unsigned long)sleepMs, (unsigned)gCfg.intervalSec);
    Serial.flush();
    deepSleepMillis(sleepMs);
}
