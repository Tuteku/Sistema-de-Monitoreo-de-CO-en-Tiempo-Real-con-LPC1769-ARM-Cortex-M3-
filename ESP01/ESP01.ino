#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// 1. Configuración de WiFi
const char* ssid = "2.4Personal-4839D";       // <<--- CAMBIA ESTO
const char* password = "mati200200";  // <<--- CAMBIA ESTO

// 2. Configuración Serial y Servidor
#define BAUDRATE 9600
ESP8266WebServer server(80);

// 3. Variables de Almacenamiento de Datos
const int DATA_SIZE = 10;
// Array para almacenar las últimas 10 mediciones de CO
float co_values[DATA_SIZE] = {0.0}; 
int co_index = 0; // Índice para saber dónde insertar el próximo valor

// --- DECLARACIÓN DE FUNCIONES ---
void handleRoot();
void handleSerial();
String getCoDataJson();

// --- SETUP ---
void setup() {
  // WiFi.disconnect(true);
  // WiFi.mode(WIFI_OFF);
  // Serial.begin(BAUDRATE);
  // Serial.println("Credenciales borradas. Flasheando el codigo principal");
  Serial.begin(BAUDRATE); // Inicia Serial
  WiFi.mode(WIFI_STA); 
  // 1. Conexión a la red WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado.");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP()); 

  // 2. Definición del Handler para la página principal
  server.on("/", handleRoot); 

  // 3. Iniciar el servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

// --- LOOP PRINCIPAL ---
void loop() {
  // 1. Manejar las peticiones HTTP pendientes (OBLIGATORIO para el servidor)
  server.handleClient(); 
  
  // 2. Manejar la lectura serial desde el LPC1769
  handleSerial();
}

// --- FUNCIONES DE LECTURA SERIAL ---

/**
 * Función que gestiona la lectura de datos desde el puerto Serial (UART del LPC1769).
 * Asume que el LPC1769 envía un valor float seguido de un salto de línea (\n).
 */
void handleSerial() {
  // Revisa si hay datos disponibles en el puerto Serial
  if (Serial.available() > 0) {
    
    // Lee la cadena hasta que encuentra un salto de línea (delimitador)
    String dataString = Serial.readStringUntil('\n');
    dataString.trim(); // Elimina espacios en blanco y caracteres de control

    // Verifica que la cadena no esté vacía
    if (dataString.length() > 0) {
      
      // Convierte la cadena a un valor de punto flotante (float)
      float newValue = dataString.toFloat();
      
      // Muestra el valor recibido para depuración
      Serial.print("CO Value Received: ");
      Serial.println(newValue, 2); 

      // Almacenamiento: Inserta el nuevo valor en el array circularmente
      co_values[co_index] = newValue;
      co_index = (co_index + 1) % DATA_SIZE; // Asegura que el índice vuelva a 0 después de DATA_SIZE
    }
  }
}

// --- FUNCIONES DEL SERVIDOR WEB ---

// Construye la cadena JSON del array de datos para inyectarla en JavaScript
String getCoDataJson() {
  String json = "[";
  for (int i = 0; i < DATA_SIZE; i++) {
    // Agrega el valor con dos decimales
    json += String(co_values[i], 2); 
    if (i < DATA_SIZE - 1) {
      json += ","; // Agrega coma si no es el último elemento
    }
  }
  json += "]";
  return json;
}

// Genera la página HTML con el gráfico
void handleRoot() {
  String coData = getCoDataJson();

  // El código HTML es el mismo que en la respuesta anterior
  String html = "<!DOCTYPE html><html><head><title>Monitor CO</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'></head><body>";
  
  html += "<h1>Monitor de Niveles de CO</h1>";
  html += "<p>Ultimos " + String(DATA_SIZE) + " valores recopilados. (Actualiza la pagina para ver nuevos datos)</p>";

  // Contenedor del Gráfico (Canvas)
  html += "<canvas id='coChart' width='300' height='200'></canvas>";

  // --- Bloque JavaScript para dibujar el gráfico ---
  html += "<script>";
  
  // 1. Inyectar los datos del array C++ directamente en JS
  html += "var co_data = " + coData + ";"; 

  // 2. Código de visualización (Usando Canvas nativo para ligereza)
  html += "var ctx = document.getElementById('coChart').getContext('2d');";
  html += "var w = 300; var h = 200; var padding = 20;";
  
  // Calcula valores min/max del array
  html += "var maxVal = Math.max(...co_data);";
  html += "if (maxVal === -Infinity || maxVal === 0) maxVal = 10;"; // Default
  html += "maxVal *= 1.1;"; // Deja un margen superior
  
  html += "ctx.clearRect(0, 0, w, h);"; // Limpiar el canvas
  html += "ctx.beginPath();";
  html += "ctx.strokeStyle = '#FF5733';";
  html += "ctx.lineWidth = 2;";

  // Dibuja el eje Y (opcional)
  html += "ctx.moveTo(padding, padding); ctx.lineTo(padding, h - padding); ctx.stroke();";
  
  // Mapeo del primer punto
  html += "var x = padding; var y = h - padding - (co_data[0] / maxVal) * (h - 2 * padding);";
  html += "ctx.moveTo(x, y);";
  
  // Mapeo y dibujo de los puntos restantes
  html += "for(var i = 1; i < co_data.length; i++){";
  html += "  x = padding + (i / (co_data.length - 1)) * (w - 2 * padding);";
  html += "  y = h - padding - (co_data[i] / maxVal) * (h - 2 * padding);";
  html += "  ctx.lineTo(x, y);";
  html += "}";
  html += "ctx.stroke();";
  
  html += "</script>";
  // --------------------------------------------------------
  
  html += "</body></html>";

  // Enviar la respuesta HTTP al cliente
  server.send(200, "text/html", html);
}