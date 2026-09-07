/*
 * PROYECTO: Grabadora de Voz WebSocket con TFT
 *
 * CONEXIONADO FINAL - ESP32-S3 N16R8
 * -----------------------------------------------------------
 * * [MICRÓFONO INMP441 (I2S)]
 * - VCC  -> 3.3V
 * - GND  -> GND
 * - L/R  -> GND (Canal Izquierdo)
 * - SCK  -> GPIO 47 (BCLK)
 * - WS   -> GPIO 48 (LRC)
 * - SD   -> GPIO 21 (DIN)
 * * [PANTALLA TFT ST7789 (240x280)]
 * - VCC  -> 3.3V
 * - GND  -> GND
 * - SCL  -> GPIO 12 (Reloj SPI / SCK) | Bus FSPI
 * - SDA  -> GPIO 11 (Datos SPI / MOSI) | Bus FSPI
 * - RES  -> GPIO 14 | Seguro para S3
 * - DC   -> GPIO 9 | Seguro para S3
 * - CS   -> 10 (Chip Select, aunque solo hay 1 dispositivo conectado al bus SPI)
 * - BLK  -> 3.3V (Brillo fijo) o GPIO 4
 * * [BOTÓN EXTERNO]
 * - PIN  -> GPIO 42 (Conectado a GND al pulsar)
 * - Configurado con INPUT_PULLUP interna.
 * -----------------------------------------------------------
 * * NOTA: 
 *  Arduino IDE -> PSRAM: "OPI PSRAM" | Flash: "16MB" | USB CDC On Boot: "Disabled" (grabamos por USB-UART)
 * -----------------------------------------------------------
 * * Install:
 *  Adafruit ST7789 Library
 *  Adafruit GFX Library
 *  Adafruit BusIO.
 * -----------------------------------------------------------
 * * NOTA: Dibujar en el display
 *  Usamos el script image_to_RGB565_bitmap.py para convertir imágenes arbitrarias a bitmaps (byte arrays); podemos dibujar
 *  en Paint o en cualquier herramienta, o subir cualquier tipo de imagen, para mostrarla en nuestro display. Se recomienda dejar
 *  ya las imágenes preparadas al tamaño 240x280 o una resolución que sea un múltiplo, pues el script hace resize a este tamaño 
 *  y puede deformarla.
 *  En este ESP32 tenemos 16 MB de memoria flash; una imagen puede RGB565 a 240x280 (o 280x240) puede ocupar 240x280x16/8/1024 = 131,25 KB en el ESP32
 *  (aunque pueda ocupar más en el PC, donde cada píxel ocupa unos 8 bytes, mientras que en el ESP32 cada píxel volverá a ocupar 2 bytes
 *  [5 para el rojo, 6 para el verde y 5 para el azul]).
 *
 * Referencias:
 *  - https://www.instructables.com/Convert-and-Display-Color-Images-on-an-Arduino-TFT/
 *  - https://m.media-amazon.com/images/I/71xah1Uo8XL._AC_UF1000,1000_QL80_.jpg
 *
 * CONFIGURACIÓN en Arduino IDE:
 *  Board: ESP32S3 Dev Module
 *  Port: (el COM que te aparezca, ej. COM4)
 *  CPU Frequency: **240MHz (WiFi/BT)**
 *  Core Debug Level: **None**
 *  Events Run On: **Core 1**
 *  Arduino Runs On: **Core 1**
 *  Flash Size: **16MB (128Mb)**
 *  Partition Scheme: **Huge APP (3MB No OTA / 1MB SPIFFS)**
 *  PSRAM: **OPI PSRAM (Enabled)**
 *  Flash Mode: **QIO**
 *  Flash Frequency: **80MHz**
 *  Upload Speed: **921600**
 *  USB CDC On Boot: **Disabled** *(como ya usas USB-UART)*
 *  Erase All Flash Before Upload: **Disabled** *(puedes poner Enabled si tienes errores raros)*
 *  JTAG Adapter: **Disabled**
 *  Zigbee Mode: **Disabled**
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebSocketsClient.h> 
#include <driver/i2s.h>

// --- INCLUIR IMÁGENES AQUÍ ---
// Tras haberlas pasado por el script image_to_RGB565_bitmap.py
#include "espera_img.h"    // Debe contener el array espera_map
#include "grabando_img.h"  // Debe contener el array grabando_map
#include "proceso_img.h"   // Debe contener el array proceso_map
#include "enviando_img.h"  // Debe contener el array enviando_map
#include "iniciando_img.h"    // Debe contener el array iniciando_map

// --- CONFIGURACIÓN PINES ---
#define TFT_ST7789_240x280
#define TFT_RST       14
#define TFT_DC        9
#define TFT_MOSI      11
#define TFT_SCLK      12
#define TFT_CS        10
// BLK a 3V3

#define PIN_BUTTON    42
#define I2S_MIC_PORT  I2S_NUM_0
#define MIC_I2S_SCK   47
#define MIC_I2S_WS    48
#define MIC_I2S_SD    21

// --- COLORES CTIC RGB565 ---
#define COLOR_NARANJA 0xF484  // #F29123
#define COLOR_GRIS    0x52AA  // #535355

// --- PARÁMETROS RED ---
const char* ssid = "FiWi";
const char* password = "pass";
//const char* ssid = "sercommBB1415_EXT";
//const char* password = "pass";
//const char* ws_host = "172.16.102.82"; // IP del server de virgil
//const char* ws_host = "192.168.0.102"; 
const char* ws_host = "192.168.4.184"; 
//const char* endpoint = "/";
const char* endpoint = "/ws/chat-audio?user_id=180d8408-f244-4176-93da-d236e86a974e";
//const int ws_port = 5005; // puerto para script de pruebas
const int ws_port = 58000; // backend de Virgil (8000, 58000...)

// --- PARÁMETROS AUDIO ---
const int SAMPLE_RATE = 16000;
const int MAX_RECORD_TIME = 10; 
const int BUFFER_SIZE = SAMPLE_RATE * MAX_RECORD_TIME;

// --- GLOBALES ---
// Inicializar usando SPI por Hardware (más rápido y estable)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WebSocketsClient webSocket;
int16_t *audio_buffer = NULL;
size_t recorded_samples = 0;
bool is_recording = false;
bool is_connected = false;

void updateDisplay(char state) {
    // 1. Dibujar el fondo (Imagen Full Color)
    switch(state) {
        case 'A': tft.drawRGBBitmap(0, 0, espera_map, 280, 240); break;
        case 'B': tft.drawRGBBitmap(0, 0, grabando_map, 280, 240); break;
        case 'C': tft.drawRGBBitmap(0, 0, proceso_map, 280, 240); break;
        case 'D': tft.drawRGBBitmap(0, 0, enviando_map, 280, 240); break;
        case 'E': tft.drawRGBBitmap(0, 0, iniciando_map, 280, 240); break;
    }

    // 2. Configurar color de texto según el estado
    uint16_t colorTexto;
    if (state == 'A' || state == 'B') {
        colorTexto = COLOR_NARANJA; // Estados A y B en Naranja
    } else {
        colorTexto = COLOR_GRIS;    // Estados C, D y E en Gris
    }

    // 3. Dibujar el texto en la parte superior
    tft.setTextSize(3);
    
    // Opcional: Dibujar una pequeña sombra negra para asegurar legibilidad
    tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(22, 22); 
    
    // Imprimir el mensaje correspondiente
    String msg = "";
    if(state == 'A') msg = is_connected ? "CONECTADO" : "DESCONECTADO";
    else if(state == 'B') msg = "GRABANDO...";
    else if(state == 'C') msg = "PROCESANDO";
    else if(state == 'D') msg = "ENVIANDO...";
    else if(state == 'E') msg = "INICIANDO...";
    
    tft.print(msg);

    // Dibujar el texto principal encima
    tft.setTextColor(colorTexto);
    tft.setCursor(20, 20);
    tft.print(msg);
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            is_connected = false;
            updateDisplay('A');
            break;
        case WStype_CONNECTED:
            is_connected = true;
            updateDisplay('A');
            break;
        default: break;
    }
}

void setup() {
    Serial.begin(115200);
    
    // 1. Inicializar TFT (ST7789V2 240x280)
    // Forzamos los pines SPI en el S3 antes de tft.init
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1); 
    tft.init(240, 280);
    tft.setRotation(3);
    tft.fillScreen(COLOR_GRIS); // Limpiar la pantalla con el color de fondo al inicio:

    updateDisplay('E'); // Pantalla de inicio
    
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // 2. Inicializar PSRAM (Obligatorio para N16R8)
    if (!psramInit()) {
        Serial.println("❌ Error PSRAM");
        while(1);
    }
    audio_buffer = (int16_t*) ps_malloc(BUFFER_SIZE * sizeof(int16_t));
    
    // 3. Configuración I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024
    };
    i2s_pin_config_t pins = {
        .bck_io_num = MIC_I2S_SCK, 
        .ws_io_num = MIC_I2S_WS, 
        .data_out_num = -1, 
        .data_in_num = MIC_I2S_SD
    };
    i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pins);

    // 4. WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    
    // 5. WebSocket
    webSocket.begin(ws_host, ws_port, endpoint);
    webSocket.onEvent(webSocketEvent);
    
    updateDisplay('A'); // Estado de espera
}

void loop() {
    webSocket.loop();
    int btn = digitalRead(PIN_BUTTON);

    if (btn == LOW && !is_recording) {
        is_recording = true;
        recorded_samples = 0;
        updateDisplay('B');
    } 
    else if (btn == HIGH && is_recording) {
        is_recording = false;
        updateDisplay('C');
        delay(200);
        
        if (is_connected && recorded_samples > 0) {
            updateDisplay('D');
            webSocket.sendBIN((uint8_t*)audio_buffer, recorded_samples * 2);
        }
        delay(500);
        updateDisplay('A');
    }

    if (is_recording && recorded_samples < BUFFER_SIZE) {
        size_t bytes_read = 0;
        i2s_read(I2S_MIC_PORT, &audio_buffer[recorded_samples], 1024, &bytes_read, portMAX_DELAY);
        int samples_just_read = bytes_read / 2;
        for (int i = 0; i < samples_just_read; i++) {
            audio_buffer[recorded_samples + i] <<= 2; // Ganancia
        }
        recorded_samples += samples_just_read;
    }
}
