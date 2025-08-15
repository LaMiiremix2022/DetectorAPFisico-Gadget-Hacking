// WiFi Locator para ESP32 + ILI9341 + XPT2046
// Requiere: Adafruit_ILI9341, Adafruit_GFX, XPT2046_Touchscreen
// Asegúrate de tener Fonts/Org_01.h en la librería Adafruit_GFX/Fonts

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <vector>
#include <Fonts/Org_01.h> // Fuente estilo "hacker" (opcional)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <SD.h>      // Librería SD
#include <SPI.h>     // Librería SPI (necesaria para SD)

// -------- COLORES -------------

#define TFT_BEIGE 0xF7BB
#define TFT_DARKORANGE 0xFC60
#define TFT_ANTIQUEWHITE 0xFF5A

// -------- CONFIGURABLE ----------------
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4

#define TOUCH_CS 21
#define TOUCH_IRQ 22 // opcional

#define SD_CS 5      // Ajusta el número según tu conexión

// Touch calibration (NO TOCAR si ya calibraste)
#define TS_MINX 1070
#define TS_MAXX 3240
#define TS_MINY 610
#define TS_MAXY 710

// Visual layout (puedes ajustar si quieres)
#define ROW_HEIGHT 28
#define TITLE_H 24
#define PADDING 6

// Scanning interval (ms)
#define SCAN_INTERVAL 1500

// EMA smoothing factor (0.0..1.0)
#define EMA_ALPHA 0.35

// RSSI mapping
#define RSSI_MIN -100
#define RSSI_MAX -30

// Desplazamiento general vertical para bajar UI
const int TOP_OFFSET = 20;
const int FOOTER_BOTTOM_GAP = 10; // gap abajo para los botones

// --------------------------------------

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// estructuras y globals
struct Net {
  String ssid;
  String bssid;
  int32_t rssi;
  int8_t channel;
};

std::vector<Net> networks;
SemaphoreHandle_t netMutex;

volatile bool requestRescan = false;

String targetBSSID = "";
String targetSSID = "";
float ema_rssi = NAN;
bool inProximityMode = false;
int scrollOffset = 0;
int selectedRow = 0; // fila actualmente seleccionada (índice absoluto)

// prototipos
void scanTask(void* pvParameters);
void drawWiFiList();
void drawProximityScreen();
void handleTouch();
void mapTouchToScreen(int16_t rawx, int16_t rawy, int &x, int &y);
uint16_t colorFromRatio(float r); // 0..1 -> color

// ---------------- Banner ----------------

void showIntroBanner() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setFont(&Org_01);
  tft.setTextSize(2);

  String line1 = "Creado por d1se0";
  String line2 = "Detector de Wifi cercano";

  int16_t x1, y1;
  uint16_t w1, h1, w2, h2;

  // Medir ancho de los textos
  tft.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  tft.getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);

  int16_t targetX1 = (tft.width() - w1) / 2;
  int16_t targetX2 = (tft.width() - w2) / 2;
  int16_t yPos1 = tft.height() / 2 - 20;
  int16_t yPos2 = tft.height() / 2 + 10;

  int16_t startX = -max(w1, w2); // comienza fuera de pantalla

  // Animación de desplazamiento
  for (int x = startX; x <= targetX1; x += 4) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(x, yPos1);
    tft.setTextColor(ILI9341_CYAN);
    tft.print(line1);

    // Segunda línea se mueve proporcional al avance de la primera
    int secondX = startX + (x - startX) * (float)(targetX2 - startX) / (targetX1 - startX);
    tft.setCursor(secondX, yPos2);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(line2);

    delay(30); // velocidad animación
  }

  // Pequeña pausa antes de iniciar
  delay(800);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  if (!ts.begin()) {
    Serial.println("Touch init failed!");
  } else {
    ts.setRotation(1);
  }

  netMutex = xSemaphoreCreateMutex();

  showIntroBanner();

  // Animación / mensaje inicial simple
  tft.setFont(&Org_01);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(PADDING, TOP_OFFSET);
  tft.print("Escaneando redes");
  // pequeño "loading" con puntos
  for (int i = 0; i < 6; ++i) {
    tft.print(".");
    delay(120);
  }

  // Start scanning task (en core 1)
  xTaskCreatePinnedToCore(scanTask, "scanTask", 4096, NULL, 1, NULL, 1);
}

// ---------------- Loop ----------------
void loop() {
  handleTouch();

  if (!inProximityMode) {
    // draw list occasionally or when updated
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 700) {
      drawWiFiList();
      lastDraw = millis();
    }
  } else {
    drawProximityScreen();
    delay(150); // UI refresh cadence
  }
}

// ------------------ Scan Task ------------------
void scanTask(void* pvParameters) {
  for (;;) {
    if (requestRescan) requestRescan = false;

    int n = WiFi.scanNetworks();
    std::vector<Net> tmp;
    for (int i = 0; i < n; ++i) {
      Net entry;
      entry.ssid = WiFi.SSID(i);
      entry.bssid = WiFi.BSSIDstr(i);
      entry.rssi = WiFi.RSSI(i);
      entry.channel = WiFi.channel(i);
      tmp.push_back(entry);
    }
    if (xSemaphoreTake(netMutex, portMAX_DELAY) == pdTRUE) {
      networks = tmp;
      // ajustar selectedRow/scrollOffset si se redujo la lista
      if (selectedRow >= (int)networks.size()) selectedRow = max(0, (int)networks.size()-1);
      if (scrollOffset > selectedRow) scrollOffset = selectedRow;
      xSemaphoreGive(netMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL));
  }
}

// ------------------ Dibujar iconos en esquina superior derecha ------------------
void drawStatusIcons() {
    int x = tft.width() - 6; 
    int y = TOP_OFFSET;

    // --- Icono batería decorativo ---
    int batW = 20, batH = 10;
    x -= batW;
    tft.drawRoundRect(x, y, batW, batH, 3, ILI9341_WHITE);   // borde redondeado
    tft.fillRoundRect(x + 1, y + 1, batW - 2, batH - 2, 2, ILI9341_GREEN); // relleno verde fijo
    tft.fillRect(x + batW, y + 3, 2, batH - 6, ILI9341_WHITE); // polo batería

    x -= 8;

    // --- Icono SD decorativo ---
    int sdW = 14, sdH = 16;
    x -= sdW;
    tft.fillRoundRect(x, y, sdW, sdH, 3, ILI9341_BLUE);                 // cuerpo azul fijo
    tft.fillTriangle(x, y, x + 4, y, x, y + 4, ILI9341_BLACK);          // esquina cortada
    tft.drawLine(x + 2, y + 3, x + sdW - 3, y + 3, ILI9341_BLACK);     // ranura
    for (int i = 0; i < 4; i++) {                                       // contactos dorados
        tft.fillRect(x + 2 + (i * 3), y + sdH - 4, 2, 3, ILI9341_YELLOW);
    }
}

// ------------------ Mini señal para cada SSID ------------------
void drawSignalIcon(int baseX, int baseY, int rssi) {
    // 4 barras
    int nBars = map(constrain(rssi, RSSI_MIN, RSSI_MAX), RSSI_MIN, RSSI_MAX, 1, 4);
    int barW = 3, spacing = 2;
    for (int i = 0; i < 4; i++) {
        int h = (i+1) * 4;
        int color = i < nBars ? ILI9341_GREEN : ILI9341_WHITE;
        tft.fillRect(baseX + i*(barW+spacing), baseY + 12 - h, barW, h, color);
    }
}

// ------------------ Dibujar botones redondeados ------------------
void drawRoundedButton(int x, int y, int w, int h, uint16_t color, const char* label) {
    tft.fillRoundRect(x, y, w, h, 8, color);  // radio de 8 px
    tft.drawRoundRect(x, y, w, h, 8, ILI9341_WHITE); // borde blanco
    int16_t tx = x + w/2 - (strlen(label)*6); // centrar texto aprox
    int16_t ty = y + h/2 - 6;
    tft.setCursor(tx, ty);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.print(label);
}

// ------------------ UI Drawing ------------------
void drawWiFiList() {
    if (xSemaphoreTake(netMutex, (TickType_t)10) != pdTRUE) return;

    int n = networks.size();
    tft.setFont(&Org_01);
    tft.fillScreen(ILI9341_BLACK);

    // Título
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(PADDING, TOP_OFFSET);
    tft.print("Selecciona SSID:");
    drawStatusIcons();

    // Footer y filas
    int btnH = 40;
    int btnY = tft.height() - btnH - FOOTER_BOTTOM_GAP;
    int availableHeight = btnY - (TITLE_H + TOP_OFFSET);
    int visibleRows = max(1, availableHeight / ROW_HEIGHT);

    if (selectedRow < 0) selectedRow = 0;
    if (selectedRow >= n) selectedRow = max(0, n-1);
    if (scrollOffset > selectedRow) scrollOffset = selectedRow;
    if (scrollOffset + visibleRows - 1 < selectedRow) scrollOffset = max(0, selectedRow - visibleRows + 1);

    // Filas
    for (int i = 0; i < visibleRows; ++i) {
        int idx = i + scrollOffset;
        if (idx >= n) {
            tft.fillRect(0, TITLE_H + TOP_OFFSET + i*ROW_HEIGHT, tft.width(), ROW_HEIGHT, ILI9341_BLACK);
            continue;
        }
        int y = TITLE_H + TOP_OFFSET + i*ROW_HEIGHT;
        uint16_t bgColor = (idx == selectedRow) ? TFT_DARKORANGE : ILI9341_DARKGREY;
        tft.fillRoundRect(4, y+2, tft.width()-8, ROW_HEIGHT-4, 6, bgColor);

        tft.setCursor(12, y + 8);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(1.5);
        tft.print(networks[idx].ssid);

        tft.setCursor(tft.width() - 110, y + 8);
        tft.print(networks[idx].rssi);
        tft.print(" dBm");

        drawSignalIcon(tft.width() - 60, y, networks[idx].rssi);
    }

    // Footer botones redondeados
    int btnW = tft.width() / 3 - 10;
    drawRoundedButton(5, btnY, btnW, btnH, TFT_ANTIQUEWHITE, "UP");
    drawRoundedButton(btnW+10, btnY, btnW, btnH, TFT_ANTIQUEWHITE, "DOWN");
    drawRoundedButton(2*btnW+15, btnY, btnW, btnH, TFT_DARKORANGE, "OK");

    xSemaphoreGive(netMutex);
}

void drawProximityScreen() {
    int32_t rssi = INT32_MIN;
    if (xSemaphoreTake(netMutex, (TickType_t)10) == pdTRUE) {
        for (auto &e : networks) {
            if (e.bssid == targetBSSID) { rssi = e.rssi; break; }
        }
        xSemaphoreGive(netMutex);
    }

    if (rssi == INT32_MIN) {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(PADDING, TOP_OFFSET + 6);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.print("Objetivo no encontrado");

        drawRoundedButton(10, tft.height()-40, tft.width()-20, 34, ILI9341_BLUE, "Volver");
        return;
    }

    if (isnan(ema_rssi)) ema_rssi = rssi;
    else ema_rssi = EMA_ALPHA * rssi + (1.0 - EMA_ALPHA) * ema_rssi;

    int barMaxW = tft.width() - 40;
    int barW = map((int)round(ema_rssi), RSSI_MIN, RSSI_MAX, 0, barMaxW);
    barW = constrain(barW, 0, barMaxW);

    tft.fillScreen(ILI9341_BLACK);
    tft.setFont(&Org_01);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(PADDING, TOP_OFFSET);
    tft.print("TRACKING SIGNAL");
    tft.setTextSize(1);
    tft.setCursor(PADDING, TOP_OFFSET + 28);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(targetSSID);

    int barX = 20;
    int barY = tft.height() / 2 - 20;
    int barH = 40;

    tft.drawRoundRect(barX-2, barY-2, barMaxW+4, barH+4, 6, ILI9341_DARKGREY);
    tft.fillRoundRect(barX, barY, barMaxW, barH, 6, ILI9341_BLACK);

    for(int i=0; i<barW; i+=8){
        uint16_t col = colorFromRatio((float)i / barMaxW);
        tft.fillRect(barX+i, barY, 4, barH, col);
    }

    static int pulse = 0;
    pulse = (pulse + 1) % 20;
    int peakW = barW - pulse;
    if(peakW > 0) tft.fillRect(barX + peakW, barY, 2, barH, ILI9341_WHITE);

    tft.setTextSize(1);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(barX, barY + barH + 10);
    tft.printf("%d dBm", (int)round(ema_rssi));

    // Botones redondeados
    int btnW = (tft.width() - 30)/2;
    drawRoundedButton(10, tft.height()-40, btnW, 34, ILI9341_DARKGREY, "Volver");
    drawRoundedButton(20 + btnW, tft.height()-40, btnW, 34, ILI9341_DARKGREY, "Rescan");
}

// color gradient: red -> yellow -> green
uint16_t colorFromRatio(float r) {
  r = constrain(r, 0.0f, 1.0f);
  uint8_t red, green;
  if (r < 0.5f) {
    float t = r / 0.5f;
    red = 255;
    green = (uint8_t)(t * 255);
  } else {
    float t = (r - 0.5f) / 0.5f;
    red = (uint8_t)((1 - t) * 255);
    green = 255;
  }
  // RGB888 -> RGB565
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3);
}

// ------------------ Touch & UI interaction ------------------
void handleTouch() {
    if (!ts.touched()) return;

    TS_Point p = ts.getPoint();

    // DEBUG raw
    Serial.print("Raw touch: X=");
    Serial.print(p.x);
    Serial.print(" Y=");
    Serial.println(p.y);

    int x, y;
    mapTouchToScreen(p.x, p.y, x, y);

    // DEBUG mapped
    Serial.print("Mapped touch: X=");
    Serial.print(x);
    Serial.print(" Y=");
    Serial.println(y);

    // botones dims (coinciden con drawWiFiList)
    int btnH = 40;
    int btnPadding = 10; // aumenta área táctil más allá del dibujo
    int btnY = tft.height() - btnH - FOOTER_BOTTOM_GAP;
    int availableHeight = btnY - (TITLE_H + TOP_OFFSET);
    int visibleRows = max(1, availableHeight / ROW_HEIGHT);

    if (!inProximityMode) {
        // ---------- BOTONES INFERIORES (UP/DOWN/OK) ----------
        int btnW = tft.width() / 3;
        if (y >= btnY - btnPadding) { // permitir tocar un poco arriba
            // OK (izquierda)
            if (x < btnW + btnPadding) {
                if (selectedRow < (int)networks.size()) {
                    targetSSID = networks[selectedRow].ssid;
                    targetBSSID = networks[selectedRow].bssid;
                    ema_rssi = NAN;
                    inProximityMode = true;
                    drawProximityScreen();
                    Serial.println("Button OK pressed -> entering proximity");
                }
                return;
            }
            // DOWN (centro)
            else if (x < 2*btnW + btnPadding) {
                if (selectedRow + 1 < (int)networks.size()) {
                    selectedRow++;
                    if (selectedRow >= scrollOffset + visibleRows)
                        scrollOffset = selectedRow - visibleRows + 1;
                    drawWiFiList();
                    Serial.println("Button DOWN pressed (move selection down)");
                }
                return;
            }
            // UP (derecha)
            else {
                if (selectedRow > 0) {
                    selectedRow--;
                    if (selectedRow < scrollOffset) scrollOffset = selectedRow;
                    drawWiFiList();
                    Serial.println("Button UP pressed (move selection up)");
                }
                return;
            }
        }

        // ---------- TOQUE SOBRE FILAS ----------
        int listTop = TITLE_H + TOP_OFFSET;
        if (y >= listTop && y < btnY) {
            int rowIdx = (y - listTop) / ROW_HEIGHT + scrollOffset;
            if (rowIdx >= 0 && rowIdx < (int)networks.size()) {
                selectedRow = rowIdx;
                targetSSID = networks[selectedRow].ssid;
                targetBSSID = networks[selectedRow].bssid;
                ema_rssi = NAN;
                inProximityMode = true;
                drawProximityScreen();
                Serial.print("Row selected by touch: ");
                Serial.println(selectedRow);
            }
            return;
        }
    } else {
        // ---------- MODO PROXIMITY: Volver / Rescan ----------
        int r_btnW = (tft.width() - 30) / 2;
        int r_btnY = tft.height() - 40;
        int btnPadding = 5; // margen extra
        if (y >= r_btnY - btnPadding) {
            // VOLVER
            if (x >= 10 && x < 10 + r_btnW + btnPadding) {
                inProximityMode = false;
                drawWiFiList();
                Serial.println("Proximity: VOLVER pressed");
                return;
            }
            // RESCAN
            else if (x >= 20 + r_btnW && x < 20 + r_btnW + r_btnW + btnPadding) {
                requestRescan = true;
                if (xSemaphoreTake(netMutex, (TickType_t)1000) == pdTRUE) {
                    int n = WiFi.scanNetworks();
                    networks.clear();
                    for (int i = 0; i < n; ++i) {
                        Net e; e.ssid = WiFi.SSID(i); e.bssid = WiFi.BSSIDstr(i); e.rssi = WiFi.RSSI(i); e.channel = WiFi.channel(i);
                        networks.push_back(e);
                    }
                    if (selectedRow >= (int)networks.size()) selectedRow = max(0, (int)networks.size()-1);
                    if (scrollOffset > selectedRow) scrollOffset = selectedRow;
                    xSemaphoreGive(netMutex);
                }
                drawProximityScreen();
                Serial.println("Proximity: RESCAN pressed (quick scan)");
                return;
            }
        }
    }
}

// Map raw touch values to display coordinates (calibrado)
void mapTouchToScreen(int16_t rawx, int16_t rawy, int &x, int &y) {
    // Ajusta TS_MINX, TS_MAXX, TS_MINY, TS_MAXY según calibración real
    x = map(rawx, TS_MINX, TS_MAXX, 0, tft.width() - 1);
    y = map(rawy, TS_MINY, TS_MAXY, 0, tft.height() - 1);

    // limitar al rango de pantalla
    if (x < 0) x = 0; else if (x >= tft.width()) x = tft.width() - 1;
    if (y < 0) y = 0; else if (y >= tft.height()) y = tft.height() - 1;
}
