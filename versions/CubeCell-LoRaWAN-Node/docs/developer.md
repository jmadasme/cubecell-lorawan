# Developer Guide — CubeCell LoRaWAN Bridge v1.3

## Arquitectura

```
v1.3 se compone de 3 modos operando en simultaneo:

┌─────────────────────────────────────────────────────────────┐
│                      loop()                                 │
│                                                             │
│  ┌──────────────────┐  ┌──────────────┐  ┌───────────────┐ │
│  │ Bridge Serial    │  │ Heartbeat    │  │ Stale Flush   │ │
│  │ (interactivo)    │  │ (cada 30s)   │  │ (buffer >500ms│ │
│  │                  │  │              │  │  sin datos)   │ │
│  │ Lee Serial       │  │ Si           │  │               │ │
│  │ Parsea HEX       │  │ millis()-    │  │ Descarta      │ │
│  │ sendAndReceive() │  │ lastUplink   │  │ buffer parcial│ │
│  │                  │  │ >= 30s:      │  │               │ │
│  │ Muestra resultado│  │ sendHeartbeat│  │               │ │
│  └──────────────────┘  └──────────────┘  └───────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              v
              ┌───────────────────────────┐
              │    sendAndReceive()        │
              │                           │
              │  1. node.sendReceive()     │
              │  2. Si downlink (>0):      │
              │     printDownlink()        │
              │  3. Actualiza lastUplinkMs │
              └───────────────────────────┘
```

## Diagrama de flujo completo

```
                  ┌───────────────┐
                  │   POWER ON    │
                  └───────┬───────┘
                          │
                          v
                  ┌───────────────┐
                  │   setup()     │
                  │ - Serial     │
                  │ - radio.begin│
                  │ - beginOTAA  │
                  │ - scanGuard  │
                  │ - activateOTA│
                  │ - prompt ">" │
                  └───────┬───────┘
                          │
                          v
                  ┌───────────────┐
                  │    loop()     │◄────────────────────┐
                  └───────┬───────┘                     │
                          │                             │
              ┌───────────┴───────────┐                 │
              │                       │                 │
         Serial data              No serial         Tiempo
         disponible                data              >30s
              │                       │                 │
              v                       v                 v
      ┌───────────────┐      ┌──────────────┐  ┌──────────────┐
      │ Acumular en   │      │ Stale check  │  │ sendHeart-   │
      │ serialLine[]  │      │ >500ms?      │  │ beat()       │
      └───────┬───────┘      │ Si→descartar │  └──────┬───────┘
              │              └──────────────┘         │
              v                                       v
      ┌───────────────┐                       ┌──────────────┐
      │ ¿Newline?     │                       │ sendAnd-     │
      │ No→seguir     │                       │ Receive(BB)  │
      │ Si→procesar   │                       └──────┬───────┘
      └───────┬───────┘                              │
              │                                      │
              v                                      v
      ┌───────────────┐                       ┌──────────────┐
      │ parseHexString│                       │ ¿Downlink?   │
      │ ¿Hex valido?  │                       │ Si→printDown-│
      └───────┬───────┘                       │ link()       │
              │                              └──────────────┘
         ┌────┴────┐
         │         │
       Hex OK   Hex invalido
         │         │
         v         v
   ┌──────────┐  ┌───────────────────┐
   │ensure-   │  │"?? Error: hex ..."│
   │Joined()  │  │ prompt "> "       │
   └────┬─────┘  └───────────────────┘
        │
   ┌────┴─────┐
   │ joined   │ no joined
   │          │
   v          v
┌────────┐ ┌─────────────────────┐
│sendAnd- │ │ Rejoin + reintento │
│Receive()│ └─────────────────────┘
└────┬───┘
     │
     v
┌─────────────┐
│ ¿Downlink?  │
│ Si→printDL  │
│ No→seguir   │
└──────┬──────┘
       │
       v
┌─────────────┐
│ prompt "> " │
└──────┬──────┘
       │
       └─────────────────────────────────┘
```

---

## Descripcion de funciones

### `void setup()`

Inicializa serial, radio, nodo LoRaWAN y realiza el join OTAA. Configura workaround y muestra prompt.

### `void loop()`

Funcion principal. Atiende tres procesos:
1. Lectura de serial (modo bridge interactivo)
2. Stale buffer flush (descarta datos residuales)
3. Heartbeat timer (envia BB cada 30s)

### `static void sendHeartbeat(void)`

Envía el heartbeat (`0xBB`) para abrir ventana RX. Si el join se perdió, intenta rejoin.

### `static int sendAndReceive(const uint8_t* data, size_t len)`

Funcion central de envio:
1. Muestra `>> Enviando: XX YY ZZ`
2. Llama a `node.sendReceive()` con buffer de downlink
3. Si hay downlink, llama a `printDownlink()`
4. Actualiza `lastUplinkMs`

### `static void printDownlink(const uint8_t* data, size_t len)`

Formatea el downlink recibido:

```
[DOWNLINK] 68 6F 6C 61  →  "hola"
```

Muestra los bytes en hex y su representacion ASCII (convierte caracteres no imprimibles a `.`).

### `static size_t parseHexString(const char* input, uint8_t* output, size_t maxOut)`

Convierte texto hex (con/sin espacios) a bytes.

### `static bool ensureJoined(void)`

Verifica si la sesion esta activa. Si no, intenta rejoin OTAA.

### `static void radioSleepDelay(RadioLibTime_t ms)`

Workaround: reemplaza delay() por millis() para evitar el bug del timer ASR6502.

---

## Variables globales clave

| Variable | Tipo | Descripcion |
|----------|------|-------------|
| `node` | `LoRaWANNode` | Nodo LoRaWAN |
| `radio` | `SX1262` | Driver del modulo LoRa |
| `txBuffer` | `uint8_t[51]` | Buffer de transmision |
| `serialLine` | `char[256]` | Buffer de entrada serial |
| `lastUplinkMs` | `uint32_t` | Timestamp del ultimo uplink |
| `heartbeatPayload` | `const uint8_t[1]` | `{0xBB}` — payload del heartbeat |
| `HEARTBEAT_MS` | `30000` | Intervalo de heartbeat (30s) |
| `downlinkData` | `uint8_t[51]` | Buffer para datos de downlink |

---

## API RadioLib utilizada

| Funcion | Proposito |
|---------|-----------|
| `radio.begin()` | Inicializa SX1262 |
| `node.beginOTAA()` | Configura credenciales OTAA |
| `node.activateOTAA()` | Ejecuta join |
| `node.isActivated()` | Verifica sesion activa |
| `node.sendReceive(data, len, port, downBuf, downLen, isConfirmed)` | Envia uplink, recibe downlink |
| `node.setSleepFunction()` | Reemplaza delay interno |

---

## Guia de extension

### Cambiar intervalo de heartbeat

```cpp
#define HEARTBEAT_MS 60000UL  // cada 60 segundos
```

### Cambiar payload de heartbeat

```cpp
const uint8_t heartbeatPayload[] = { 0xCA, 0xFE };
const size_t heartbeatLen = 2;
```

### Filtrar downlinks

Para ignorar MAC commands (1 byte) y solo mostrar payloads de usuario:

```cpp
static void printDownlink(const uint8_t* data, size_t len) {
    if (len <= 1) return;  // ignorar MAC commands
    // ... mostrar solo payloads de usuario
}
```

### Procesar downlink automaticamente

Para que la CubeCell ejecute acciones sin esperar al ESP32:

```cpp
static void printDownlink(const uint8_t* data, size_t len) {
    if (len >= 2 && data[0] == 0x01) {
        // Comando: encender GPIO
        digitalWrite(data[1], data[2]);
    }
    // ... mostrar igual en serial
}
```

---

## Estructura de archivos

```
CubeCell-LoRaWAN-Node/
├── CubeCell-LoRaWAN-Node.ino    # Firmware v1.3
├── README.md                     # Documentacion
├── .gitignore
└── docs/
    ├── datasheet.md              # Especificaciones tecnicas
    └── developer.md              # Guia de desarrollo
```
