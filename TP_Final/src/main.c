#include "lpc17xx.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_gpdma.h"
#include "lpc17xx_exti.h"
#include <stdio.h>
#include <math.h>

#define LED_ROJO 		(1<<2)
#define LED_VERDE 		(1<<27)
#define LED_AMARILLO 	(1<<3)
#define BUZZER 			(1<<22)

#define ADC_FREQ 		20000
#define NUM_SAMPLES_ADC	10

#define UMBRAL_PRECAUCION_PPM   25   	// Ajustar segun calibracion
#define R0_SENSOR				72.41  	// Resistencia en kOhm del sensor en aire limpio
#define RL_SENSOR				47
#define MUESTRAS_PELIGROSAS_SEG	10

#define TIEMPO_MUESTRA_PROMEDIO 3000
#define TIEMPO_TRANSMISION_VIVO 10000

volatile uint8_t 	flag_buzzer_toggle = 0;
volatile uint8_t 	status_flag = 0;

volatile uint16_t 	actual_adc_samples[NUM_SAMPLES_ADC];
volatile uint16_t 	last_adc_value_ppm = 0;
volatile uint16_t 	last_samples_for_average[NUM_SAMPLES_ADC];
volatile uint16_t 	samples_average_ppm = 0;


void cfgPin(uint8_t port_num, uint8_t pin_num, uint8_t func_num);
void pinConfiguration(void);
void cfgTimer(void);
void cfgADC(void);
void cfgUART(void);
void cfgDMA(void);
void DMA_SetupAndStart_CH7(void);

uint16_t calc_average_ppm(void);
uint16_t convert_adc_to_ppm(uint16_t raw_data);

void UART_SendChar_Safe(char c);
void UART_SendNumber_Safe(char prefix, uint16_t num);

int main(){

	pinConfiguration();

    // Indicación visual de inicio: parpadeo rapido de todos los LEDs
    for(uint8_t i = 0; i < 3; i++){
    	LPC_GPIO0 -> FIOSET |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
        LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
    }

	cfgDMA();
	cfgUART();
    cfgADC();
    cfgTimer();

	while(1){};

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
	timerMAT01.IntOnMatch = DISABLE; // Generar interrupcion para el envío UART
	timerMAT01.StopOnMatch = DISABLE;
	timerMAT01.ResetOnMatch = ENABLE;
	timerMAT01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE; // Trigger para el ADC
	timerMAT01.MatchValue = 499 ; // Interrumpe y dispara ADC cada 500ms (2Hz)

	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerMode0);
	TIM_ConfigMatch(LPC_TIM0, &timerMAT01);
	TIM_Cmd(LPC_TIM0, ENABLE);

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
	timerMAT10.MatchValue = 499; // Genera interrupción cada 500ms

	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
	NVIC_EnableIRQ(TIMER1_IRQn);

	// --- Timer 2: Cambia el valor a transmitir entre la transmision en tiempo real y el promedio ---
	TIM_TIMERCFG_Type timerMode2;
	timerMode2.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode2.PrescaleValue = 1000; //Base de tiempo de 1ms

	TIM_MATCHCFG_Type timerMAT20;
	timerMAT20.MatchChannel = 0;
	timerMAT20.IntOnMatch = ENABLE;
	timerMAT20.StopOnMatch = ENABLE;
	timerMAT20.ResetOnMatch = DISABLE;
	timerMAT20.ExtMatchOutputType = TIM_EXTMATCH_NOTHING; // Trigger para el ADC
	timerMAT20.MatchValue = (uint32_t)TIEMPO_TRANSMISION_VIVO ; // Tiempo inicial 10 segundos

	TIM_Init(LPC_TIM2, TIM_TIMER_MODE, &timerMode2);
	TIM_ConfigMatch(LPC_TIM2, &timerMAT20);
	TIM_Cmd(LPC_TIM2, ENABLE);

	NVIC_EnableIRQ(TIMER2_IRQn);
}

void cfgADC(void){
	ADC_Init(LPC_ADC, ADC_FREQ);
	ADC_BurstCmd(LPC_ADC, DISABLE); // Deshabilitar modo Burst
	ADC_StartCmd(LPC_ADC, ADC_START_ON_MAT01); // Iniciar por Match1 del Timer 0
	ADC_EdgeStartConfig(LPC_ADC, ADC_START_ON_FALLING);
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
void DMA_SetupAndStart_CH7(void){
    // Reconfigura el canal DMA 7 (M2M) para asegurar que el contador de transferencia se reinicie.
	GPDMA_Channel_CFG_Type  cfgMEM_DMA_CH7;

	cfgMEM_DMA_CH7.ChannelNum = 7;
	cfgMEM_DMA_CH7.TransferSize = NUM_SAMPLES_ADC;
	cfgMEM_DMA_CH7.TransferType = GPDMA_TRANSFERTYPE_M2M;
	// Usar Half-Word (16 bits) ya que los buffers son uint16_t
	cfgMEM_DMA_CH7.TransferWidth = GPDMA_WIDTH_HALFWORD;
	cfgMEM_DMA_CH7.SrcMemAddr = (uint32_t)actual_adc_samples;
	cfgMEM_DMA_CH7.DstMemAddr = (uint32_t)last_samples_for_average;
	cfgMEM_DMA_CH7.SrcConn = 0;
	cfgMEM_DMA_CH7.DstConn = 0;
	cfgMEM_DMA_CH7.DMALLI = 0;

	// Configurar y habilitar el canal para una unica transferencia
	GPDMA_Setup(&cfgMEM_DMA_CH7);
	GPDMA_ChannelCmd(7, ENABLE);
	NVIC_EnableIRQ(DMA_IRQn);
}

void cfgDMA(void){
	NVIC_DisableIRQ(DMA_IRQn);
	GPDMA_Init();
	// No se configura el canal aqui, sino en DMA_SetupAndStart_CH7 para poder reusarlo.
}

uint16_t convert_adc_to_ppm(uint16_t raw_data){
	// El valor raw es de 12 bits (0-4095)
	if (raw_data == 0) return 0;

	// Voltaje de salida del sensor Vs = (raw_data / 4096) * 3.3V
	// Resistencia del sensor Rs = (3.3V - Vs) * RL / Vs
	// Resistencia del sensor Rs = RL_SENSOR * (4096 - raw_data) / raw_data

	float rs = (float) (4096.0f - raw_data) * RL_SENSOR /raw_data;

	// Relacion Rs/Ro
	float rs_ro_ratio = rs / R0_SENSOR;

//	float x = 100.0f * rs_ro_ratio;
	uint16_t concentracion_ppm = (uint16_t) 100 * pow(rs_ro_ratio, -1.52f);

	// Asegurar un valor positivo
	return (concentracion_ppm > 0) ? concentracion_ppm : 0;
}

uint16_t calc_average_ppm(void){
	uint16_t sum = 0;

	// Iteracion sobre el banco de memoria
	for(uint16_t inte = 0; inte < NUM_SAMPLES_ADC; inte++){
		uint16_t aux = convert_adc_to_ppm(last_samples_for_average[inte]);
		sum += aux;
	}
	uint16_t samples_average = (uint16_t) sum / NUM_SAMPLES_ADC;
	return samples_average;
}

void UART_SendChar_Safe(char c) {
    // Espera hasta que el registro de transmision (THR) este vacio, usando registro directo.
    while (!(LPC_UART2->LSR & UART_LSR_THRE));
    // Escribe directamente en el registro de datos
    LPC_UART2->THR = (uint8_t)c;
}

// Función manual para convertir uint16_t a string
static char *uint16_to_string_manual(uint16_t num, char *buffer) {
    int i = 0;
    if (num == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return buffer;
    }
    uint16_t temp = num;
    while (temp > 0) {
        buffer[i++] = (temp % 10) + '0';
        temp /= 10;
    }
    buffer[i] = '\0';

    int start = 0;
    int end = i - 1;
    char swap_char;

    while (start < end) {
        swap_char = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = swap_char;
        start++;
        end--;
    }
    return buffer;
}

// Función que utiliza UART_SendChar_Safe para transmitir
void UART_SendNumber_Safe(char prefix, uint16_t num){
    char tx_buffer[8];
    int j = 0;

    tx_buffer[j++] = prefix;

    uint16_to_string_manual(num, &tx_buffer[j]);

    while(tx_buffer[j] != '\0') {
        j++;
    }

    tx_buffer[j++] = '\n';

	for (int i = 0; i < j; i++) {
		UART_SendChar_Safe(tx_buffer[i]);
	}
}


// --- MANEJO DE INTERRUPCIONES ---

void ADC_IRQHandler(void){
    // Contador para mantener el estado de alarma critica
	static uint8_t alarm_counter = 0;
	static uint8_t sample_idx = 0; // Índice para llenar el buffer actual

	// Verificamos si la interrupción por canal 0 se disparó.
    if(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)){

    	// 1. CAPTURA Y ALMACENAMIENTO DE DATO
    	actual_adc_samples[sample_idx] = (ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0));
        // 2. CONVERSION y LÓGICA DE TIEMPO REAL
        last_adc_value_ppm = convert_adc_to_ppm(actual_adc_samples[sample_idx]);

        // 3. ACTUALIZAR ÍNDICE (Captura circular manual)
        sample_idx = (sample_idx + 1) % NUM_SAMPLES_ADC;

        // --- Logica de Alarmas ---
		if(last_adc_value_ppm < UMBRAL_PRECAUCION_PPM){
			// Estado SEGURO: LED Verde
			alarm_counter = 0;
			LPC_GPIO0 -> FIOSET |= (LED_VERDE);
			LPC_GPIO0 -> FIOCLR |= (LED_AMARILLO | LED_ROJO | BUZZER);
	        TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		} else if(alarm_counter >= MUESTRAS_PELIGROSAS_SEG){
			alarm_counter++;
			// Estado CRITICO: LED Rojo + Buzzer
			LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_AMARILLO);
			LPC_GPIO0 -> FIOSET |= (LED_ROJO);
			TIM_Cmd(LPC_TIM1, ENABLE); // Activar buzzer con PWM
			NVIC_EnableIRQ(TIMER1_IRQn);
		} else if(last_adc_value_ppm >= UMBRAL_PRECAUCION_PPM){
			// Estado PRECAUCION: LED Amarillo
			alarm_counter++; // Acumular lecturas de precaucion
			LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | BUZZER);
			LPC_GPIO0 -> FIOSET |= (LED_AMARILLO);
			TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		}

		// USO DE LA FUNCION UART SEGURA (HardFault Fix)
		if(status_flag){
			UART_SendNumber_Safe('P' ,samples_average_ppm);
		}else{
			UART_SendNumber_Safe('U', last_adc_value_ppm);
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

void TIMER2_IRQHandler(void){
    if(TIM_GetIntStatus(LPC_TIM2, TIM_MR0_INT)){
        if(status_flag == 0){
        	status_flag = 1;
			TIM_UpdateMatchValue(LPC_TIM2, 0, (uint32_t)TIEMPO_MUESTRA_PROMEDIO);
			TIM_ResetCounter(LPC_TIM2);
			TIM_Cmd(LPC_TIM2, ENABLE);

			DMA_SetupAndStart_CH7(); // Reinicia la transferencia de los datos
        } else{
        	status_flag = 0;
			TIM_UpdateMatchValue(LPC_TIM2, 0, (uint32_t)TIEMPO_TRANSMISION_VIVO);
			TIM_ResetCounter(LPC_TIM2);
			TIM_Cmd(LPC_TIM2, ENABLE);
        }
        TIM_ClearIntPending(LPC_TIM2, TIM_MR0_INT);
    }
}

void DMA_IRQHandler(void){
	if(GPDMA_IntGetStatus(GPDMA_STAT_INTTC, 7)){
		samples_average_ppm = calc_average_ppm();
		GPDMA_ClearIntPending(GPDMA_STAT_INTTC, 7);
		GPDMA_ChannelCmd(7, DISABLE);
	}
}
