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

#define UMBRAL_PRECAUCION_PPM   4   	// Ajustar segun calibracion
#define R0_SENSOR				86.19  	// Resistencia en kOhm del sensor a 100ppm de CO en aire limpio
#define RL_SENSOR				10 		// Resistencia de carga en kOhm del sensor

#define BUFFER0_START	0x2007C000
#define BUFFER1_START	(BUFFER0_START + (NUM_SAMPLES_ADC*sizeof(uint32_t)))

volatile uint8_t 	flag_buzzer_toggle = 0;
volatile uint8_t 	status_flag = 0;

volatile uint32_t 	*actual_adc_samples = (volatile uint32_t *) BUFFER0_START;
volatile uint16_t 	last_adc_value_ppm = 0;
volatile uint32_t 	*last_samples = (volatile uint32_t *) BUFFER1_START;
volatile uint16_t 	samples_average_ppm = 0;


void cfgPin(uint8_t port_num, uint8_t pin_num, uint8_t func_num);
void pinConfiguration(void);
void cfgTimer(void);
void cfgADC(void);
void cfgUART(void);
void cfgDMA(void);
void cfgEXTI(void);

uint16_t calc_average_ppm(void);
uint16_t convert_adc_to_ppm(uint16_t raw_data);


void UART_SendNumber(char prefix, uint16_t num); 			//Funcion para mandar las mediciones del sensor

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
//    cfgEXTI();
	cfgDMA();
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
	timerMAT01.MatchValue = 500 ; // Interrumpe y dispara ADC cada 500ms (2Hz)

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
	timerMAT10.MatchValue = 500; // Genera interrupción cada 500ms

	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
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

void cfgDMA(void){
	GPDMA_Channel_CFG_Type  cfgADC_DMA_CH0;
	GPDMA_LLI_Type			cfgADC_LLI0;

	NVIC_DisableIRQ(DMA_IRQn);
	GPDMA_Init();

	cfgADC_LLI0.SrcAddr = (uint32_t)&(LPC_ADC->ADDR0);
	cfgADC_LLI0.DstAddr = (uint32_t)actual_adc_samples;
	cfgADC_LLI0.NextLLI = (uint32_t)&cfgADC_LLI0;
	cfgADC_LLI0.Control = (NUM_SAMPLES_ADC << 0) 	// Tamano de transferencia
							| (1 << 18) 		// Ancho de palabra en fuente 16 bits
							| (1 << 21) 		// Ancho de palabra en destino 16 bits
							& ~(1 << 26) 		// Sin incremento en fuente
							| (1 << 27); 		// Incremento en destino

	cfgADC_DMA_CH0.ChannelNum = 0;
	cfgADC_DMA_CH0.TransferSize = NUM_SAMPLES_ADC;
	cfgADC_DMA_CH0.TransferType = GPDMA_TRANSFERTYPE_P2M;
	cfgADC_DMA_CH0.TransferWidth = 0;
	cfgADC_DMA_CH0.SrcMemAddr = 0;
	cfgADC_DMA_CH0.DstMemAddr = (uint32_t)actual_adc_samples;
	cfgADC_DMA_CH0.SrcConn = GPDMA_CONN_ADC;
	cfgADC_DMA_CH0.DstConn = 0;
	cfgADC_DMA_CH0.DMALLI = (uint32_t)&cfgADC_LLI0;

	GPDMA_Setup(&cfgADC_DMA_CH0);

	GPDMA_Channel_CFG_Type  cfgMEM_DMA_CH7;

	cfgMEM_DMA_CH7.ChannelNum = 7;
	cfgMEM_DMA_CH7.TransferSize = NUM_SAMPLES_ADC;
	cfgMEM_DMA_CH7.TransferType = GPDMA_TRANSFERTYPE_M2M;
	cfgMEM_DMA_CH7.TransferWidth = GPDMA_WIDTH_WORD;
	cfgMEM_DMA_CH7.SrcMemAddr = (uint32_t)actual_adc_samples;
	cfgMEM_DMA_CH7.DstMemAddr = (uint32_t)last_samples;
	cfgMEM_DMA_CH7.SrcConn = 0;
	cfgMEM_DMA_CH7.DstConn = 0;
	cfgMEM_DMA_CH7.DMALLI = 0;

	GPDMA_Setup(&cfgMEM_DMA_CH7);
	GPDMA_ChannelCmd(7, DISABLE);
}

void cfgEXTI(void){
	PINSEL_CFG_Type pin;
	pin.Portnum = 2;
	pin.Pinnum = 10;
	pin.Funcnum = 1;
	pin.Pinmode = PINSEL_PINMODE_PULLUP;
	pin.OpenDrain = PINSEL_PINMODE_NORMAL;
	PINSEL_ConfigPin(&pin);

	EXTI_InitTypeDef cfgEINT0;

	EXTI_Init();

	cfgEINT0.EXTI_Line = EXTI_EINT0;
	cfgEINT0.EXTI_Mode = EXTI_MODE_EDGE_SENSITIVE;
	cfgEINT0.EXTI_polarity = EXTI_POLARITY_LOW_ACTIVE_OR_FALLING_EDGE;
	EXTI_Config(&cfgEINT0);
	NVIC_EnableIRQ(EINT0_IRQn);
}

uint16_t convert_adc_to_ppm(uint16_t raw_data){
	// El valor raw es de 12 bits (0-4095)
	if (raw_data == 0) return 0;

	// Voltaje de salida del sensor Vs = (raw_data / 4096) * 3.3V
	// Resistencia del sensor Rs = (3.3V - Vs) / (Vs / RL_SENSOR)
	// Resistencia del sensor Rs = RL_SENSOR * (4096 - raw_data) / raw_data
	float rs = (float) RL_SENSOR * (4096 - raw_data) / raw_data;

	// Relacion Rs/Ro
	float rs_ro_ratio = rs / R0_SENSOR;

	uint16_t concentracion_ppm = (uint16_t) 100.0f * pow(rs_ro_ratio, -1.52f);

	// Asegurar un valor positivo
	return (concentracion_ppm > 0) ? concentracion_ppm : 0;
}

uint16_t calc_average_ppm(void){
	uint32_t sum = 0;

	// Iteracion sobre el banco de memoria
	for(uint16_t inte = 0; inte < NUM_SAMPLES_ADC; inte++){
		// Extraccion del valor del ADC de 12 bits de la palabra de 32 bits (bits 4-15)
		sum += (*(last_samples + inte) >> 4) & 0x0FFF;
	}
	uint16_t samples_average = (uint16_t) sum / NUM_SAMPLES_ADC;
	return convert_adc_to_ppm(samples_average);
}


// --- MANEJO DE INTERRUPCIONES ---

void ADC_IRQHandler(void){
    // Contador para mantener el estado de alarma crítica
	static uint8_t alarm_counter = 0;

    if(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)){

        uint16_t raw_adc = (uint16_t)((ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0)));// >> 4) & 0x0FFF);
        last_adc_value_ppm = convert_adc_to_ppm(raw_adc);
        // --- Logica de Alarmas ---
		if(last_adc_value_ppm < UMBRAL_PRECAUCION_PPM){
			// Estado SEGURO: LED Verde
			alarm_counter = 0;
			LPC_GPIO0 -> FIOSET |= (LED_VERDE);
			LPC_GPIO0 -> FIOCLR |= (LED_AMARILLO | LED_ROJO | BUZZER);
	        TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		} else if(alarm_counter >= 7){
			alarm_counter++;
			if(alarm_counter >= 50){
				status_flag = 1;
				NVIC_EnableIRQ(DMA_IRQn);
				GPDMA_ChannelCmd(7, ENABLE);
			}
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

		if(status_flag){
			UART_SendNumber('P' ,samples_average_ppm);
		}else{
			UART_SendNumber('U', last_adc_value_ppm);
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

void EINT0_IRQHandler(void){
	if(status_flag){
		status_flag = 0;
		NVIC_DisableIRQ(DMA_IRQn);
		GPDMA_ChannelCmd(7, DISABLE);
		GPDMA_ChannelCmd(0, ENABLE);
	}else{
		status_flag = 1;
		NVIC_EnableIRQ(DMA_IRQn);
		GPDMA_ChannelCmd(0, DISABLE);
		GPDMA_ChannelCmd(7, ENABLE);
	}
	LPC_SC->EXTINT |= (1<<0);
}

void DMA_IRQHandler(void){
	if(GPDMA_IntGetStatus(GPDMA_STAT_INTTC, 7)){
		samples_average_ppm = calc_average_ppm();
		GPDMA_ClearIntPending(GPDMA_STAT_INTTC, 7);
	}
}

// --- FUNCIONES DE CONVERSIÓN Y UART ---

// Envia el valor de concentracion al ESP8266
void UART_SendNumber(char prefix, uint16_t num){
	// Búfer seguro para Prefijo + Valor + \n + \0
    char tx_buffer[10];

    // Usamos sprintf para formatear y concatenar la cadena: Prefijo + Valor Entero + \n
    int len = sprintf(tx_buffer, "%c%u\n", prefix, num);

    // Enviar la cadena completa (Prefijo + Dígitos + '\n').
	if (len > 0) {
		UART_Send(LPC_UART2, (uint8_t*)tx_buffer, len, BLOCKING);
	}
}
