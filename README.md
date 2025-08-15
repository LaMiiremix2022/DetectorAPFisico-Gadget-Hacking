# WiFi Locator Físico con ESP32 + ILI9341 + XPT2046

Detector físico de puntos de acceso WiFi (AP) usando un ESP32 con pantalla táctil ILI9341. Permite localizar físicamente routers cercanos y medir la intensidad de la señal en tiempo real. Ideal para proyectos de seguimiento de señales WiFi y análisis de cobertura.

---

## Características

- Escaneo de redes WiFi cercanas.
- Visualización en pantalla táctil ILI9341.
- Indicador de intensidad de señal RSSI con barra de colores.
- Modo de proximidad para seguimiento de un AP específico.
- Interfaz táctil para seleccionar SSID y navegar entre opciones.
- Almacenamiento opcional en tarjeta SD (requiere conexión y librería SD).
- Visualización de iconos de batería y SD (decorativos).

---

## Esquema de Conexión

Conexiones entre el ESP-WROOM-32, la pantalla LED ILI9341 y el touch XPT2046:

```
ESP-WROOM-32 -------------> PANTALLA LED (ILI9341) -------------> TOUCH (XPT2046)

3V3                ->              VCC
GND                ->              GND
D15                ->              CS
D4                 ->              RESET
D2                 ->              DC
D23                ->              SDI (MOSI)            ->          T_DIN
D18                ->              SCK                   ->          T_CLK
3V3                ->              LED
D19                ->              SDO (MISO)            ->          T_DO
D21                ->              NADA                  ->          T_CS
D22                ->              NADA                  ->          T_IRQ
```

> Nota: Ajusta los pines `SD_CS` según tu conexión con la tarjeta SD.

---

## Requisitos

- **Hardware**
  - ESP32 (ESP-WROOM-32 recomendado)
  - Pantalla ILI9341
  - Módulo táctil XPT2046
  - Cables de conexión y opcional tarjeta SD

- **Software**
  - [Arduino IDE](https://www.arduino.cc/en/software)
  - Librerías necesarias:
    - `Adafruit_GFX`
    - `Adafruit_ILI9341`
    - `XPT2046_Touchscreen`
    - `SD` (opcional, para almacenamiento)
    - `SPI` (general para SD y pantalla)

---

## Instalación Paso a Paso

1. **Instalar Arduino IDE**
   - Descarga desde la página oficial: [Arduino IDE](https://www.arduino.cc/en/software)
   - Instala siguiendo las instrucciones de tu sistema operativo.

2. **Configurar ESP32 en Arduino IDE**
   - Abre Arduino IDE → `Archivo` → `Preferencias`
   - En `Gestor de URLs adicionales de tarjetas`, añade:
     ```
     https://dl.espressif.com/dl/package_esp32_index.json
     ```
   - Ve a `Herramientas` → `Placa` → `Gestor de tarjetas` y busca `ESP32`. Instálalo.

3. **Instalar librerías**
   - Ve a `Programa` → `Incluir Librería` → `Gestionar Bibliotecas…`
   - Busca e instala:
     - `Adafruit GFX Library`
     - `Adafruit ILI9341`
     - `XPT2046_Touchscreen`
   - Asegúrate de que el archivo `Org_01.h` está en `Adafruit_GFX/Fonts/`.

4. **Configurar pines en el código**
   - Ajusta los pines según tu conexión física:
     ```cpp
     #define TFT_CS   15
     #define TFT_DC   2
     #define TFT_RST  4
     #define TOUCH_CS 21
     #define TOUCH_IRQ 22
     #define SD_CS    5 // Ajustar según tu tarjeta SD
     ```

5. **Subir código al ESP32**
   - Conecta el ESP32 por USB.
   - Selecciona la placa correcta: `ESP32 Dev Module`.
   - Selecciona el puerto correcto en `Herramientas → Puerto`.
   - Pulsa `Subir`.

6. **Uso**
   - Al iniciar, la pantalla mostrará un banner de bienvenida.
   - La lista de redes WiFi cercanas aparecerá automáticamente.
   - Usa los botones táctiles `UP`, `DOWN` y `OK` para seleccionar un SSID.
   - Al seleccionar un AP, se activará el modo de proximidad con barra de señal RSSI en tiempo real.

---

## Contribuciones y Forks

Este proyecto está abierto a mejoras:

- Mejor calibración táctil.
- Nuevos estilos de UI.
- Almacenamiento avanzado de redes detectadas.
- Integración con mapas o GPS.

Si quieres mejorar el proyecto, **haz un fork y envía pull requests**.

> Codigos de colores para Panel LED:

[Ir a codigos de colores](https://gist.github.com/Kongduino/36d152c81bbb1214a2128a2712ecdd18)

---

## Captura de pantalla (simulación)

```
[Banner de bienvenida animado]
[Lista de SSID con nivel de señal y barra de color]
[Botones táctiles: UP, DOWN, OK]
[Modo proximidad: barra RSSI con color y valor dBm]
```

---

## Licencia

Proyecto de código abierto. Usa y modifica bajo tu responsabilidad. No se garantiza el uso para fines ilegales.

---

## Autor

Creado por **d1se0**  
Detector de Wifi cercano físico basado en ESP32 + ILI9341 + XPT2046.
