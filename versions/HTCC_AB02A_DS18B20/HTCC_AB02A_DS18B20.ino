/*
 * HTCC_AB02A_DS18B20.ino — Nodo Temperatura LoRaWAN Low-Power (v1.2, RadioLib)
 * ===========================================================================
 * Heltec CubeCell HTCC-AB02A (ASR6502 + SX1262)
 * Region: AU915 sub-banda 2 | OTAA | Clase A
 * Librería: RadioLib 7.7.1 (NO stack nativo LoRaWan_APP.h)
 *
 * OBJETIVO v1.2:
 *   - Leer un sensor DS18B20 (cápsula sumergible 1 m, solo temperatura) por
 *     protocolo OneWire con bit-banging manual (sin DallasTemperature).
 *   - Power gating del sensor: VDD (GPIO3/P6_2) se alimenta solo durante la
 *     medición y se apaga junto con el MCU en el deep sleep (consumo ~0).
 *   - Uplink no confirmado (puerto 1, 4 bytes big-endian): temperatura en
 *     décimas de °C (int16 BE) + batería en mV (uint16 BE).
 *   - Ciclo de 10 min: deep sleep real (TimerEvent_t RTC + lowPowerHandler()).
 *   - Re-join OTAA automático si se pierde la sesión.
 *   - 1 salida digital controlable por downlink (GPIO1/P6_0).
 *
 * PAYLOAD v1.2 (puerto 1, big-endian, 4 bytes):
 *   [0:1]  temperatura  int16  BE  décimas de °C (complemento a dos)
 *          ej: 234 = 23.4 °C, -55 = -5.5 °C. Rango físico DS18B20: -550..+1250.
 *          Sentinel DS18B20_INVALID (0x7FFF) si desconectado/corto/CRC inválido.
 *   [2:3]  batería      uint16 BE  mV (getBatteryVoltage)
 *
 * DOWNLINK (puerto 1), primer byte comanda GPIO1/P6_0:
 *   0x01 -> ON (HIGH) | 0x00 -> OFF (LOW) | 0x02 -> TOGGLE
 *
 * BAJO CONSUMO:
 *   - Deep sleep del MCU: lowPowerHandler() (CySysPmDeepSleep) + RTC.
 *   - Radio SX1262 en sleep: radio.sleep() (~0.6 µA).
 *   - Despertar por TimerEvent_t (RTC) a los 600000 ms.
 *   - VDD del DS18B20 (GPIO3/P6_2) en LOW durante el sleep.
 *   - DQ del DS18B20 (GPIO4/P6_4) en alta impedancia (INPUT) durante el sleep
 *     para no inyectar corriente por el pin con el sensor apagado.
 *
 * ONEWIRE (bit-banging manual):
 *   - Se implementa el protocolo OneWire directamente sobre GPIO4/P6_4 con
 *     control total del timing. Las fases de bit-bang se envuelven en
 *     noInterrupts()/interrupts() para evitar glitches de timing en el ASR6502.
 *   - No se usa DallasTemperature (1 solo sensor, resolución fija 12 bits,
 *     sin búsqueda de ROM ni conversión asíncrona) -> ahorro de Flash/RAM.
 *   - Fallback de timing (I-003): si las lecturas fallan por latencia de
 *     digitalWrite/digitalRead del core CubeCell (CRC inválido intermitente),
 *     migrar el bit-bang a digitalWriteFast o acceso directo a registros GPIO
 *     del ASR6502 dentro de las mismas fases noInterrupts(). Verificar en
 *     campo con >=10 lecturas consecutivas sin CRC inválido antes de dar por
 *     bueno el timing. No hay cambio de código inmediato: solo documentación.
 *
 * ===========================================================================
 */

#include <RadioLib.h>
#include "Arduino.h"
#include <math.h>          /* roundf() para décimas de °C */

/* ------------------------------------------------------------------ */
/* Pines SX1262 (internos del SiP ASR6502, no reasignables)            */
/*   NSS=P4_3=35, DIO1=P4_6=38, RST=P5_7=47, BUSY=P4_7=39             */
/* ------------------------------------------------------------------ */
Module mod(35, 38, 47, 39);
SX1262 radio(&mod);

/* ================================================================== */
/* CREDENCIALES OTAA — CAMBIAR POR TARJETA ANTES DE FLASHEAR           */
/* En producción masiva cada tarjeta lleva su propio DevEUI/AppKey.    */
/* Los valores de abajo son placeholders de ejemplo (base v1.1).       */
/* El DevEUI real debe registrarse en el NS (gateway Milesight         */
/* UG56/UG65). NwkKey == AppKey asume LoRaWAN 1.0.x; si el NS usa      */
/* 1.1 con NwkKey distinta, editar OTAA_NWKKEY independientemente.     */
/* ================================================================== */
#define OTAA_DEVEUI   0x70B3D56F27CEE98BULL          /* DevEUI, big-endian  */
#define OTAA_APPEUI   0x24E124C0002ADD01ULL          /* JoinEUI/AppEUI      */
#define OTAA_APPKEY   { 0x89,0xF5,0xA1,0xA2,0x4D,0x63,0x47,0x57,            \
                        0x91,0xF8,0xBF,0x33,0xEA,0x03,0x14,0x86 }  /* 16 B */
#define OTAA_NWKKEY   { 0x89,0xF5,0xA1,0xA2,0x4D,0x63,0x47,0x57,            \
                        0x91,0xF8,0xBF,0x33,0xEA,0x03,0x14,0x86 }  /* 16 B */

uint64_t joinEui = OTAA_APPEUI;                       /* JoinEUI/AppEUI */
uint64_t devEui  = OTAA_DEVEUI;                       /* DevEUI         */
uint8_t appKey[] = OTAA_APPKEY;                       /* 16 B           */
uint8_t nwkKey[] = OTAA_NWKKEY;                       /* 16 B           */

/* Región AU915, sub-banda 2 */
const LoRaWANBand_t Region = AU915;
const uint8_t subBand = 2;

LoRaWANNode node(&radio, &Region, subBand);

/* ------------------------------------------------------------------ */
/* Configuración de pines DS18B20                                      */
/*   VDD = GPIO3 / P6_2 (PWM1): power gating del sensor                */
/*   DQ  = GPIO4 / P6_4 (PWM2): datos OneWire (pull-up ext. 4.7 kΩ     */
/*                              conectado a VDD del sensor, NO al 3.3 V */
/*                              fijo — así el pull-up queda sin         */
/*                              corriente al apagar VDD en sleep)      */
/* ------------------------------------------------------------------ */
const uint8_t DS18B20_VDD_PIN = GPIO3;     /* P6_2 — alimentación (power gating) */
const uint8_t DS18B20_DQ_PIN  = GPIO4;     /* P6_4 — línea de datos OneWire      */

/* Salida controlable por downlink (heredada del base v1.1) */
const uint8_t OUTPUT_PIN = GPIO1;          /* P6_0 */

/* ------------------------------------------------------------------ */
/* Constantes de operación                                             */
/* ------------------------------------------------------------------ */
#define SLEEP_INTERVAL_MS        600000UL   /* ciclo 10 min                 */
#define DS18B20_POWERUP_SETTLE_MS  10       /* estabilización tras VDD ON   */
#define DS18B20_CONV_TIME_MS      750       /* conversión 12 bits           */
#define DS18B20_RESOLUTION_BITS    12       /* fija, documental             */
#define DS18B20_INVALID          0x7FFF     /* sentinel error lectura       */
#define DS18B20_CMD_CONVERT      0x44       /* Convert T                    */
#define DS18B20_CMD_READ_SCRATCH 0xBE       /* Read Scratchpad              */
#define DS18B20_CMD_SKIP_ROM     0xCC       /* Skip ROM (1 solo sensor)     */

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
/* OneWire / DS18B20 — bit-banging manual                              */
/*                                                                     */
/* Convenciones de línea (bus OneWire, open-drain):                    */
/*   - Para escribir '0' o leer, el MCU pone DQ en OUTPUT y lo baja.   */
/*   - Para soltar la línea, DQ se pone en INPUT (alta impedancia) y   */
/*     el pull-up externo 4.7 kΩ (a VDD del sensor) la sube a HIGH.    */
/*   - Las fases de bit-bang se ejecutan con interrupciones            */
/*     deshabilitadas (noInterrupts/interrupts) para que el RTC o el   */
/*     radio no rompan los time-slots del bus. La espera de conversión */
/*     (750 ms) queda FUERA de noInterrupts().                         */
/* ------------------------------------------------------------------ */

/* Línea a LOW (drive): OUTPUT + LOW */
static void ds18b20DQDriveLow(void) {
    pinMode(DS18B20_DQ_PIN, OUTPUT);
    digitalWrite(DS18B20_DQ_PIN, LOW);
}

/* Soltar la línea: INPUT (alta impedancia), el pull-up externo sube */
static void ds18b20DQRelease(void) {
    pinMode(DS18B20_DQ_PIN, INPUT);
}

/* Reset OneWire: pulso LOW 480 µs + ventana de presencia.
 * Devuelve true si un DS18B20 respondió con el pulso de presencia.
 * DEBE llamarse con interrupciones deshabilitadas.                   */
static bool ds18b20Reset(void) {
    ds18b20DQDriveLow();
    delayMicroseconds(480);      /* pulso de reset >= 480 µs */
    ds18b20DQRelease();
    delayMicroseconds(70);       /* ventana de presencia 15-60 µs tras soltar */
    bool present = (digitalRead(DS18B20_DQ_PIN) == LOW);
    delayMicroseconds(410);      /* completar slot de reset (total ~960 µs) */
    return present;
}

/* Write time slot: bit 1 -> LOW corto (1-15 µs) y soltar;
 * bit 0 -> LOW sostenido (60-120 µs). LSB primero.
 * DEBE llamarse con interrupciones deshabilitadas.                   */
static void ds18b20WriteByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x01) {
            /* Write 1: LOW 1-15 µs, luego soltar el resto del slot */
            ds18b20DQDriveLow();
            delayMicroseconds(6);
            ds18b20DQRelease();
            delayMicroseconds(60);
        } else {
            /* Write 0: LOW sostenido 60-120 µs */
            ds18b20DQDriveLow();
            delayMicroseconds(60);
            ds18b20DQRelease();
            delayMicroseconds(6);
        }
        b >>= 1;
    }
}

/* Read time slot: LOW corto, soltar y muestrear la línea a ~15 µs.
 * LSB primero. DEBE llamarse con interrupciones deshabilitadas.      */
static uint8_t ds18b20ReadByte(void) {
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {
        b >>= 1;
        ds18b20DQDriveLow();
        delayMicroseconds(3);
        ds18b20DQRelease();
        delayMicroseconds(8);
        if (digitalRead(DS18B20_DQ_PIN) == HIGH) b |= 0x80;
        delayMicroseconds(50);   /* completar el slot (~61 µs) */
    }
    return b;
}

/* CRC-8 Dallas (polinomio x^8+x^5+x^4+1 = 0x31, resto 0x8C).
 * Valida el byte 8 (CRC) del scratchpad contra los bytes 0..7.       */
static uint8_t ds18b20Crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t b = *data++;
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

/* Lee la temperatura del DS18B20 (resolución fija 12 bits).
 *
 * Secuencia: reset -> skip ROM -> Convert T (0x44) -> espera 750 ms
 *            -> reset -> skip ROM -> Read Scratchpad (0xBE) -> 9 bytes
 *            -> validar CRC-8 -> calcular décimas de °C.
 *
 * Deja el sensor alimentado (VDD en HIGH) para que el llamador decida
 * cuándo apagar el power gating.
 *
 * En caso de error (sin presencia, CRC inválido) escribe
 * DS18B20_INVALID en *tenthsOut y devuelve false; el nodo NO aborta el
 * ciclo y uplinkea igualmente con el sentinel.
 */
static bool ds18b20ReadTemperature(int16_t* tenthsOut) {
    /* 1. Power-gate ON + estabilización */
    digitalWrite(DS18B20_VDD_PIN, HIGH);
    delay(DS18B20_POWERUP_SETTLE_MS);

    /* 2-3. Reset + presencia; skip ROM; ordenar conversión (bit-bang) */
    noInterrupts();
    bool present = ds18b20Reset();
    if (present) {
        ds18b20WriteByte(DS18B20_CMD_SKIP_ROM);
        ds18b20WriteByte(DS18B20_CMD_CONVERT);   /* Convert T, 12 bits */
    }
    interrupts();

    if (!present) {
        debug("[TEMP] sin presencia (desconectado/corto)\r\n");
        *tenthsOut = DS18B20_INVALID;
        return false;
    }

    /* 4. Esperar conversión 12 bits (interrupciones HABILITADAS: el
     * RTC sigue corriendo y no se debe bloquear el MCU aquí) */
    delay(DS18B20_CONV_TIME_MS);

    /* 5. Reset + skip ROM + leer scratchpad (9 bytes, bit-bang) */
    uint8_t scratch[9];
    bool scratchRead = false;
    noInterrupts();
    if (ds18b20Reset()) {
        scratchRead = true;
        ds18b20WriteByte(DS18B20_CMD_SKIP_ROM);
        ds18b20WriteByte(DS18B20_CMD_READ_SCRATCH);  /* Read Scratchpad */
        for (uint8_t i = 0; i < 9; i++) {
            scratch[i] = ds18b20ReadByte();
        }
    }
    interrupts();

    if (!scratchRead) {
        debug("[TEMP] sensor desaparecio entre fases\r\n");
        *tenthsOut = DS18B20_INVALID;
        return false;
    }

    /* 6. Validar CRC-8 (Dallas) del byte 8 contra bytes 0..7 */
    if (ds18b20Crc8(scratch, 8) != scratch[8]) {
        debug("[TEMP] CRC invalido\r\n");
        *tenthsOut = DS18B20_INVALID;
        return false;
    }

    /* 6b. Sanidad del scratchpad (I-001): un DS18B20 real devuelve valores
     * fijos en los bytes reservados (5=0xFF, 6=0x0C, 7=0x10). Si DQ queda en
     * corto a GND, el scratchpad lee todo-ceros y el CRC-8 de 8 ceros es 0x00
     * (válido), por lo que sin este chequeo se reportaría 0.0 °C en lugar del
     * sentinel. Adicionalmente se validan los bits fijos del registro de
     * configuración (byte 4): bit 7 = 0 y bits 4..0 = 1 (los bits R1/R0 de
     * resolución pueden variar 9..12 bits, por eso se enmascaran).
     * Si la validación falla se escribe DS18B20_INVALID y se devuelve false
     * SIN abortar el ciclo: el llamador uplinkea igualmente el sentinel y
     * apaga el power gating. */
    if (!(scratch[5] == 0xFF && scratch[6] == 0x0C && scratch[7] == 0x10) ||
        (scratch[4] & 0x9F) != 0x1F) {
        debug("[TEMP] scratchpad insano (cfg=0x%02X 5=0x%02X 6=0x%02X "
              "7=0x%02X) -> DQ corto a GND?\r\n",
              scratch[4], scratch[5], scratch[6], scratch[7]);
        *tenthsOut = DS18B20_INVALID;
        return false;
    }

    /* 7. Calcular temperatura: raw (12 bits) * 0.0625 °C/LSB */
    int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
    float tempC = raw * 0.0625f;
    *tenthsOut = (int16_t)roundf(tempC * 10.0f);

    debug("[TEMP] raw=%d -> %.1f C -> %d decimas (0x%04X)\r\n",
          raw, tempC, *tenthsOut, (uint16_t)*tenthsOut);
    return true;
}

/* ------------------------------------------------------------------ */
/* Lectura de batería (heredada del base)                              */
/* ------------------------------------------------------------------ */
static uint16_t readBatteryMv(void) {
    uint16_t mv = getBatteryVoltage();
    debug("[BATT] %u mV\r\n", mv);
    return mv;
}

/* ------------------------------------------------------------------ */
/* Enviar uplink de telemetría (4 bytes, puerto 1) + capturar downlink */
/* ------------------------------------------------------------------ */
static int sendSensorReport(void) {
    /* Leer temperatura (deja VDD en HIGH) */
    int16_t tenths;
    ds18b20ReadTemperature(&tenths);

    /* Power gating OFF: cortar alimentación del sensor.
     * DQ en alta impedancia para no inyectar corriente por el pin
     * con el sensor apagado (pull-up ext. a VDD queda sin corriente). */
    digitalWrite(DS18B20_VDD_PIN, LOW);
    pinMode(DS18B20_DQ_PIN, INPUT);

    /* Leer batería (ADC interno P2_0, independiente del sensor) */
    uint16_t bat = readBatteryMv();

    /* Payload 4 bytes big-endian */
    uint8_t payload[4];
    payload[0] = (uint8_t)((uint16_t)tenths >> 8);  /* temp int16 BE */
    payload[1] = (uint8_t)((uint16_t)tenths & 0xFF);
    payload[2] = (uint8_t)(bat >> 8);               /* batt uint16 BE */
    payload[3] = (uint8_t)(bat & 0xFF);

    debug("[UPLINK] port=1 payload=");
    for (uint8_t i = 0; i < sizeof(payload); i++) {
        debug("%02X", payload[i]);
    }
    debug(" (4 B)\r\n");

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
/* Reinicio controlado ante fallos fatales de inicialización (I-004)   */
/* ------------------------------------------------------------------ */
/* Elección: se usa NVIC_SystemReset() — soft-reset del core ARM       */
/* Cortex-M0+ del ASR6502 expuesto por CMSIS en el core CubeCell —     */
/* para volver a un estado limpio sin dejar el nodo colgado.           */
/* Si por cualquier motivo el reset no actuara, se cae a un reintento  */
/* periódico con deep sleep real (batería-friendly), nunca a un bucle  */
/* ocupado while(true) delay(1) sin salida.                            */
static void fatalErrorReset(void) {
    debug("[FATAL] Fallo fatal de init -> reinicio controlado\r\n");
    Serial.flush();
    delay(1000);
    NVIC_SystemReset();
    /* Solo se alcanza si NVIC_SystemReset no está disponible o no actuó:
     * reintento periódico (deep sleep + despertar), no un bucle ocupado. */
    while (true) {
        radio.sleep();
        deepSleepMillis(10000);
    }
}

/* ------------------------------------------------------------------ */
/* SETUP                                                               */
/* ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    delay(3000);

    debug("\r\n[INIT] HTCC-AB02A nodo DS18B20 v1.2 (RadioLib) — AU915 SB2\r\n");
    debug("[INIT] DevEUI=%016llX JoinEUI=%016llX\r\n",
          (unsigned long long)devEui, (unsigned long long)joinEui);

    /* Configurar GPIOs */
    pinMode(OUTPUT_PIN, OUTPUT);
    digitalWrite(OUTPUT_PIN, LOW);           /* estado inicial: apagado */

    /* DS18B20: VDD apagado inicialmente, DQ en alta impedancia.
     * (sin pull-up interno para no atar DQ al 3.3 V fijo) */
    pinMode(DS18B20_VDD_PIN, OUTPUT);
    digitalWrite(DS18B20_VDD_PIN, LOW);
    pinMode(DS18B20_DQ_PIN, INPUT);

    debug("[INIT] GPIOs: out=P6_0, ds18b20_vdd=P6_2, ds18b20_dq=P6_4\r\n");

    /* Inicializar radio */
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Radio FAILED: %d (%s)\r\n", state, stateDecode(state));
        fatalErrorReset();   /* reinicio controlado (no bucle infinito) */
    }
    debug("[INIT] Radio OK\r\n");

    /* Inicializar nodo OTAA */
    state = node.beginOTAA(joinEui, devEui, nwkKey, appKey);
    if (state != RADIOLIB_ERR_NONE) {
        debug("[INIT] Node FAILED: %d (%s)\r\n", state, stateDecode(state));
        fatalErrorReset();   /* reinicio controlado (no bucle infinito) */
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

    /* Reportar temperatura + batería (estado del instante) */
    sendSensorReport();

    /* Volver a dormir 10 minutos (deep sleep real) */
    debug("[SLEEP] Durmiendo %lu ms...\r\n", (unsigned long)SLEEP_INTERVAL_MS);
    Serial.flush();
    deepSleepMillis(SLEEP_INTERVAL_MS);
}
