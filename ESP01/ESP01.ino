#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// 1. Configuración de WiFi
const char* ssid = "2.4Personal-4839D"; 
const char* password = "mati200200";

// 2. Configuración Serial y Servidor
#define BAUDRATE 9600
// --- CONTROL DE REFRESH AQUÍ (en milisegundos) ---
#define REFRESH_INTERVAL_MS 500 
// -------------------------------------------------
ESP8266WebServer server(80);

// 3. Variables de Almacenamiento de Datos
const int DATA_SIZE = 10;
// Array para almacenar las últimas 10 mediciones. Usamos float.
float co_values[DATA_SIZE] = {0.0}; 
int co_index = 0; // Índice para saber dónde insertar el próximo valor

// --- DECLARACIÓN DE FUNCIONES ---
void handleRoot();
void handleData(); // Nueva función para servir solo el JSON
void handleSerial();
String getCoDataJson();

// --- SETUP ---
void setup() {
  Serial.begin(BAUDRATE); // Inicia Serial con el LPC1769 a 9600 baudios
  WiFi.mode(WIFI_STA); 
  
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado.");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP()); 


  server.on("/", handleRoot); 
  server.on("/data", handleData); // NUEVA RUTA: Sirve el JSON de datos para el AJAX

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

// --- LOOP PRINCIPAL ---
void loop() {
  server.handleClient(); 

  handleSerial();
}

// --- FUNCIONES DE LECTURA SERIAL ---

void handleSerial() {

  if (Serial.available() > 0) {  
    String dataString = Serial.readStringUntil('\n');
    dataString.trim(); // Elimina espacios en blanco y caracteres de control

    if (dataString.length() > 0) {

      float newValue = dataString.toFloat();

      Serial.print("Value Received: ");
      Serial.println(newValue, 2); 

      co_values[co_index] = newValue;
      co_index = (co_index + 1) % DATA_SIZE; 
    }
  }
}

// --- FUNCIONES DEL SERVIDOR WEB ---

// Construye la cadena JSON del array de datos
String getCoDataJson() {
  String json = "[";
  for (int i = 0; i < DATA_SIZE; i++) {
    json += String(co_values[i], 2); // Agrega el valor con dos decimales
    if (i < DATA_SIZE - 1) {
      json += ","; 
    }
  }
  json += "]";
  return json;
}

// Sirve el array de datos completo como JSON para AJAX
void handleData() {
  server.send(200, "application/json", getCoDataJson());
}


// Genera la página HTML principal con el gráfico y el script de actualización AJAX
void handleRoot() {
  String html = F(R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Monitor CO</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f4f7f6; }
    h1 { color: #333; }
    #coChart { border: 1px solid #ddd; background-color: #fff; margin: 20px auto; max-width: 90%; }
    p { color: #666; }
  </style>
</head>
<body>
  <h1>Monitor de Niveles de CO (en ppm) (LPC1769)</h1>
  <p>Ultimos <span id="data_size_display">)=====");
  html += String(DATA_SIZE); // Inyecta DATA_SIZE
  html += F(R"=====(</span> valores. Actualizacion cada )=====");
  html += String((float)REFRESH_INTERVAL_MS / 1000.0f, 2); // Inyecta el tiempo de refresco en segundos
  html += F(R"=====(s.</p>
  <canvas id='coChart' width='300' height='200'></canvas>

  <script>
    // Constantes inyectadas desde C++
    const DATA_SIZE = )=====");
  html += String(DATA_SIZE); 
  html += F(R"=====(;
    const REFRESH_INTERVAL_MS = )=====");
  html += String(REFRESH_INTERVAL_MS); // Inyección clave del tiempo de refresco
  html += F(R"=====(;

    let co_data = []; // El array comienza vacío y se llena con AJAX

    const ctx = document.getElementById('coChart').getContext('2d');
    const w = 300; 
    const h = 200; 
    const padding = 20;

    // --- FUNCIONES DE GRÁFICO ---

    function drawChart() {
        if (co_data.length === 0) return; // No dibujar si no hay datos

        // Calcula el valor máximo para la escala Y (máximo 3300 mV, pero es dinámico)
        let maxVal = Math.max(...co_data);
        if (maxVal === -Infinity || maxVal < 10) maxVal = 500; 
        else maxVal *= 1.1; 

        ctx.clearRect(0, 0, w, h); // Limpiar el canvas

        // Dibujar ejes (simplificado)
        ctx.strokeStyle = '#999';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(padding, h - padding);
        ctx.lineTo(w - padding, h - padding); // Eje X
        ctx.moveTo(padding, padding);
        ctx.lineTo(padding, h - padding); // Eje Y
        ctx.stroke();

        // Dibujar la línea de datos
        ctx.beginPath();
        ctx.strokeStyle = '#007BFF'; // Azul
        ctx.lineWidth = 2;

        for(let i = 0; i < co_data.length; i++){
            let x = padding + (i / (DATA_SIZE - 1)) * (w - 2 * padding);
            let value = co_data[i] || 0; 
            let normalizedY = (value / maxVal) * (h - 2 * padding);
            let y = h - padding - normalizedY;

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();
    }


    // --- FUNCIÓN DE ACTUALIZACIÓN AJAX (CLAVE) ---

    function updateData() {
        const xhr = new XMLHttpRequest();
        // Petición a la nueva ruta /data
        xhr.open('GET', '/data', true); 
        xhr.setRequestHeader('Content-Type', 'application/json');

        xhr.onload = function() {
            if (xhr.status === 200) {
                try {
                    // El nuevo JSON reemplaza el array de datos
                    co_data = JSON.parse(xhr.responseText);
                    drawChart(); // Redibuja el gráfico con los nuevos datos
                } catch (e) {
                    console.error("Error al parsear JSON:", e);
                }
            }
        };
        xhr.onerror = function() {
            console.error("Error al conectar con el servidor.");
        };
        xhr.send();
    }

    // Configurar la actualización dinámica usando la constante inyectada
    setInterval(updateData, REFRESH_INTERVAL_MS); 

    // Intentar una carga inicial
    updateData();
  </script>

</body>
</html>
)=====");

  server.send(200, "text/html", html);
}