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

#define LED_ROJO (1<<2)
#define LED_VERDE (1<<27)
#define LED_AMARILLO (1<<3)
#define BUZZER (1<<22)
#define freq 20000 //Ver valor de frecuencia del adc
#define UMBRAL_SEGURO    1400   // Ajustar según calibración
#define UMBRAL_PRECAUCION 2100 // Ajustar según calibración

volatile uint16_t adc_value = 0;
volatile uint8_t adc_ready = 0; //Flag para procesar.
volatile uint8_t flagt = 0;
volatile uint8_t prueba = 0;


void cfgPin(uint8_t port_num, uint8_t pin_num, uint8_t func_num);
void pinConfiguration(void);
void cfgTimer(void);
void cfgADC(void);
void cfgUART(void);
//
void UART_SendString(char *str); //Funcion para mandar string por UART
void UART_SendNumber(uint16_t num); //Funcion para mandar las mediciones del sensor


int main(){

	pinConfiguration();

	//UART_SendString("Sistema iniciado\r\n");
    // Indicación visual de inicio: parpadeo rápido de todos los LEDs
    for(uint8_t i = 0; i < 3; i++){
    	LPC_GPIO0 -> FIOSET |= (LED_VERDE | LED_ROJO | LED_AMARILLO);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
        LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | LED_AMARILLO);
        for(volatile uint32_t j = 0; j < 1000000; j++); // Delay
    }

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
	cfgPin(0, 23, 1); //AD0.0 pin
	cfgPin(1, 29, 3); //MAT0.1 pin para inicializacion del ADC
	cfgPin(0, 10, 1); // TXD2 pin (UART2)
	cfgPin(0,22,0);
	cfgPin(1,22,3);
	cfgPin(1,25,3);
	cfgPin(3,25,0);

	LPC_GPIO0 -> FIODIR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER );
	LPC_GPIO0 -> FIOCLR |= (LED_VERDE | LED_ROJO | LED_AMARILLO | BUZZER );
	LPC_GPIO3 -> FIODIR |= (1<<25);
	LPC_GPIO3 -> FIOCLR |= (1<<25);
}

void cfgTimer(void){
	TIM_TIMERCFG_Type timerMode0;
	timerMode0.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode0.PrescaleValue = 1000; //Base de tiempo de desborde de 1ms

	TIM_MATCHCFG_Type timerMAT01; // Para iniciar ADC
	timerMAT01.MatchChannel = 1;
	timerMAT01.IntOnMatch = ENABLE;
	timerMAT01.StopOnMatch = DISABLE;
	timerMAT01.ResetOnMatch = ENABLE;
	timerMAT01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE;
	timerMAT01.MatchValue = 500 ;

	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerMode0);
	TIM_ConfigMatch(LPC_TIM0, &timerMAT01);
	TIM_Cmd(LPC_TIM0, ENABLE);

	NVIC_EnableIRQ(TIMER0_IRQn);

	TIM_TIMERCFG_Type timerMode1;
	timerMode1.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode1.PrescaleValue = 1000; //Base de tiempo de desborde de 1ms

	TIM_MATCHCFG_Type timerMAT10; // Periodo PWM para senalizacion acustica
	timerMAT10.MatchChannel = 0;
	timerMAT10.IntOnMatch = ENABLE;
	timerMAT10.StopOnMatch = DISABLE;
	timerMAT10.ResetOnMatch = ENABLE;
	timerMAT10.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
	timerMAT10.MatchValue = 500; //freq= 2kHz

	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
	TIM_Cmd(LPC_TIM1, ENABLE);
}

void cfgADC(void){
	ADC_Init(LPC_ADC, freq);
	ADC_BurstCmd(LPC_ADC, DISABLE);
	ADC_StartCmd(LPC_ADC, ADC_START_ON_MAT01);
	ADC_EdgeStartConfig(LPC_ADC, ADC_START_ON_RISING);
	ADC_ChannelCmd(LPC_ADC, ADC_CHANNEL_0, ENABLE);
	ADC_IntConfig(LPC_ADC, ADC_ADINTEN0, ENABLE);

	NVIC_EnableIRQ(ADC_IRQn);
}

void cfgUART(void){
	UART_CFG_Type pinUART2; //Valor por defecto 9600 baudios, 8 bits de datos, sin partidad y 1 bit de parada.
	UART_FIFO_CFG_Type UARTFIFO;
	pinUART2.Baud_rate = 9600;
	UART_ConfigStructInit(&pinUART2);
	UART_Init(LPC_UART2, &pinUART2);
	UART_FIFOConfigStructInit(&UARTFIFO);
	UART_FIFOConfig(LPC_UART2, &UARTFIFO);
	UART_IntConfig(LPC_UART2, UART_INTCFG_RBR, DISABLE);
	UART_TxCmd(LPC_UART2, ENABLE);
}

// Definición de la función de conversión
void intToStr(uint8_t num, uint8_t buffer[]) {
    int i = 0;

    // Manejar el caso especial de 0
    if (num == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    // Procesar dígitos en orden inverso
    while (num > 0) {
        buffer[i++] = (num % 10) + '0'; // Convierte el dígito a su caracter ASCII
        num /= 10;
    }

    // Invertir la cadena (ej: 0051 -> 1500)
    int len = i;
    for (int j = 0; j < len / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[len - 1 - j];
        buffer[len - 1 - j] = temp;
    }

    // Agregar el terminador nulo \0 (necesario para UART_SendString)
    buffer[len] = '\0';
}

void ADC_IRQHandler(void){
	static uint8_t cont = 0;
    if(ADC_ChannelGetStatus(LPC_ADC, ADC_CHANNEL_0, ADC_DATA_DONE)){
        adc_value = (uint16_t) (ADC_ChannelGetData(LPC_ADC, ADC_CHANNEL_0)-400) * 3300 / 4096;
            // Lógica de umbrales
		if(adc_value < UMBRAL_PRECAUCION){
			// Estado seguro - LED verde
			cont = 0;
			LPC_GPIO0 -> FIOSET |= (LED_VERDE);
			LPC_GPIO0 -> FIOCLR |= (LED_AMARILLO);
			LPC_GPIO0 -> FIOCLR |= (LED_ROJO);
			LPC_GPIO0 -> FIOCLR |= (BUZZER);
	        TIM_Cmd(LPC_TIM1, DISABLE); // Apagar buzzer
			NVIC_DisableIRQ(TIMER1_IRQn);
		} else if(cont >= 5){
			// Crítico - LED rojo + buzzer
			LPC_GPIO0 -> FIOCLR |= (LED_VERDE);
			LPC_GPIO0 -> FIOCLR |= (LED_AMARILLO);
			LPC_GPIO0 -> FIOSET |= (LED_ROJO);
			TIM_Cmd(LPC_TIM1, ENABLE); // Activar buzzer con PWM
			NVIC_EnableIRQ(TIMER1_IRQn);
		}else if(adc_value > UMBRAL_PRECAUCION){
			cont ++;
			// Precaución - LED amarillo
			LPC_GPIO0 -> FIOCLR |= (LED_VERDE);
			LPC_GPIO0 -> FIOSET |= (LED_AMARILLO);
			LPC_GPIO0 -> FIOCLR |= (LED_ROJO);
			LPC_GPIO0 -> FIOCLR |= (BUZZER);
			TIM_Cmd(LPC_TIM1, DISABLE);
			NVIC_DisableIRQ(TIMER1_IRQn);
		}
    }
}

void TIMER1_IRQHandler(void){
    if(TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)){
        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
        if(flagt == 0){
			LPC_GPIO0 -> FIOSET |= (BUZZER);
			flagt = 1;
			return;
        }else if(flagt == 1){
            LPC_GPIO0 -> FIOCLR |= (BUZZER);
            flagt = 0;
        }
    }
}

void TIMER0_IRQHandler(void){
    if(TIM_GetIntStatus(LPC_TIM0, TIM_MR1_INT)){
        TIM_ClearIntPending(LPC_TIM0, TIM_MR1_INT);
        LPC_GPIO0 -> FIOSET |= (BUZZER);
		UART_SendNumber(adc_value);
		LPC_GPIO0 -> FIOCLR |= (BUZZER);
    }
}

void UART_SendString(char *str){
    while(*str){
        UART_SendByte(LPC_UART2, *str++);
    }
}


void UART_SendNumber(uint16_t num){
    // Búfer para la cadena: 5 dígitos máx. (65535) + \n + \0 = 7
    uint8_t buffer[6];
    uint8_t var = (uint8_t) ((4.13f * num + 531.2f) / 1980);

    // 1. Convertir el número (entero) a cadena en el búfer
    intToStr(var, buffer);

    // 2. Agregar el delimitador \n al final
    // Encontramos el final de la cadena (donde está \0)
    int len = 0;
    while (buffer[len] != '\0') {
        len++;
    }

    // Reemplazar \0 por \n y agregar el nuevo terminador
    buffer[len] = '\n';
    buffer[len + 1] = '\0';

    // 3. Enviar la cadena completa (ej: "1500\n")
    //UART_SendString(buffer);
    UART_Send(LPC_UART2, buffer, sizeof(buffer), BLOCKING);
}
//

