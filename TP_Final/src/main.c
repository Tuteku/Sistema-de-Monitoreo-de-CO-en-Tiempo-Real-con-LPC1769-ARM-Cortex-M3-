/*
Togglea el led rojo integrado cada 1 segundo usando el Timer0 del LPC1769.
*/

#include "lpc17xx.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_adc.h"
#include <stdio.h>

#define LED_ROJO 		(1<<2)
#define LED_VERDE 		(1<<27)
#define LED_AMARILLO 	(1<<3)
#define BUZZER 			(1<<22)
#define ADC_FREQ 		20000

#define UMBRAL_PRECAUCION_MV    1500   // Ajustar segun calibracion
#define UMBRAL_CRITICO_MV 		2200 // Ajustar segun calibracion

volatile uint16_t adc_value_mv = 0;
volatile uint8_t flag_buzzer_toggle = 0;
volatile uint8_t prueba = 0;


void cfgPin(uint8_t port_num, uint8_t pin_num, uint8_t func_num);
void pinConfiguration(void);
void cfgTimer(void);
void cfgADC(void);
void cfgUART(void);

void UART_SendString(char *str); //Funcion para mandar string por UART
void UART_SendNumber(uint16_t num); //Funcion para mandar las mediciones del sensor
void intToStr(uint16_t num, uint8_t buffer[]);

int main(){

	pinConfiguration();

    // Indicación visual de inicio: parpadeo rapido de todos los LEDs
    for(uint8_t i = 0; i < 3; i++){
    	LPC_GPIO0 -> FIOSET |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
        LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
    }

	cfgUART();
    cfgADC();
    cfgTimer();

	while(1){
		// Bucle principal vacio, las tareas se manejan por interrupcion.
	};

    return 0;
}


void cfgPin(uint8_t port, uint8_t pin_num, uint8_t func){
	PINSEL_CFG_Type pin;
	pin.Portnum = port;
	pin.Pinnum = pin_num;
	pin.Funcnum = func;
	pin.Pinmode = PINSEL_PINMODE_TRISTATE;
	pin.OpenDrain = PINSEL_PINMODE_NORMAL;
	PINSEL_ConfigPin(&pin);
}

void pinConfiguration(void){
    // ADC0.0 (Sensor CO)
	cfgPin(0, 23, 1);

    // MAT0.1 (Trigger del ADC)
	cfgPin(1, 29, 3);

    // TXD2 (UART2 para ESP8266)
	cfgPin(0, 10, 1);

    // P0.22 (Buzzer)
	cfgPin(0, 22, 0);

    // Configuracion de LEDs (P0.2, P0.27, P0.3) y Buzzer (P0.22) como salidas
	LPC_GPIO0 -> FIODIR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER );
	LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER );
}

void cfgTimer(void){
	// --- Timer 0: Generador de Pulsos (Trigger ADC y Envio UART) ---
	TIM_TIMERCFG_Type timerMode0;
	timerMode0.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode0.PrescaleValue = 1000; //Base de tiempo de 1ms

	TIM_MATCHCFG_Type timerMAT01; // Para iniciar ADC y la Interrupcion de Envio UART
	timerMAT01.MatchChannel = 1;
	timerMAT01.IntOnMatch = ENABLE; // Generar interrupcion para el envío UART
	timerMAT01.StopOnMatch = DISABLE;
	timerMAT01.ResetOnMatch = ENABLE;
	timerMAT01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE; // Trigger para el ADC
	timerMAT01.MatchValue = 500 ; // Interrumpe y dispara ADC cada 500ms (2Hz)

	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerMode0);
	TIM_ConfigMatch(LPC_TIM0, &timerMAT01);
	TIM_Cmd(LPC_TIM0, ENABLE);
	NVIC_EnableIRQ(TIMER0_IRQn);

	// --- Timer 1: Generador de Tono para Buzzer (Alarma) ---
	// Se usa como base de tiempo para alternar el Buzzer en el ISR de TIMER1
	TIM_TIMERCFG_Type timerMode1;
	timerMode1.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode1.PrescaleValue = 100; // Base de tiempo de 100us

	TIM_MATCHCFG_Type timerMAT10;
	timerMAT10.MatchChannel = 0;
	timerMAT10.IntOnMatch = ENABLE;
	timerMAT10.StopOnMatch = DISABLE;
	timerMAT10.ResetOnMatch = ENABLE;
	timerMAT10.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
	timerMAT10.MatchValue = 500; // Genera interrupción cada 500ms


	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
//	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
//	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
//	TIM_Cmd(LPC_TIM1, ENABLE);
}

void cfgADC(void){
	ADC_Init(LPC_ADC, ADC_FREQ);
	ADC_BurstCmd(LPC_ADC, DISABLE); // Deshabilitar modo Burst
	ADC_StartCmd(LPC_ADC, ADC_START_ON_MAT01); // Iniciar por Match1 del Timer 0
	ADC_EdgeStartConfig(LPC_ADC, ADC_START_ON_RISING);
	ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE); // Habilitar canal 0
	ADC_IntConfig(LPC_ADC, ADC_ADINTEN0, ENABLE); // Habilitar interrupcion en canal 0

	NVIC_EnableIRQ(ADC_IRQn);
}

void cfgUART(void){
	UART_CFG_Type pinUART2;
	UART_FIFO_CFG_Type UARTFIFO;
	pinUART2.Baud_rate = 9600;
	UART_ConfigStructInit(&pinUART2);
	UART_Init(LPC_UART2, &pinUART2);
	UART_FIFOConfigStructInit(&UARTFIFO);
	UART_FIFOConfig(LPC_UART2, &UARTFIFO);
	UART_IntConfig(LPC_UART2, UART_INTCFG_RBR, DISABLE);
	UART_TxCmd(LPC_UART2, ENABLE);
}

// --- MANEJO DE INTERRUPCIONES ---

void ADC_IRQHandler(void){
    // Contador para mantener el estado de alarma crítica
	static uint8_t alarm_counter = 0;

    if(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)){

        // CORRECCIÓN: Cálculo preciso del voltaje escalado (0-3300 mV)
        // Se utiliza la fórmula V_out = (ADC_Lectura / 4096) * 3300
        // Nota: Si se requiere un offset de 400 (como en tu código original), descomentar la línea:
        // float raw_adc = (float)ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
        // adc_value_mv = (uint16_t) ( ((raw_adc - 400.0f) * 3300.0f) / 4096.0f );

        uint32_t raw_adc = ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0);
        adc_value_mv = (uint16_t) ( ((float)raw_adc * 3300.0f) / 4096.0f );

        // --- Logica de Alarmas ---
		if(adc_value_mv < UMBRAL_PRECAUCION_MV){
			// Estado SEGURO: LED Verde
			alarm_counter = 0;
			LPC_GPIO0 -> FIOSET |= (LED_VERDE);
			LPC_GPIO0 -> FIOCLR |= (LED_AMARILLO | LED_ROJO | BUZZER);
	        TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		} else if(alarm_counter >= 10){
			// Estado CRITICO: LED Rojo + Buzzer
			if (adc_value_mv >= UMBRAL_CRITICO_MV) {
                alarm_counter = 10; // Asegura que el estado critico se mantenga
            }

			LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_AMARILLO);
			LPC_GPIO0 -> FIOSET |= (LED_ROJO);
			TIM_Cmd(LPC_TIM1, ENABLE); // Activar buzzer con PWM
			NVIC_EnableIRQ(TIMER1_IRQn);
		} else if(adc_value_mv >= UMBRAL_PRECAUCION_MV){
			// Estado PRECAUCION: LED Amarillo
			alarm_counter++; // Acumular lecturas de precaucion
			LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | BUZZER);
			LPC_GPIO0 -> FIOSET |= (LED_AMARILLO);
			TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		}
    }
}
void TIMER1_IRQHandler(void){
	// Maneja la oscilacion del buzzer para generar un tono.
    if(TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)){
        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);

        if(flag_buzzer_toggle == 0){
			LPC_GPIO0 -> FIOSET |= (BUZZER);
			flag_buzzer_toggle = 1;
        } else{
            LPC_GPIO0 -> FIOCLR |= (BUZZER);
            flag_buzzer_toggle = 0;
        }
    }
}

void TIMER0_IRQHandler(void){
	// Maneja el evento de envio UART (ocurre cada 500ms)
    if(TIM_GetIntStatus(LPC_TIM0, TIM_MR1_INT)){
        TIM_ClearIntPending(LPC_TIM0, TIM_MR1_INT);
        // Envía el valor de voltaje escalado para que el ESP lo maneje
		UART_SendNumber(adc_value_mv);
    }
}

// --- FUNCIONES DE CONVERSIÓN Y UART ---

// Función auxiliar para convertir un número (uint16_t) a cadena (char array)
void intToStr(uint16_t num, uint8_t buffer[]) {
    int i = 0;
    uint16_t temp = num;

    if (num == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    // Procesar dígitos en orden inverso
    while (temp > 0) {
        buffer[i++] = (temp % 10) + '0';
        temp /= 10;
    }

    // Invertir la cadena (ej: 5100 -> 0015)
    int len = i;
    for (int j = 0; j < len / 2; j++) {
        char swap = buffer[j];
        buffer[j] = buffer[len - 1 - j];
        buffer[len - 1 - j] = swap;
    }

    // Agregar el terminador nulo \0
    buffer[len] = '\0';
}

// Envia el valor de concentracion al ESP8266
void UART_SendNumber(uint16_t num){
    // Buffer seguro para 5 dígitos + '\n' + '\0'
    uint8_t buffer[8];

    // Calculo de la concentracion final (PPM)
    // Usamos el voltaje num (adc_value_mv) para calcular la concentración final.
    // Formula: PPM = (4.13 * V + 531.2) / 1980.0

    float concentration_ppm = (4.13f * (float)num + 531.2f) / 1980.0f;

    // Aseguramos que la concentración sea un valor entero (ppm) para el envio UART
    uint16_t var_co = (uint16_t)concentration_ppm;

    // 1. Convertir el numero (uint16_t) a cadena en el búfer
    intToStr(var_co, buffer);

    // 2. Encontrar la longitud de la cadena de digitos
    int len = 0;
    while (buffer[len] != '\0' && len < 6) {
        len++;
    }

    // 3. AGREGAR DELIMITADOR \n
    // Se inserta el salto de linea al final de los dígitos.
    if (len < 7) {
        buffer[len] = '\n';
        // El terminador nulo es opcional aqui, pero ayuda en la depuracion.
        buffer[len + 1] = '\0';

        // 4. Enviar la cadena completa (ej: "5\n" o "1500\n").
        // Se envia len + 1 bytes (los digitos + el '\n').
        UART_Send(LPC_UART2, buffer, len + 1, BLOCKING);
    }
    // Si len >= 7, hay overflow o error, y el dato no se envia
}
