#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// 1. Configuración de WiFi
const char* ssid = "2.4Personal-4839D"; 	 // <<--- TUS CREDENCIALES
const char* password = "mati200200"; 	// <<--- TUS CREDENALES

// 2. Configuración Serial y Servidor
#define BAUDRATE 9600
// --- CONTROL DE REFRESH AQUÍ (en milisegundos) ---
#define REFRESH_INTERVAL_MS 500 
// -------------------------------------------------
ESP8266WebServer server(80);

// 3. Variables de Almacenamiento de Datos
const int DATA_SIZE = 10;
// Array para almacenar las últimas 10 mediciones. Usamos float para compatibilidad.
float co_values[DATA_SIZE] = {0.0}; 
int co_index = 0; // Índice para saber dónde insertar el próximo valor
float latest_co_value = 0.0; // <<<--- VARIABLE AÑADIDA: ALMACENA LA ÚLTIMA LECTURA

// --- DECLARACIÓN DE FUNCIONES ---
void handleRoot();
void handleData(); // Nueva función para servir solo el JSON
void handleSerial();
String getCoDataJson();

// --- SETUP ---
void setup() {
	Serial.begin(BAUDRATE); // Inicia Serial con el LPC1769 a 9600 baudios
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

	// 2. Definición de Handlers
	server.on("/", handleRoot); 
	server.on("/data", handleData); // NUEVA RUTA: Sirve el JSON de datos para el AJAX
	
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

void handleSerial() {
	// Revisa si hay datos disponibles en el puerto Serial
	if (Serial.available() > 0) {
		
		// Lee la cadena hasta que encuentra un salto de línea (delimitador)
		String dataString = Serial.readStringUntil('\n');
		dataString.trim(); // Elimina espacios en blanco y caracteres de control

		// Verifica que la cadena no esté vacía
		if (dataString.length() > 0) {
			
			// Convierte la cadena (ej: "5") a un valor de punto flotante. 
            // Aunque el LPC envía un entero, toFloat funciona bien para el almacenamiento.
			float newValue = dataString.toFloat();
			
			// Muestra el valor recibido para depuración
			Serial.print("Value Received: ");
			Serial.println(newValue, 2); 

			// Almacenamiento: Inserta el nuevo valor en el array circularmente
			co_values[co_index] = newValue;
			co_index = (co_index + 1) % DATA_SIZE; 
			latest_co_value = newValue; // <<<--- ACTUALIZACIÓN CRÍTICA
		}
	}
}

// --- FUNCIONES DEL SERVIDOR WEB ---

// Construye la cadena JSON del array de datos y el último valor
String getCoDataJson() {
	String json = "{\"values\":["; // <<<--- CAMBIO DE FORMATO JSON
	
	// 1. Array de valores
	for (int i = 0; i < DATA_SIZE; i++) {
		json += String(co_values[i], 2); // Agrega el valor con dos decimales
		if (i < DATA_SIZE - 1) {
			json += ","; 
		}
	}
	json += "],";
	
	// 2. Último valor
	json += "\"latest\":" + String(latest_co_value, 2);
	
	json += "}";
	// El formato JSON final es: {"values":[1.0, 2.0, ...], "latest": 3.0}
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
	<title>Monitor CO Dinamico</title>
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
		}
		.status-safe { background-color: #10b981; } /* Green */
		.status-caution { background-color: #f59e0b; } /* Yellow */
		.status-critical { background-color: #ef4444; } /* Red */
	</style>
</head>
<body>
	<div class="container">
		<h1>Monitor de Concentracion de CO</h1>
		<p class="sub-header">Datos en tiempo real desde LPC1769 via UART/ESP-01.</p>
		
		<div class="data-display">
			<div class="metric">
				<p class="metric-value" id="latest_value">0</p>
				<p class="metric-label">Ultima Concentracion (ppm)</p>
			</div>
			<div class="metric">
				<p class="metric-value" id="status_text">Seguro</p>
				<p class="metric-label">Estado</p>
			</div>
		</div>

		<div id="status_bar_container">
			<div class="status-indicator status-safe" id="status_bar">Nivel Seguro</div>
		</div>
		
		<p class="sub-header">Ultimos <span id="data_size_display">)=====");
	html += String(DATA_SIZE); // Inyecta DATA_SIZE
	html += F(R"=====(</span> valores. Actualizacion cada )=====");
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
		
		let co_data = []; // El array comienza vacío y se llena con AJAX
		
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

			if (co_data.length === 0) return; // No dibujar si no hay datos

			// Calcula el valor máximo para la escala Y
			let maxVal = Math.max(...co_data);
			if (maxVal === -Infinity || maxVal < UMBRAL_CRITICO) maxVal = UMBRAL_CRITICO * 1.5; 
			else maxVal *= 1.2; 
			
			// Si maxVal es 0, forzar un valor mínimo para que el gráfico sea visible.
			if (maxVal === 0) maxVal = 10;

			ctx.clearRect(0, 0, w, h); // Limpiar el canvas

			// --- Zonas de color (Fondo del gráfico) ---
			
			// Zona CRÍTICA (Rojo)
			let criticalY = h - padding - (UMBRAL_CRITICO / maxVal) * (h - 2 * padding);
			ctx.fillStyle = 'rgba(239, 68, 68, 0.1)';
			ctx.fillRect(padding, padding, w - 2 * padding, criticalY - padding);
			
			// Zona PRECAUCIÓN (Amarillo)
			let cautionY = h - padding - (UMBRAL_PRECAUCION / maxVal) * (h - 2 * padding);
			ctx.fillStyle = 'rgba(245, 158, 11, 0.1)';
			ctx.fillRect(padding, criticalY, w - 2 * padding, cautionY - criticalY);
			
			// Zona SEGURA (Verde) - Del umbral de precaución hacia abajo
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
		function updateStatus(value) {
			const statusElement = document.getElementById('status_bar');
			const valueElement = document.getElementById('latest_value');
			const textElement = document.getElementById('status_text');

			valueElement.textContent = value.toFixed(0);
			statusElement.className = 'status-indicator'; // Resetear clases

			if (value > UMBRAL_CRITICO) {
				statusElement.classList.add('status-critical');
				statusElement.textContent = "¡PELIGRO CRITICO!";
				textElement.textContent = "Critico";
			} else if (value > UMBRAL_PRECAUCION) {
				statusElement.classList.add('status-caution');
				statusElement.textContent = "Precaucion Alta";
				textElement.textContent = "Precaucion";
			} else {
				statusElement.classList.add('status-safe');
				statusElement.textContent = "Nivel Seguro";
				textElement.textContent = "Seguro";
			}
		}


		// --- FUNCIÓN DE ACTUALIZACIÓN AJAX (CLAVE) ---

		function updateData() {
			const xhr = new XMLHttpRequest();
			// Petición a la ruta /data
			xhr.open('GET', '/data', true); 
			xhr.setRequestHeader('Content-Type', 'application/json');

			xhr.onload = function() {
				if (xhr.status === 200) {
					try {
						const data = JSON.parse(xhr.responseText);
						// El nuevo array de valores
						co_data = data.values; 
						
						// Actualizar elementos
						updateStatus(data.latest);
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
		
		// Llamar a resize al inicio para configurar el canvas correctamente
		resizeCanvas();
	</script>

</body>
</html>
)=====");

	// Enviar la respuesta HTTP al cliente
	server.send(200, "text/html", html);
}