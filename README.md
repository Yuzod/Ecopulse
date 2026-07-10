# Ecopulse, sistema de monitoreo ambiental

Ecopulse es un sistema IoT de monitoreo ambiental que mide temperatura, humedad, calidad de aire y nivel de sonido, muestra los datos localmente en una pantalla OLED y los envía a la nube (ThingSpeak) para visualizarlos en un dashboard web en tiempo real.

## Arquitectura

```
Sensores (DHT22, MQ135, KY038)
        |
        v
     ESP32  ------------->  Pantalla OLED (vista local)
        |
        v  WiFi
  ThingSpeak (nube)
        |
        v
  Dashboard web
```

El ESP32 lee los sensores, clasifica el aire y el sonido, muestra todo en el OLED y cada 20 segundos envía los 4 valores a un canal de ThingSpeak. El dashboard web lee ese mismo canal para graficar el historial y disparar alertas.

Repositorio actual: https://thingspeak.mathworks.com/channels/3411838 

## Hardware

| Componente | Función |
|---|---|
| DHT22 | Temperatura y humedad |
| MQ135 | Calidad de aire (gas) |
| KY038 | Nivel de sonido |  
| OLED SSD1306 (I2C, 128x64) | Pantalla local | 

## Archivos del repositorio

- **`monitoreo_calidad_aire_final.ino`** — Firmware del ESP32. Lee los sensores, muestra 3 páginas rotativas en el OLED (temperatura/humedad, aire/sonido, estado de red) y envía los datos a ThingSpeak cada 20 segundos.
- **`air_quality_monitoring_dashboard.html`** — Dashboard web que muestra 5 nodos (4 simulados + 1 real conectado a ThingSpeak) con KPIs, mapa de nodos, alertas automáticas y gráficos de historial mediante Chart.js.

## Requisitos

### Firmware (.ino)

- Arduino IDE con soporte para ESP32 instalado
- Librerías: `WiFi`, `Wire`, `DHT` (Adafruit), `Adafruit_GFX`, `Adafruit_SSD1306`, `ThingSpeak`
- Una cuenta de [ThingSpeak](https://thingspeak.com/) con un canal creado y 4 campos (fields) habilitados: temperatura, humedad, aire, sonido

### Dashboard (.html)

- Un navegador moderno (usa `fetch`, Chart.js vía CDN)
- El Channel ID y la Read API Key de tu canal de ThingSpeak

## Configuración

1. Abre `monitoreo_calidad_aire_final.ino` en el Arduino IDE.
2. Edita las siguientes líneas con tus propios datos:
   ```cpp
   const char* WIFI_SSID     = "tu_red";
   const char* WIFI_PASSWORD = "tu_password";
   unsigned long CHANNEL_ID  = 0000000;      // tu Channel ID
   const char* WRITE_API_KEY = "TU_API_KEY"; // tu Write API Key
   ```
3. Sube el código al ESP32.
4. Abre `air_quality_monitoring_dashboard.html` en el navegador.
5. En la barra inferior del dashboard, ingresa el **Channel ID** y la **Read API Key** de tu canal y presiona **Conectar**. El nodo "Mi ESP32" empezará a mostrar tus datos reales.

## Campos de ThingSpeak

| Field | Dato |
|---|---|
| 1 | Temperatura (°C) |
| 2 | Humedad (%) |
| 3 | Calidad de aire (valor crudo MQ135, 0-4095) |
| 4 | Nivel de sonido (valor crudo KY038, 0-4095) |

## Notas y limitaciones

- El MQ135 no entrega una lectura de calidad de aire estandarizada sin calibración previa; los umbrales usados (`Limpio`, `Aceptable`, `Moderado`, `Malo`, `Peligroso`) son de referencia y conviene ajustarlos comparando con un sensor calibrado.
- El dashboard combina 4 nodos simulados con 1 nodo real; los simulados son solo para dar contexto visual de una red más amplia.
- ThingSpeak con cuenta gratuita exige un mínimo de 15 segundos entre escrituras; el firmware usa 20 segundos para tener margen.

## Seguridad

Este proyecto usa credenciales (contraseña de WiFi, API Keys) directamente en el código fuente. Antes de subir cambios a un repositorio público:

- No subas tu Write API Key real de ThingSpeak.
- La Read API Key ingresada en el dashboard web es visible en el navegador; usa solo una llave de **lectura**, nunca la de escritura.

