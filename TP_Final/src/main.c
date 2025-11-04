/*
Togglea el led rojo integrado cada 1 segundo usando el Timer0 del LPC1769.
*/

#include "LPC17xx.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_gpio.h"

#define LED_ROJO ()
#define LED_VERDE ()
#define LED_AMARILLO ()

void cfgPin(uint8_t port_num, uint8_t pin_num, uint8_t func_num);
void pinConfiguration(void);
void cfgTimer(void);

int main(){


	pinConfiguration();


	while(1){};
}


void cfgPin(uint8_t port, uint8_t pin, uint8_t func){
	PINSEL_CFG_Type pin;
	pin.Portnum = port;
	pin.Pinnum = pin;
	pin.Funcnum = func;
	pin.Pinmode = PINSEL_PINMODE_TRISTATE;
	pin.OpenDrain = PINSEL_PINMODE_NORMAL;
	PINSEL_ConfigPin(&pin);
}

void pinConfiguration(void){
	cfgPin(0, 23, 1); //AD0.0 pin
	cfgPin(1, 29, 3); //MAT0.1 pin para inicializacion del ADC
	cfgPin(0, 2, 1); //TXD0 pin
	for(uint8_t inte = 3; inte < 7; inte++){
		GPIO_SetDir(0, inte, 1); //GPIO pins 0.3-0.6 para senalizacion optica y acustica
	}
}

void cfgTimer(void){
	TIM_TIMERCFG_Type timerMode0;
	timerMode0.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode0.PrescaleValue = 1000; //Base de tiempo de desborde de 1ms

	TIM_MATCHCFG_Type timerMAT01; // Para iniciar ADC
	timerMAT01.MatchChannel = 1;
	timerMAT01.IntOnMatch = DISABLE;
	timerMAT01.StopOnMatch = DISABLE;
	timerMAT01.ResetOnMatch = ENABLE;
	timerMAT01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE;
	timerMAT01.MatchValue = ;

	TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &timerMode0);
	TIM_ConfigMatch(LPC_TIM0, &timerMAT01);
	TIM_Cmd(LPC_TIM0, ENABLE);

	TIM_TIMERCFG_Type timerMode1;
	timerMode1.PrescaleOption = TIM_PRESCALE_USVAL;
	timerMode1.PrescaleValue = 1000; //Base de tiempo de desborde de 1ms

	TIM_MATCHCFG_Type timerMAT10; // Periodo PWM para senalizacion acustica
	timerMAT10.MatchChannel = 0;
	timerMAT10.IntOnMatch = ENABLE;
	timerMAT10.StopOnMatch = DISABLE;
	timerMAT10.ResetOnMatch = ENABLE;
	timerMAT10.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
	timerMAT10.MatchValue = ;

	TIM_MATCHCFG_Type timerMAT11; // Ciclo de trabajo PWM para senalizacion acustica
	timerMAT11.MatchChannel = 1;
	timerMAT11.IntOnMatch = ENABLE;
	timerMAT11.StopOnMatch = ENABLE;
	timerMAT11.ResetOnMatch = DISABLE;
	timerMAT11.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
	timerMAT11.MatchValue = ;

	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &timerMode1);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT10);
	TIM_ConfigMatch(LPC_TIM1, &timerMAT11);
	TIM_Cmd(LPC_TIM1, ENABLE);

	NVIC_IRQHandler(TIMER1_IRQn);
}
