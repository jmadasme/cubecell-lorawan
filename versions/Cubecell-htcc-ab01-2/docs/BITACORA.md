# Bitácora de desarrollo — Cubecell-htcc-ab01-2

Registro de problemas encontrados, decisiones técnicas, enfoques descartados y lecciones aprendidas durante el desarrollo del nodo LoRaWAN `Cubecell-htcc-ab01-2` (Heltec CubeCell HTCC-AB01, RadioLib 7.7.1, AU915 SB2, gateway Milesight UG56).

---

## Índice

1. [Resumen del proyecto](#1-resumen-del-proyecto)
2. [Problemas encontrados y soluciones](#2-problemas-encontrados-y-soluciones)
3. [Enfoques descartados y por qué](#3-enfoques-descartados-y-por-qué)
4. [Decisiones técnicas y su justificación](#4-decisiones-técnicas-y-su-justificación)
5. [Estado operativo al cierre de esta versión](#5-estado-operativo-al-cierre-de-esta-versión)
6. [Lecciones aprendidas](#6-lecciones-aprendidas)
7. [Pendientes y extensiones futuras](#7-pendientes-y-extensiones-futuras)

---

## 1. Resumen del proyecto

**Objetivo**: nodo LoRaWAN mínimo sobre HTCC-AB01 que:

1. Se una por OTAA contra el Network Server embebido del gateway Milesight UG56.
2. Envíe periódicamente un payload pequeño (versión + número aleatorio de 32 bits).
3. Acepte downlinks de configuración (SF, intervalo, consulta RSSI) con whitelist estricta.
4. Sobreviva power-cycles **sin re-join** y **sin rechazos `-1116` por DevNonce repetido**.
5. Consuma lo mínimo entre ciclos (deep sleep real).

**Resultado**: firmware v1.0 completo, auditado (0 incidencias), compilado (89.364 B, 68% flash), grabado y operativo. Join pendiente del registro del device en el UG56 (tarea operativa del usuario, no del firmware).

---

## 2. Problemas encontrados y soluciones

### 2.1 `-1116 (NO_JOIN_ACCEPT)` — DevNonce repetido tras power-cycle

**Síntoma (fase temprana)**: cada reinicio del nodo implicaba un join nuevo; tras varios ciclos, el Network Server empezó a rechazar joins con `-1116`. Un nodo que se alimenta por batería/solar se reinicia a menudo, así que el problema era inaceptable.

**Causa raíz**: el stack LoRaWAN exige que cada JoinRequest use un **DevNonce nuevo** (anti-replay). Si el nodo no persiste los nonces usados, tras un corte repite valores ya vistos por el NS y este descarta el join.

**Solución implementada** (contrato de RadioLib 7.7.1):

1. `saveNoncesToEeprom()` se ejecuta **antes/inmediatamente después de cada intento de join** (éxito o fallo):
   - En `setup()`: si `activateOTAA()` falla → guardar nonces.
   - En `ensureJoined()` (`loop()`): guardar nonces **siempre** tras el intento.
2. `loadSession()` restaura **nonces + sesión completa** en el arranque. Orden obligatorio:
   `beginOTAA()` → `loadSession()` → `activateOTAA()`.
3. Tras un join exitoso, `saveSessionToEeprom()` persiste la sesión; después, `saveSessionThrottled()` la refresca cada 32 uplinks.

**Resultado**: power-cycle = arranque en segundos con `SESSION_RESTORED` (sin join), y cuando el join falla (NS aún sin credenciales), los nonces avanzan correctamente en EEPROM y no se repiten.

### 2.2 `setSleepFunction()` no era la herramienta correcta

**Síntoma**: la API `node.setSleepFunction()` de RadioLib parecía "el" mecanismo para dormir el nodo durante el ciclo LoRaWAN, pero su semántica no encajaba: tras configurarla, el deep sleep de la aplicación no mejoraba y el comportamiento era confuso.

**Causa**: `setSleepFunction(cb)` es para que el **stack interno** de RadioLib duerma la radio en sus propias esperas (umbral `RADIOLIB_LORAWAN_DELAY_SLEEP_THRESHOLD`, p. ej. retardos entre comandos MAC). No es un mecanismo de deep sleep de aplicación. Además, en la familia CubeCell core la callback esperada no está disponible de forma natural.

**Solución**: eliminarla del diseño. El deep sleep de aplicación se implementa con:

- `scanGuard = 200` (padding de la ventana RX para no perder downlinks),
- `TimerEvent_t` programado a N ms,
- bucle de `lowPowerHandler()` (core CubeCell) hasta expirar el timer,
- `radio.sleep()` antes y `radio.standby()` al despertar.

### 2.3 El monitor serie `arduino-cli monitor` sale al instante

**Síntoma**: lanzado desde scripts/agentes, `arduino-cli monitor -p /dev/ttyUSB0` terminaba inmediatamente sin mostrar nada.

**Causa**: sin TTY interactivo (stdin no es terminal) el monitor sale.

**Workaround documentado y verificado**:

```bash
timeout 30 sh -c 'sleep 60 | arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200'
```

`sleep 60` alimenta stdin durante 60 s; `timeout 30` acota la captura. Baudrate 115200.

### 2.4 Detección del puerto y del chip en la grabación

**Síntoma**: dudas iniciales sobre el puerto y el chip al grabar.

**Resolución**: `arduino-cli board list` → `/dev/ttyUSB0` (puente CP2102 integrado). El upload reporta `Silicon ID 0x256a11b5` = **ASR6502**, lo que confirma que la placa es la HTCC-AB01 y que la grabación fue correcta (14,67 s).

### 2.5 FQBN único para la AB01 (no existe "V2")

**Síntoma**: búsqueda infructuosa de una variante "V2" del board para revisiones recientes de la HTCC-AB01.

**Resolución**: en el core CubeCell 1.4.0 **no existe** variante V2. El FQBN único es `CubeCell:CubeCell:CubeCell-Board` para toda la familia AB01. Verificado en `boards.txt` del core.

### 2.6 Join rechazado en campo: `-1116` como estado operativo (no bug)

**Síntoma**: con el firmware final y todo verificado, el nodo loguea `[JOIN] Fallo: -1116 (NO_JOIN_ACCEPT)` cada ~12 s.

**Diagnóstico**: el gateway UG56 aún **no tiene registradas las credenciales del nodo** (DevEUI/JoinEUI/AppKey). El nodo transmite JoinRequests perfectamente; el NS los descarta por DevEUI desconocido.

**Resolución**: es un pendiente **operativo** del usuario (registrar el device en el UG56). Documentado como causa nº 1 en la sección de troubleshooting del README. Cuando el registro exista, el siguiente intento (≤12 s) será aceptado sin tocar el firmware.

**Sub-caso a vigilar**: el UG56 **auto-asigna JoinEUI** (p. ej. `24E124C0002ADD01`). Si el firmware quedó con un JoinEUI distinto, tras registrar habrá que **copiar el JoinEUI del NS a `credentials.h` y re-flashear** (RadioLib valida que el join-accept venga para el JoinEUI configurado).

### 2.7 Hallazgo de documentación: polaridad de Vext en la AB01 (`LOW` = ON)

**Síntoma**: al redactar la documentación se detectó que el borrador describía `digitalWrite(Vext, LOW)` como "apagado" (heredado de una suposición inicial del PLAN).

**Verificación (fuentes oficiales del core 1.4.0)**:

- `Factory_Test_AB01.ino` — el test de fábrica de la propia AB01 — ejecuta `pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);` **antes** de `display.init()`: `LOW` energiza el rail.
- `LoRaWan_APP.cpp` enciende la OLED de la variante BoardPlus (AB02) con `digitalWrite(Vext, LOW)` y la apaga con `HIGH`.
- Todos los ejemplos de sensores del core alimentan el sensor con `digitalWrite(Vext, LOW)` (comentario `//set vext to high`) y lo cortan con `HIGH`.

**Conclusión**: en la familia CubeCell la lógica de Vext es **activa-baja (`LOW` = ON, `HIGH` = OFF)**; la **HTCC-AB02A es la excepción** (`HIGH` = ON, verificado en campo en el proyecto hermano `HTCC_AB02A_AMG8833_Thermal_v2`). En este nodo v1.0 el firmware escribe `LOW`, así que el rail Vext queda **encendido** tras el arranque; sin periféricos conectados el efecto es inocuo. La guía de extensión AMG8833 (§7) asume ya la polaridad correcta: `LOW` para energizar y `HIGH` antes del deep sleep.

**Acción**: corregidos README (§4.2/§4.3), `developer.md` (Apéndice A) y esta bitácora. Lección nº 13 (§6).

---

## 3. Enfoques descartados y por qué

| Enfoque | Descartado porque |
|---|---|
| **ABP** (Activation By Personalization) | El proyecto exige OTAA por seguridad y rotación de sesión. ABP evita joins pero complica el manejo de frame counters en el NS y no permite el ciclo de vida limpio DevNonce/sesión que queremos demostrar. |
| **`setSleepFunction()` de RadioLib** | Semántica equivocada (esperas internas del stack, no deep sleep de app). Ver §2.2. |
| **Guardar la sesión completa en EEPROM en cada uplink** | Desgaste de ciclos de escritura de la EEPROM emulada. Sustituido por *throttling* cada 32 uplinks (`SESS_SAVE_EVERY_UPLINKS`). |
| **No persistir nada** (join en cada boot) | Genera DevNonces repetidos → rechazos `-1116`; además gasta energía y tiempo de aire. Descartado tras el problema §2.1. |
| **Guardar solo la sesión (no los nonces)** | Cubre el re-join tras boot, pero **no** el caso "el NS aún no tiene el device": los nonces quedarían repitiéndose entre power-cycles. Por eso se persisten los nonces *en cada intento de join*, no solo en el éxito. |
| **Downlink confirmado (con ACK)** | Dobla el costo energético por comando. El NS reintenta si hace falta; Clase A sin confirmaciones es el patrón correcto para telemetría periódica. |
| **Confiar en el SF por defecto de RadioLib** | Sin `applyDrFromConfig()` tras cada arranque/rejoin, el DR quedaba fuera de la política 7..9. Se centralizó en `gCfg.sf` + `drFromSf()`. |
| **Payload con sensor desde v1** (AMG8833, batería) | Decisión explícita del usuario: esta primera versión solo lleva el número aleatorio, para validar el ciclo completo join/uplink/downlink/persistencia con un payload mínimo trazable. Los sensores son extensiones (§7). |
| **T-Beam como plataforma** | La HTCC-AB01 está optimizada para bajo consumo con radio integrada; el T-Beam (ESP32) es otra familia (más rápida pero mucho más hambrienta). Se mantiene el soporte T-Beam como referencia de portabilidad. |
| **Copiar el decoder con `let/const/arrow` (ES6)** | Los motores JS embebidos de los Network Servers (UG56/ChirpStack) pueden no soportar ES6. `decoder.js` se escribió en **ES5 puro**. |

---

## 4. Decisiones técnicas y su justificación

### 4.1 Persistencia total de sesión y nonces en EEPROM

**Decisión**: sesión LoRaWAN (DevAddr, claves, counters, 302 B) + nonces (14 B) persistidos en EEPROM; layout `0x040` bloque de sesión; escritura de sesión *throttled* cada 32 uplinks.

**Por qué**:

- Ahorra energía (sin join en cada arranque).
- Evita `-1116` por DevNonce repetido (ver §2.1).
- La EEPROM emulada del ASR6502 tiene ciclos limitados → el throttling es obligatorio.

**Detalle fino**: `loadSession()` **debe** ir después de `beginOTAA()` y antes de `activateOTAA()` (requisito de RadioLib 7.7.1; `setBufferSession()` deja el estado `SESSION_PENDING` que `activateOTAA()` resuelve como `SESSION_RESTORED`). El orden está comentado en el código y cubierto por la auditoría.

### 4.2 `scanGuard = 200` ms

**Decisión**: elevar `node.scanGuard` del default (10) a 200.

**Por qué**: la ventana de escucha RX en Clase A puede perderse con gateways cuyo forwarding tiene latencia (UG56 con NS embebido). 200 ms de padding garantiza recibir el downlink. Costo energético despreciable (~200 ms de RX extra por ciclo).

### 4.3 Deep sleep con `TimerEvent_t` + `lowPowerHandler()`

**Decisión**: ciclo de sueño implementado con el timer del core CubeCell y el handler de bajo consumo, sin `sleep()` basado en RTOS ni busy-wait.

**Por qué**: es el mecanismo canónico de la familia ASR650x, combinable con `radio.sleep()`; tras expirar el timer el chip se reinicia y `setup()` re-ejecuta todo el ritual de restauración. Consumo medido coherente con µA en reposo.

### 4.4 Payload v1 mínimo con versión + random

**Decisión**: 5 bytes `[0x01 | uint32 big-endian]` con un número aleatorio; sin sensor ni batería.

**Por qué**: decisión explícita del usuario para la primera versión. Permite validar end-to-end (join, uplink, downlink, decoder, persistencia) con una señal trivial de trazar en el NS. El byte de versión permite evolucionar el formato sin romper el decoder.

### 4.5 PRNG sembrado con `micros()/millis()` + ADC

**Decisión**: `randomSeed(micros() ^ ((millis()) << 16) ^ (analogRead(ADC) << 8) ^ analogRead(ADC))`.

**Por qué**: sin entropía de verdad (no hay TRNG accesible fácilmente), la combinación de dos relojes independientes + ruido del ADC (pin flotante/divisor de batería) da variación suficiente para un identificador de traza. **No** es apto para criptografía y no se usa como tal.

### 4.6 Downlink whitelist estricta, sin confirmación

**Decisión**: solo `0x01` (SF 7..9), `0x02` (intervalo 1..1440, 2 bytes big-endian) y `0x03` (consulta RSSI) en el puerto 1; sin ACK; cualquier otra cosa se ignora con log.

**Por qué**:

- Superficie de ataque mínima: un downlink malicioso o corrupto no puede activar nada fuera del whitelist.
- Validación de rango doble (longitud + valor) para evitar estados imposibles.
- Sin confirmación para no gastar energía; el operador puede reenviar desde el NS.

### 4.7 RSSI capturado antes de dormir la radio

**Decisión**: `gLastRssi = radio.getRSSI()` **inmediatamente** tras `sendReceive()`, antes de `radio.sleep()`.

**Por qué**: el RSSI de un downlink solo es representativo justo tras recibirlo; tras apagar la radio el registro ya no es válido. Se usa para el comando `0x03`.

### 4.8 Config separada de sesión en EEPROM (bloques con magic bytes)

**Decisión**: `NodeConfig` en `0x000` (magic `0xC0 0xC1`) y bloque de sesión en `0x040` (magic `0x5A 0x5B`), con `#error` de compilación si no cabe en 512 B.

**Por qué**:

- Independencia de ciclos de vida: la config cambia por downlink; la sesión por join. Invalidar una no debe tocar la otra.
- Magic bytes = versionado barato: cambiar el layout futuro solo requiere incrementarlos.
- El `#error` estático protege el layout contra cambios accidentales de tamaño en RadioLib (los `*_BUF_SIZE` podrían cambiar entre versiones).

### 4.9 Un único FQBN (no existe V2)

**Decisión**: documentar `CubeCell:CubeCell:CubeCell-Board` como el único FQBN para la AB01 en core 1.4.0.

**Por qué**: verificado en `boards.txt` del core: no hay variante V2. Los tutoriales que la mencionan aplican a otros cores o confunden con la AB02.

### 4.10 Fechoría del JoinEUI auto-asignado en el UG56

**Decisión**: documentar como paso operativo crítico que el JoinEUI que muestre el formulario del UG56 debe copiarse al firmware si difiere.

**Por qué**: RadioLib descarta join-accepts para un JoinEUI distinto; es una causa silenciosa de `-1116` que persiste aunque las demás credenciales estén bien.

---

## 5. Estado operativo al cierre de esta versión

| Ítem | Estado |
|---|---|
| Compilación | ✅ 89.364 B (68% flash), RAM ~8% |
| Auditoría estática | ✅ 0 incidencias |
| Grabación | ✅ `/dev/ttyUSB0`, 14,67 s, Silicon ID `0x256a11b5` (ASR6502) |
| Radio | ✅ `radio.begin()` OK |
| Join OTAA | ⏳ Rechazado `-1116` — **pendiente registrar el device en el UG56** |
| Uplink/downlink | ⏳ A la espera del join (lógica verificada en revisión) |
| Persistencia | ✅ Verificada por diseño + auditoría (nonces/sesión en EEPROM) |

El firmware reintenta el join cada ~12 s (10 s de deep sleep + ~2 s de intento) y está listo para entrar en producción en cuanto el UG56 tenga el device registrado.

---

## 6. Lecciones aprendidas

1. **La persistencia de nonces no es opcional** en cualquier nodo OTAA que se reinicie (batería, watchdog, deep sleep largo). Es la diferencia entre un nodo que funciona meses y uno que se "desune" solo. Aplicable a **todos** los proyectos LoRaWAN futuros.
2. **El orden de inicialización de RadioLib importa**: `beginOTAA()` → `loadSession()` → `activateOTAA()`. Un orden incorrecto no da error de compilación; simplemente el restore no ocurre. Documentar y comentar estos contratos en el código.
3. **`setSleepFunction()` ≠ deep sleep**. Leer la documentación de la versión exacta de la librería antes de asumir semánticas. La API era de RadioLib pero para otro propósito.
4. **El default de `scanGuard` (10 ms) es corto** para algunos gateways/NS. 200 ms es un buen valor de partida con NS embebidos.
5. **Los Network Servers asisten con JoinEUI discrecional**: el UG56 auto-asigna JoinEUI. El firmware debe adaptarse al NS (copiar y re-flashear), no al revés. Verificar SIEMPRE el JoinEUI del formulario.
6. **`-1116` tiene tres causas típicas** (en orden de frecuencia): device no registrado, JoinEUI distinto (auto-asignado), sub-banda del gateway distinta. Un log claro (`stateDecode`) ahorra horas de depuración.
7. **El monitoreo serie en CI/agentes necesita el workaround del pipe** (`sh -c 'sleep 60 | arduino-cli monitor ...'`). Sin TTY, el monitor no funciona.
8. **La EEPROM emulada se desgasta**: toda escritura periódica debe ser *throttled* (32 uplinks es un buen orden de magnitud) y agrupada por bloques con magic.
9. **Whitelist en downlink desde el día uno**: en Clase A el canal de bajada es "sin autenticación a nivel aplicación"; validar longitud y rango antes de ejecutar cualquier cosa es barato y elimina clases enteras de fallos.
10. **Los decoders deben ser ES5** para máxima compatibilidad con los motores JS de los NS embebidos (UG56, ChirpStack). Evitar `let/const/arrow` por defecto.
11. **Documentar el estado de campo en el propio README** (el caso `-1116` pendiente de registro) evita que el próximo operador lo interprete como bug del firmware.
12. **Un byte de versión en el payload** cuesta 1 B y evita romper decoders al evolucionar el formato (lección aplicada de proyectos previos con AMG8833).
13. **La polaridad de los pines de control varía por variante de placa**: en la familia CubeCell el rail `Vext` es activo-bajo (`LOW` = ON) en AB01/AB02, pero la HTCC-AB02A lo invierte (`HIGH` = ON). No heredar la polaridad de un proyecto hermano sin verificarla contra el factory test y los ejemplos oficiales de **esa** variante.

---

## 7. Pendientes y extensiones futuras

| Extensión | Notas |
|---|---|
| **Sensor térmico AMG8833** (I²C) | Proyecto hermano `HTCC_AB02A_AMG8833_Thermal_v2` como referencia. SDA=P0_1, SCL=P0_0, alimentado por Vext (polaridad AB01: `LOW`=ON, `HIGH`=OFF; la AB02A del proyecto hermano usa la inversa). Payload crecería a >5 B → subir `PAYLOAD_VERSION_OK` a `0x02`. |
| **Lectura de batería** | `getBatteryVoltage()` del core (ADC=P2_3, VBAT_ADC_CTL=P3_3). Añadir 2 B big-endian al payload. |
| **Forzar rejoin con USER_KEY** | P3_3 (compartido con VBAT_ADC_CTL — coordinar si se usa batería). Pulsación larga → invalidar sesión EEPROM → rejoin. |
| **Uplink confirmado ocasional** | Para telemetría crítica (p. ej. alarma), añadir un comando/modo que fuerce confirmed=true. |
| **Intervalos adaptativos** | Bajar el intervalo automáticamente si el RSSI cae o si el NS no acusa uplinks. |
| **Registro en producción en el UG56** | **Bloqueante de campo**: registrar DevEUI/AppKey en el NS y verificar JoinEUI del firmware. |

---

*Bitácora mantenida durante el desarrollo del pipeline analista → implementador → auditor → documentador. Versión del firmware: v1.0.*
