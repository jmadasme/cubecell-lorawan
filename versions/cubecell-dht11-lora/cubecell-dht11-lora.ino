/*
 * cubecell-dht11-lora.ino
 * ==========================
 * Heltec CubeCell HTCC-AB02A — Sensor DHT11 LoRaWAN de ultra-bajo consumo
 * 
 * Ciclo: deep sleep 10 min → despertar → leer DHT11 → enviar LoRaWAN → dormir
 * Payload 4 bytes: random(2B) + temperatura_en_décimas(2B), little-endian
 * 
 * Batería: ER14250 1200mAh (Li-SOCl2 3.6V)
 * Autonomía estimada: > 1 año (típico ~5 años en condiciones nominales)
 * 
 * Conexiones DHT11:
 *   VCC → GPIO3 (P6_2) — power gating controlado por firmware
 *   DATA → GPIO2 (P6_1) — protocolo one-wire
 *   GND → GND
 * 
 * Placa: Heltec CubeCell HTCC-AB02A (ASR6501 Cortex-M0+ + SX1262)
 * Región: US915 (configurable en Arduino IDE Tools > LoRaWAN Region)
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"

/* ============================================================
 * CREDENCIALES LoRaWAN OTAA
 * ============================================================ */
#include "credentials.h"

/* ============================================================
 * CONFIGURACIÓN LoRaWAN
 * ============================================================ */

/* Canales LoRaWAN — default: canales 0-7 de la sub-banda 1 (US915) */
uint16_t userChannelsMask[6] = { 0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };

/* Región LoRaWAN — definida en Arduino IDE Tools > LoRaWAN Region */
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/* Clase A (mínimo consumo de batería) */
DeviceClass_t loraWanClass = LORAWAN_CLASS;

/* Ciclo de transmisión: 10 minutos = 600,000 ms */
uint32_t appTxDutyCycle = 600000;   // 10 minutos

/* Activación OTAA (definido en Tools) */
bool overTheAirActivation = LORAWAN_NETMODE;

/* ADR (Adaptive Data Rate) — definido en Tools */
bool loraWanAdr = LORAWAN_ADR;

/* Preservar información de red en flash — NO re-joinear tras sleep */
bool keepNet = LORAWAN_NET_RESERVE;

/* Unconfirmed uplink (mínimo consumo, sin esperar RX) */
bool isTxConfirmed = LORAWAN_UPLINKMODE;

/* Puerto de aplicación LoRaWAN */
uint8_t appPort = 1;

/* Número de reintentos para mensajes confirmados (solo si isTxConfirmed=true) */
uint8_t confirmedNbTrials = 4;

/* ============================================================
 * CONSTANTES DEL SENSOR DHT11
 * ============================================================ */

/* Pines de conexión */
#define DHT11_POWER_PIN    GPIO3    /* P6_2 — alimentación del sensor (power gating) */
#define DHT11_DATA_PIN     GPIO2    /* P6_1 — datos one-wire */

/* Temporización */
#define DHT11_STABILIZE_MS 1000     /* Tiempo de estabilización tras power-on */
#define DHT11_TIMEOUT_US   200      /* Timeout por bit (margen para batería baja/variaciones) */

/* Valor centinela para indicar error de lectura */
#define TEMP_ERROR_VALUE   0xFFFF   /* Valor centinela para indicar error de lectura (según PLAN) */

/* Debug serial (deshabilitar en producción para ahorrar ~1mA) */
#define SERIAL_DEBUG       1
#define SERIAL_BAUD        115200

/* ============================================================
 * PROTOTIPOS DE FUNCIONES AUXILIARES
 * ============================================================ */

int16_t  leerDHT11(void);
bool     dht11ReadByte(uint8_t *data);
bool     dht11WaitFor(uint8_t expected, uint32_t timeout_us);
uint16_t generarRandom(void);

/* ============================================================
 * PREPARACIÓN DEL PAYLOAD (llamado por el framework antes de TX)
 * ============================================================ */

/**
 * Prepara el payload de 4 bytes para transmisión LoRaWAN.
 * 
 * Formato (little-endian):
 *   appData[0-1]: número aleatorio uint16_t
 *   appData[2-3]: temperatura en décimas de °C, int16_t
 *                 (ej: 245 = 24.5°C; 0xFFFF = error de lectura)
 */
static void prepareTxFrame(uint8_t port)
{
    /* --- Leer sensor DHT11 (con 1 reintento si falla) --- */
    int16_t temperatura = leerDHT11();
    if (temperatura == TEMP_ERROR_VALUE) {
        /* Un reintento tras fallo (requerido por PLAN) */
        temperatura = leerDHT11();
    }

    /* --- Generar número aleatorio --- */
    uint16_t randomValue = generarRandom();

    /* --- Construir payload --- */
    appDataSize = 4;
    appData[0] = (uint8_t)(randomValue & 0xFF);        /* Random LSB */
    appData[1] = (uint8_t)((randomValue >> 8) & 0xFF); /* Random MSB */
    appData[2] = (uint8_t)(temperatura & 0xFF);         /* Temp LSB */
    appData[3] = (uint8_t)((temperatura >> 8) & 0xFF);  /* Temp MSB */

#if SERIAL_DEBUG
    Serial.print("Payload [4B]: random=");
    Serial.print(randomValue);
    Serial.print(" (0x");
    Serial.print(randomValue, HEX);
    Serial.print("), temp=");
    if (temperatura == TEMP_ERROR_VALUE) {
        Serial.print("ERROR");
    } else {
        Serial.print(temperatura / 10);
        Serial.print(".");
        Serial.print(abs(temperatura) % 10);
        Serial.print(" C");
    }
    Serial.print(" | HEX: ");
    for (int i = 0; i < 4; i++) {
        if (appData[i] < 0x10) Serial.print("0");
        Serial.print(appData[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
#endif
}

/* ============================================================
 * SETUP — ejecutado al arranque / tras reset por deep sleep
 * ============================================================ */

void setup()
{
#if SERIAL_DEBUG
    Serial.begin(SERIAL_BAUD);
    delay(100);
    Serial.println();
    Serial.println(F("=== CubeCell HTCC-AB02A | DHT11 + LoRaWAN ==="));
    Serial.println(F("Bateria: ER14250 1200mAh | Intervalo: 10 min"));
    Serial.println(F("Payload: random(2B) + temp(2B) | US915 OTAA SF7"));
    Serial.print(F("Region: "));
    Serial.println(loraWanRegion);
#endif

    /* --- Configurar pines del DHT11 --- */
    pinMode(DHT11_DATA_PIN, INPUT);
    pinMode(DHT11_POWER_PIN, OUTPUT);
    digitalWrite(DHT11_POWER_PIN, LOW);   /* DHT11 apagado por defecto */

    /* --- Inicializar semilla aleatoria (una sola vez) --- */
    randomSeed(analogRead(ADC));

    /* --- Pines no usados a INPUT_PULLDOWN (evitar corrientes de fuga) --- */
    pinMode(GPIO4, INPUT_PULLDOWN);       /* P6_4 */
    pinMode(GPIO5, INPUT_PULLDOWN);       /* P3_4 */
    pinMode(GPIO1, INPUT_PULLDOWN);       /* P6_0 — no usado */
    
    /* Vext: apagar periféricos externos (si hay) */
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);

    /* --- Inicializar máquina de estados --- */
    deviceState = DEVICE_STATE_INIT;

    /* --- Verificar si hay sesión LoRaWAN guardada (saltar JOIN si ya existe) --- */
    LoRaWAN.ifskipjoin();
}

/* ============================================================
 * LOOP — máquina de estados gestionada por el framework LoRaWAN
 * ============================================================ */

void loop()
{
    switch (deviceState)
    {
        case DEVICE_STATE_INIT:
        {
#if(LORAWAN_DEVEUI_AUTO)
            LoRaWAN.generateDeveuiByChipID();
#endif
#if(AT_SUPPORT)
            getDevParam();
#endif
            printDevParam();
            LoRaWAN.init(loraWanClass, loraWanRegion);
            deviceState = DEVICE_STATE_JOIN;
            break;
        }

        case DEVICE_STATE_JOIN:
        {
            LoRaWAN.join();
            break;
        }

        case DEVICE_STATE_SEND:
        {
            prepareTxFrame(appPort);
            LoRaWAN.send();
            deviceState = DEVICE_STATE_CYCLE;
            break;
        }

        case DEVICE_STATE_CYCLE:
        {
            /* Programar siguiente transmisión en 10 minutos */
            txDutyCycleTime = appTxDutyCycle + randr(0, APP_TX_DUTYCYCLE_RND);
            LoRaWAN.cycle(txDutyCycleTime);
            deviceState = DEVICE_STATE_SLEEP;
            break;
        }

        case DEVICE_STATE_SLEEP:
        {
#if SERIAL_DEBUG
            Serial.print(F("Deep sleep "));
            Serial.print(txDutyCycleTime / 1000);
            Serial.println(F(" segundos..."));
            Serial.flush();
#endif
            /* Asegurar DHT11 apagado antes de dormir */
            digitalWrite(DHT11_POWER_PIN, LOW);
            
            /* Entrar en deep sleep — el framework despierta al expirar el timer */
            LoRaWAN.sleep();
            break;
        }

        default:
        {
            deviceState = DEVICE_STATE_INIT;
            break;
        }
    }
}

/* ============================================================
 * LECTURA DEL SENSOR DHT11
 * Implementación manual bit-bang (sin dependencia de librerías)
 * ============================================================ */

/**
 * Lee el sensor DHT11 y retorna la temperatura en DÉCIMAS de °C.
 * Controla el power gating (GPIO3) automáticamente.
 * 
 * Protocolo DHT11:
 *   1. Host envía START: LOW 20ms → HIGH 40µs
 *   2. DHT11 responde: LOW 80µs → HIGH 80µs (ACK)
 *   3. DHT11 envía 40 bits: [hum_int][hum_dec][temp_int][temp_dec][checksum]
 *   4. Cada bit: LOW 50µs + HIGH 26-28µs (bit 0) o HIGH 70µs (bit 1)
 * 
 * @return temperatura en décimas de °C (245 = 24.5°C)
 *         o TEMP_ERROR_VALUE si falla la lectura
 */
static bool leerDHT11_raw(uint8_t data[5])
{
    /* --- 1. Activar alimentación del DHT11 --- */
    digitalWrite(DHT11_POWER_PIN, HIGH);
    delay(DHT11_STABILIZE_MS);              /* Esperar estabilización del sensor */

    /* --- 2. Enviar señal de START --- */
    pinMode(DHT11_DATA_PIN, OUTPUT);
    digitalWrite(DHT11_DATA_PIN, LOW);      /* Pull low > 18ms */
    delay(20);
    digitalWrite(DHT11_DATA_PIN, HIGH);     /* Pull high 40µs */
    delayMicroseconds(40);
    pinMode(DHT11_DATA_PIN, INPUT);         /* Cambiar a lectura */
    
    /* --- 3. Leer ACK del DHT11 (LOW 80µs + HIGH 80µs) --- */
    if (!dht11WaitFor(LOW, DHT11_TIMEOUT_US))  return false;
    if (!dht11WaitFor(HIGH, DHT11_TIMEOUT_US)) return false;
    
    /* --- 4. Leer 5 bytes (40 bits) de datos --- */
    for (uint8_t i = 0; i < 5; i++) {
        if (!dht11ReadByte(&data[i])) return false;
    }
    
    return true;
}

int16_t leerDHT11(void)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};
    
    if (!leerDHT11_raw(data)) {
        /* Error: timeout o DHT11 no responde */
        digitalWrite(DHT11_POWER_PIN, LOW);
        pinMode(DHT11_DATA_PIN, INPUT);
#if SERIAL_DEBUG
        Serial.println(F("ERROR: DHT11 timeout o no responde"));
#endif
        return TEMP_ERROR_VALUE;
    }
    
    /* --- Apagar DHT11 inmediatamente (ahorro de energía) --- */
    digitalWrite(DHT11_POWER_PIN, LOW);
    pinMode(DHT11_DATA_PIN, INPUT);
    
    /* --- Verificar checksum --- */
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
#if SERIAL_DEBUG
        Serial.print(F("DHT11 checksum error: calc=0x"));
        Serial.print(checksum, HEX);
        Serial.print(F(" recv=0x"));
        Serial.println(data[4], HEX);
#endif
        return TEMP_ERROR_VALUE;
    }
    
    /* --- Extraer temperatura en décimas de °C --- */
    int16_t tempDecimas = ((int16_t)data[2]) * 10 + data[3];
    
#if SERIAL_DEBUG
    Serial.print(F("DHT11 OK: "));
    Serial.print(data[2]);
    Serial.print(F("."));
    Serial.print(data[3]);
    Serial.print(F(" C ("));
    Serial.print(tempDecimas);
    Serial.println(F(" decimas)"));
#endif
    
    return tempDecimas;
}

/**
 * Lee un byte (8 bits) del DHT11.
 * Cada bit: flanco LOW 50µs + pulso HIGH de 26-28µs (bit 0) o 70µs (bit 1).
 * Muestreamos a los 40µs del flanco HIGH para distinguir 0/1.
 */
bool dht11ReadByte(uint8_t *data)
{
    *data = 0;
    
    for (uint8_t i = 0; i < 8; i++) {
        /* Esperar flanco LOW (inicio de cada bit) */
        if (!dht11WaitFor(LOW, DHT11_TIMEOUT_US)) return false;
        
        /* Esperar flanco HIGH (inicio del pulso de datos) */
        if (!dht11WaitFor(HIGH, DHT11_TIMEOUT_US)) return false;
        
        /* Muestrear a los 40µs: si sigue HIGH → bit 1, si LOW → bit 0 */
        delayMicroseconds(40);
        *data <<= 1;
        if (digitalRead(DHT11_DATA_PIN) == HIGH) {
            *data |= 1;
        }
        
        /* Esperar que termine el pulso HIGH (evitar solapamiento con siguiente bit) */
        if (!dht11WaitFor(LOW, DHT11_TIMEOUT_US)) {
            /* Si ya está LOW, continuamos normalmente */
        }
    }
    
    return true;
}

/**
 * Espera a que el pin alcance el estado esperado, con timeout en µs.
 * @return true si alcanzó el estado, false si timeout.
 */
bool dht11WaitFor(uint8_t expected, uint32_t timeout_us)
{
    uint32_t start = micros();
    
    while (digitalRead(DHT11_DATA_PIN) != expected) {
        if ((micros() - start) > timeout_us) {
            return false;
        }
    }
    
    return true;
}

/* ============================================================
 * GENERACIÓN DE NÚMERO ALEATORIO
 * ============================================================ */

/**
 * Genera un número aleatorio de 16 bits.
 * La semilla se inicializó en setup(); random() avanza automáticamente.
 * No se re-semilla en cada ciclo para no reiniciar la secuencia PRNG.
 */
uint16_t generarRandom(void)
{
    return (uint16_t)random(65536);
}
