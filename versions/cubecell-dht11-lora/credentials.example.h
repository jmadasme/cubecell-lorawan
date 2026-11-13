/*
 * credentials.h
 * ==============
 * Credenciales LoRaWAN OTAA para CubeCell HTCC-AB02A
 * 
 * INSTRUCCIONES:
 *   1. Reemplaza los valores placeholder con las credenciales
 *      de tu dispositivo en el Network Server (ChirpStack, TTN, Helium, etc.)
 *   2. Formato: Little-Endian (LSB first)
 *   3. NUNCA versiones este archivo con credenciales reales.
 *      Ya está incluido en .gitignore.
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <stdint.h>

/*
 * Device EUI (8 bytes) — Identificador único del dispositivo.
 * En CubeCell se puede generar automáticamente con LORAWAN_DEVEUI_AUTO.
 * Si no, reemplazar con el DevEUI de tu Network Server.
 */
uint8_t devEui[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Application EUI / JoinEUI (8 bytes)
 * Proporcionado por tu Network Server.
 */
uint8_t appEui[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Application Key (16 bytes)
 * Proporcionado por tu Network Server.
 */
uint8_t appKey[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Claves ABP (solo si se usa ABP en lugar de OTAA)
 */
uint8_t nwkSKey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t appSKey[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint32_t devAddr = (uint32_t)0x00000000;

#endif /* CREDENTIALS_H */
