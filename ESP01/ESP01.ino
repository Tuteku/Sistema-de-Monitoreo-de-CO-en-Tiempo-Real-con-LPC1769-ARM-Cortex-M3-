#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// 1. Configuración de WiFi
// ¡IMPORTANTE! Asegúrate de que estas credenciales sean correctas.
const char* ssid = "FCEFyN 2.4GHz"; 	// <<--- TUS CREDENCIALES
const char* password = ""; 	// <<--- TUS CREDENALES

// 2. Configuración Serial y Servidor
#define BAUDRATE 9600
// --- CONTROL DE REFRESH AQUÍ (en milisegundos) ---
#define REFRESH_INTERVAL_MS 500 
// -------------------------------------------------
ESP8266WebServer server(80);

// 3. Variables de Almacenamiento de Datos
const int DATA_SIZE = 10;
// Array para almacenar las últimas 10 mediciones de la MUESTRA SIMPLE (para el gráfico).
float co_values[DATA_SIZE] = {0.0}; 
int co_index = 0; 

// --- VARIABLES CLAVE DE DATOS ---
float latest_co_value = 0.0;     // Última MUESTRA ("U") - Usada para la ALARMA
float average_co_value = 0.0;    // Último PROMEDIO ("P") - Usado para la referencia suavizada

// --- DECLARACIÓN DE FUNCIONES ---
void handleRoot();
void handleData(); 
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
	server.on("/data", handleData); 
	
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
			
			// 1. EXTRAER EL PREFIJO (Primer caracter: 'U' o 'P')
			char prefix = dataString.charAt(0);
			
			// 2. EXTRAER EL VALOR (Removiendo el prefijo)
			// La subcadena empieza en el índice 1 (después de 'U' o 'P')
			String valueString = dataString.substring(1); 
			
			float newValue = valueString.toFloat();
			
			// DEPURA: Imprime el dato y su tipo
			Serial.print("Received: ");
			Serial.print(prefix);
			Serial.print("=");
			Serial.println(newValue, 2); 

			// 3. ASIGNACIÓN CONDICIONAL: 
            // Guarda el valor en la variable correspondiente según el prefijo
			if (prefix == 'U') {
				// Dato es la MUESTRA SIMPLE (latest_co_value)
				latest_co_value = newValue;
				
				// Usamos la muestra simple para el historial del gráfico
				co_values[co_index] = newValue;
				co_index = (co_index + 1) % DATA_SIZE; 
				
			} else if (prefix == 'P') {
				// Dato es el PROMEDIO (average_co_value)
				average_co_value = newValue;
			}
		}
	}
}

// --- FUNCIONES DEL SERVIDOR WEB ---

// Construye la cadena JSON con la muestra, el promedio y el historial.
String getCoDataJson() {
	String json = "{\"values\":["; // Historial de MUESTRAS SIMPLES
	
	// 1. Array de valores (Muestras Simples para el gráfico)
	for (int i = 0; i < DATA_SIZE; i++) {
		json += String(co_values[i], 2); 
		if (i < DATA_SIZE - 1) {
			json += ","; 
		}
	}
	json += "],";
	
	// 2. Última Muestra Simple ("U")
	json += "\"latest\":" + String(latest_co_value, 2) + ",";
	
	// 3. Último Promedio ("P")
	json += "\"average\":" + String(average_co_value, 2);
	
	json += "}";
	// Formato JSON final: {"values":[1.0, 2.0, ...], "latest": 3.0, "average": 2.5}
	return json;
}

// Sirve el array de datos completo como JSON para AJAX
void handleData() {
	server.send(200, "application/json", getCoDataJson());
}


// Genera la página HTML principal con el gráfico y el script de actualización AJAX
void handleRoot() {
	// Usamos la macro F() y R"=====" para guardar memoria (PROGMEM) y simplificar el HTML
	String html = F(R"=====(
<!DOCTYPE html>
<html>
<head>
	<title>Monitor CO Dinámico</title>
	<meta name="viewport" content="width=device-width, initial-scale=1.0">
	<style>
		body { font-family: 'Inter', sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f4f7f6; }
		.container { max-width: 600px; margin: 0 auto; background-color: #ffffff; padding: 20px; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
		h1 { color: #1f2937; margin-bottom: 5px; }
		.sub-header { color: #6b7280; font-size: 0.9em; margin-bottom: 20px; }
		#coChart { border: 1px solid #ddd; background-color: #fff; margin: 20px auto; max-width: 100%; border-radius: 8px; }
		.data-display { display: flex; justify-content: space-around; padding: 10px 0; border-top: 1px solid #eee; }
		.metric { text-align: center; padding: 0 10px; }
		.metric-value { font-size: 2.2em; font-weight: bold; margin: 0; }
		.metric-label { font-size: 0.8em; color: #9ca3af; }
		.status-indicator { 
			font-weight: bold; 
			padding: 5px 15px; 
			border-radius: 9999px; 
			color: white; 
			margin-top: 10px;
			transition: background-color 0.5s; /* Animación de color */
		}
		.status-safe { background-color: #10b981; } /* Green */
		.status-caution { background-color: #f59e0b; } /* Yellow */
		.status-critical { background-color: #ef4444; } /* Red */
	</style>
</head>
<body>
	<div class="container">
		<h1>Monitor de Concentración de CO</h1>
		<p class="sub-header">Datos en tiempo real desde LPC1769 vía UART/ESP-01.</p>
		
		<div class="data-display">
			<div class="metric">
				<p class="metric-value" id="latest_value">0</p>
				<p class="metric-label">Muestra Última (ppm)</p>
			</div>
			<div class="metric">
				<p class="metric-value" id="average_value">0</p>
				<p class="metric-label">Promedio (ppm)</p>
			</div>
		</div>

		<div id="status_bar_container">
			<div class="status-indicator status-safe" id="status_bar">Nivel Seguro</div>
		</div>
		
		<p class="sub-header">Historial de <span id="data_size_display">)=====");
	html += String(DATA_SIZE); // Inyecta DATA_SIZE
	html += F(R"=====(</span> muestras. Actualización cada )=====");
	html += String((float)REFRESH_INTERVAL_MS / 1000.0f, 2); // Inyecta el tiempo de refresco en segundos
	html += F(R"=====(s.</p>
		
		<canvas id='coChart' width='600' height='300'></canvas>
	</div>

	<script>
		// Constantes inyectadas desde C++
		const DATA_SIZE = )=====");
	html += String(DATA_SIZE); 
	html += F(R"=====(;
		const REFRESH_INTERVAL_MS = )=====");
	html += String(REFRESH_INTERVAL_MS); // Inyección clave del tiempo de refresco
	html += F(R"=====(;

		// Umbrales para la visualización (deben coincidir con tu lógica de PPM)
		const UMBRAL_PRECAUCION = 2; 
		const UMBRAL_CRITICO = 5; 
		
		let co_data = []; 
		
		const canvas = document.getElementById('coChart');
		const ctx = canvas.getContext('2d');
		
		// Ajuste de tamaño de canvas para responsividad
		function resizeCanvas() {
			const containerWidth = canvas.parentNode.offsetWidth;
			canvas.width = containerWidth;
			canvas.height = containerWidth * 0.5; // Relación de aspecto 2:1
		}
		window.addEventListener('resize', resizeCanvas);
		resizeCanvas();

		// --- FUNCIONES DE GRÁFICO ---

		function drawChart() {
			const w = canvas.width;
			const h = canvas.height;
			const padding = 30;

			if (co_data.length === 0) return; 

			// Calcula el valor máximo para la escala Y
			let maxVal = Math.max(...co_data);
			if (maxVal === -Infinity || maxVal < UMBRAL_CRITICO) maxVal = UMBRAL_CRITICO * 1.5; 
			else maxVal *= 1.2; 
			
			if (maxVal === 0) maxVal = 10;

			ctx.clearRect(0, 0, w, h); 

			// --- Zonas de color (Fondo del gráfico) ---
			
			// Zona CRÍTICA (Rojo)
			let criticalY = h - padding - (UMBRAL_CRITICO / maxVal) * (h - 2 * padding);
			ctx.fillStyle = 'rgba(239, 68, 68, 0.1)';
			ctx.fillRect(padding, padding, w - 2 * padding, criticalY - padding);
			
			// Zona PRECAUCIÓN (Amarillo)
			let cautionY = h - padding - (UMBRAL_PRECAUCION / maxVal) * (h - 2 * padding);
			ctx.fillStyle = 'rgba(245, 158, 11, 0.1)';
			ctx.fillRect(padding, criticalY, w - 2 * padding, cautionY - criticalY);
			
			// Zona SEGURA (Verde)
			ctx.fillStyle = 'rgba(16, 185, 129, 0.1)';
			ctx.fillRect(padding, cautionY, w - 2 * padding, h - padding - cautionY);


			// --- Ejes y Etiquetas ---
			ctx.strokeStyle = '#9ca3af';
			ctx.lineWidth = 1;
			
			// Eje X (inferior)
			ctx.beginPath();
			ctx.moveTo(padding, h - padding);
			ctx.lineTo(w - padding, h - padding);
			ctx.stroke();

			// Eje Y (izquierdo)
			ctx.beginPath();
			ctx.moveTo(padding, padding);
			ctx.lineTo(padding, h - padding);
			ctx.stroke();

			ctx.fillStyle = '#6b7280';
			ctx.font = '10px Arial';

			// Etiquetas Y
			const numLabelsY = 4;
			for(let i = 0; i <= numLabelsY; i++) {
				let labelVal = (i / numLabelsY) * maxVal;
				let normalizedY = (labelVal / maxVal) * (h - 2 * padding);
				let y = h - padding - normalizedY;

				ctx.fillText(labelVal.toFixed(1), 5, y + 3); // Valor
				
				// Líneas de cuadrícula (opcional)
				ctx.strokeStyle = '#eee';
				ctx.beginPath();
				ctx.moveTo(padding, y);
				ctx.lineTo(w - padding, y);
				ctx.stroke();
			}

			// --- Línea de datos ---
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
				
				// Puntos de dato
				ctx.fillStyle = '#007BFF';
				ctx.beginPath();
				ctx.arc(x, y, 3, 0, Math.PI * 2);
				ctx.fill();
			}
			ctx.stroke();
		}

		// --- FUNCIÓN DE ESTADO ---
		function updateStatus(latestValue, averageValue) {
			const statusElement = document.getElementById('status_bar');
			const latestElement = document.getElementById('latest_value');
			const averageElement = document.getElementById('average_value');
			
			// Usamos la MUESTRA SIMPLE (latestValue) para definir la ALARMA
			const valueForAlarm = latestValue; 

			latestElement.textContent = latestValue.toFixed(0);
			averageElement.textContent = averageValue.toFixed(1);

			statusElement.className = 'status-indicator'; // Resetear clases

			if (valueForAlarm > UMBRAL_CRITICO) {
				statusElement.classList.add('status-critical');
				statusElement.textContent = "¡PELIGRO CRÍTICO!";
			} else if (valueForAlarm > UMBRAL_PRECAUCION) {
				statusElement.classList.add('status-caution');
				statusElement.textContent = "Precaución Alta";
			} else {
				statusElement.classList.add('status-safe');
				statusElement.textContent = "Nivel Seguro";
			}
		}


		// --- FUNCIÓN DE ACTUALIZACIÓN AJAX (CLAVE) ---

		function updateData() {
			const xhr = new XMLHttpRequest();
			xhr.open('GET', '/data', true); 
			xhr.setRequestHeader('Content-Type', 'application/json');

			xhr.onload = function() {
				if (xhr.status === 200) {
					try {
						const data = JSON.parse(xhr.responseText);
						
						// 1. Obtener Historial (Muestras Simples)
						co_data = data.values; 
						
						// 2. Actualizar Estado y Valores Numéricos con Muestra y Promedio
						updateStatus(data.latest, data.average);

						// 3. Redibujar el Gráfico
						drawChart(); 
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

		// Intentar una carga inicial y resize
		updateData();
		resizeCanvas();
	</script>

</body>
</html>
)=====");

	// Enviar la respuesta HTTP al cliente
	server.send(200, "text/html", html);
}