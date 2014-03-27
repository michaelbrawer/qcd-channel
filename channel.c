#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
/*
  FUSES =
    {
        .low = 0xe2,
	.high = 0xdf
    };
*/

/***********************
 *  COMPILE SETTINGS   *
 ***********************/

//#define DEBUG


// Set FREERUN to 1 to allow freerunning clock (output clock doesn't stop when incoming clock stops)
// Set FREERUN to 0 to disable freerunning clock (output will stop when input stops)
#define FREERUN 1


/************************
 * Mnuemonics		*
************************/


#define MIN_PW 10000


#define DIV_ADC_DRIFT 2
#define PW_ADC_DRIFT 3

#define USER_INPUT_POLL_TIME 100

/*******************
 * PIN DEFINITIONS *
 *******************/

#ifdef DEBUG
#define DEBUG_pin PB0
#define DEBUG_init DDRB |= (1<<DEBUG_pin)
#define DEBUGFLIP PORTB ^= (1<<DEBUG_pin)
#define DEBUGHIGH PORTB |= (1<<DEBUG_pin)
#define DEBUGLOW PORTB &= ~(1<<DEBUG_pin)
#endif

#define RESET_pin PB0
#define RESET_init DDRB &= ~(1<<RESET_pin); PORTB &= ~(1<<RESET_pin)
#define RESET (PINB & (1<<RESET_pin))

#define PING_pin PB2
#define PING_init DDRB &= ~(1<<PING_pin); PORTB &= ~(1<<PING_pin)
#define PING ((PINB & (1<<PING_pin)))

#define CLKOUT_pin PB1
#define CLKOUT_init DDRB |= (1 << CLKOUT_pin)
#define CLKOUT_ON PORTB |= (1 << CLKOUT_pin)
#define CLKOUT_OFF PORTB &= ~(1 << CLKOUT_pin)

#define ADC_DDR DDRB
#define ADC_PORT PORTB
#define ADC_mask ((1<<3) | (1<<2))
#define ADC_pins ((1<<PB3) | (1<<PB4))
#define ADC_DIVMULT 3
#define ADC_PW 2
#define DID_pins ((1<<ADC2D) | (1<<ADC3D))

/********************
 * GLOBAL VARIABLES *
 ********************/


volatile char timer_overflowed=0;

extern uint32_t udiv32(uint32_t divisor);
volatile uint32_t tmr_ping=0;
volatile uint32_t tmr_clkout=0;
volatile uint32_t tmr_reset=0;

volatile uint32_t ping_irq_timestamp=0;


SIGNAL (TIMER0_OVF_vect){
	tmr_ping++;
	tmr_reset++;
	tmr_clkout++;
}

uint32_t get_tmr_clkout(void){
	uint32_t result;
	cli();
	result = (tmr_clkout << 8) | TCNT0;
	sei();
	return result;
}
uint32_t get_tmr_ping(void){
	uint32_t result;
	cli();
	result = (tmr_ping << 8) | TCNT0;
	sei();
	return result;
}
uint32_t get_tmr_reset(void){
	uint32_t result;
	cli();
	result = (tmr_reset << 8) | TCNT0;
	sei();
	return result;
}



void inittimer(void){
	cli();
	//Normal mode, TOP at 0xFF, OC0A and OC0B disconnected, Prescale @ FCK/8
	TCCR0A=(0<<WGM01) | (0<<WGM00) ;
	TCCR0B= (0<<WGM02) | (0<<CS00) | (1<<CS01) | (0<<CS02);

	TCNT0=0;

	TIMSK |= (1<<TOIE0); 
	tmr_clkout=0;
	tmr_ping=0;
	tmr_reset=0;
						// Enable timer overflow interrupt
	sei();
}


void init_pins(void){
	PING_init;
	CLKOUT_init;
	RESET_init;
#ifdef DEBUG_init
	DEBUG_init;
#endif
}

void init_adc(void){
	ADC_DDR &= ~(ADC_mask); //adc input
	ADC_PORT &= ~(ADC_mask); //disable pullup
	DIDR0 = DID_pins;
	ADCSRA = (1<<ADEN);	//Enable ADC
	ADMUX = (1<<ADLAR) | (ADC_DIVMULT);	//Left-Adjust, MUX to the ADC_pin
	ADCSRA |= (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0); //prescale = clk/128 = 125kHz
	ADCSRA |= (1<<ADSC);//set the Start Conversion Flag in the ADC Status Register
}

SIGNAL (INT0_vect){
	if (PING){
		ping_irq_timestamp = (tmr_ping << 8) | TCNT0;
		tmr_ping=0;
	}
}

void init_extinterrupt(void){
	MCUCR = (1<<ISC00) | (1<<ISC01); //enable the external interrupt for Rising Edge on pin INT0 (PB2)
	GIMSK = (1<<INT0); //enable interrupt for external int
}

int8_t get_clk_div_nominal(uint8_t adc_val){
	if (adc_val<=5) 	 // /32
		return(32);
	else if (adc_val<=17) // /16
		return(16);
	else if (adc_val<=33) // /8
		return(8);
	else if (adc_val<=47) // /7
		return(7);
	else if (adc_val<=62) // /6
		return(6);
	else if (adc_val<=76) // /5
		return(5);
	else if (adc_val<=90) // /4
		return(4);
	else if (adc_val<=104) // /3
		return(3);
	else if (adc_val<=118) // /2
		return(2);
	else if (adc_val<=131) // =1
		return(1);
	else if (adc_val<=144) // x2
		return(-2);	
	else if (adc_val<=158) // x3
		return(-3);	
	else if (adc_val<=172) // x4
		return(-4);
	else if (adc_val<=186) // x5
		return(-5);
	else if (adc_val<=200) // x6
		return(-6);
	else if (adc_val<=215) // x7
		return(-7);
	else if (adc_val<=229) // x8
		return(-8);
	else if (adc_val<=242) // x12
		return(-12);
	else  					// x16
		return(-16);

}
uint32_t get_clk_div_time(int8_t clock_divide_amount, uint32_t clk_time){

	if (clock_divide_amount==64)  // /64
		return(clk_time<<6);
	else if (clock_divide_amount==32) // /32
		return(clk_time<<5);
	else if (clock_divide_amount==16) // /16
		return(clk_time<<4);
	else if (clock_divide_amount==12) // /12
		return(clk_time*12);
	else if (clock_divide_amount==8) // /8
		return(clk_time<<3);
	else if (clock_divide_amount==7) // /7
		return(clk_time*7);
	else if (clock_divide_amount==6) // /6
		return(clk_time*6);
	else if (clock_divide_amount==5) // /5
		return(clk_time*5);
	else if (clock_divide_amount==4) // /4
		return(clk_time<<2);
	else if (clock_divide_amount==3) // /3
		return(clk_time*3);
	else if (clock_divide_amount==2) // /2
		return(clk_time<<1);
	else if (clock_divide_amount==1) // =1
		return(clk_time);
	else if (clock_divide_amount==-1) // =1
		return(clk_time);
	else if (clock_divide_amount==-2) // *2
		return(clk_time>>1);
	else if (clock_divide_amount==-3) // *3
		return(clk_time/3);
	else if (clock_divide_amount==-4) // *4
		return(clk_time>>2);
	else if (clock_divide_amount==-5) // *5
		return(clk_time/5);
	else if (clock_divide_amount==-6) // *6
		return(clk_time/6);
	else if (clock_divide_amount==-7) // *7
		return(clk_time/7);
	else if (clock_divide_amount==-8) // *8
		return(clk_time>>3);
	else if (clock_divide_amount==-12) // *8
		return(clk_time/12);
	else if (clock_divide_amount==-16) // *16
		return((clk_time>>4) + 100);
	else if (clock_divide_amount==-32) // *32
		return(clk_time>>5);
	else if (clock_divide_amount==-64) // *64
			return(clk_time>>6);
		
	else return(clk_time);

}


uint32_t calc_pw(uint8_t pw_adc, uint32_t period){
	uint32_t t;

	pw_adc=255-pw_adc;

	if (pw_adc<4) t=(period>>6); //1.0625%
	else if (pw_adc<14) t=(period>>5); //3.125%
	else if (pw_adc<24) t=(period>>4); //6.25%
		//	else if (pw_adc<34) t=((period>>4)+(period>>6)); //7.8125%
	else if (pw_adc<34) t=((period>>4)+(period>>5)); //9.375%
		//	else if (pw_adc<44) t=((period>>4)+(period>>5)+(period>>6)); //10.9375%
	else if (pw_adc<44) t=(period>>3); //12.5%
	else if (pw_adc<54) t=((period>>3)+(period>>5)); //15.5%
	else if (pw_adc<64) t=((period>>3)+(period>>4)); //18.75%
	else if (pw_adc<74) t=((period>>3)+(period>>4)+(period>>5)); //21.875%
	else if (pw_adc<85) t=(period>>2); //25%
	else if (pw_adc<94) t=((period>>2)+(period>>4)); //31.25%
	else if (pw_adc<104) t=((period>>2)+(period>>3)); //37.5%
	else if (pw_adc<114) t=((period>>2)+(period>>3)+(period>>4)); //43.75%

	else if (pw_adc<140) t=(period>>1); //50%

	else if (pw_adc<150) t=((period>>1)+(period>>5)); //53.125%
	else if (pw_adc<160) t=((period>>1)+(period>>4)); //56.25%
	else if (pw_adc<170) t=((period>>1)+(period>>4)+(period>>5)); //59.375%
	else if (pw_adc<180) t=((period>>1)+(period>>3)); //62.5%
	else if (pw_adc<190) t=((period>>1)+(period>>3)+(period>>5)); //65.5%
	else if (pw_adc<200) t=((period>>1)+(period>>3)+(period>>4)); //68.75%
	else if (pw_adc<210) t=((period>>1)+(period>>3)+(period>>4)+(period>>5)); //71.875%
	else if (pw_adc<220) t=((period>>1)+(period>>2)); //75%
	else if (pw_adc<230) t=((period>>1)+(period>>2)+(period>>4)); //81.25%
	else if (pw_adc<240) t=((period>>1)+(period>>2)+(period>>3)); //87.5%
	else if (pw_adc<250) t=((period>>1)+(period>>2)+(period>>3)+(period>>4)); //93.75%
	else t=period-(period>>5); //96.875%

	if (period>(30000)){   //period is at least 30ms (lower than 33Hz) so we should use MIN_PW as a min/max
		if (pw_adc<4 || t<MIN_PW) t=MIN_PW;
		if (pw_adc>=250 || t>(period-MIN_PW)) t=period-MIN_PW; 

	}

	return(t);

}

/***************************************************
 *             MAIN() FUNCTION                     *
 *                                                 *
 ***************************************************/


int main(void){

	uint32_t ping_time=0,divclk_time=0,pw_time=0;
	uint32_t now=0;
	uint32_t t=0;
	uint32_t reset_offset_time=0;
	int poll_user_input=0;

	uint8_t reset_up=0,reset_now_flag=0,ready_to_reset=1;
	uint8_t num_pings_since_reset[9]={0,0,0,0,0,0,0,0,0};
	uint8_t cda[9]={2,3,4,5,6,7,8,16,32};
	uint8_t i;

	uint8_t adch=127;
	uint8_t divmult_adc=127;
	uint8_t pw_adc=127;

	int8_t clock_divide_amount=1;
	int8_t old_clock_divide_amount=1;

	uint8_t cur_adc=ADC_DIVMULT;


	/** Initialize **/


	inittimer();
	init_pins();
	init_extinterrupt();
	init_adc();

	_delay_ms(5);

	CLKOUT_OFF;


	/** Main loop **/
	while(1){


		if (PING && ping_irq_timestamp){

			ping_time=ping_irq_timestamp;
			ping_irq_timestamp=0;

			cli();
				tmr_reset=0;
			sei();

			clock_divide_amount=get_clk_div_nominal(divmult_adc);
			divclk_time=get_clk_div_time(clock_divide_amount,ping_time);
			pw_time=calc_pw(pw_adc,divclk_time);

			if (clock_divide_amount<=1){ 	//multiplying 
				ready_to_reset=1;
			}

			for (i=0;i<9;i++){
				num_pings_since_reset[i]++;
				if (num_pings_since_reset[i]>=cda[i]){
					if (clock_divide_amount==cda[i]) ready_to_reset=1;
					num_pings_since_reset[i]=0;
				}
			}

		} else { //(ping_irq_timestamp)
			if (!FREERUN && (get_tmr_reset() > (ping_time<<1))) {
				//incoming clock has stopped
				divclk_time=0;
				reset_offset_time=0;
				num_pings_since_reset[0]=0;num_pings_since_reset[1]=0;
				num_pings_since_reset[2]=0;num_pings_since_reset[3]=0;
				num_pings_since_reset[4]=0;num_pings_since_reset[5]=0;
				num_pings_since_reset[6]=0;num_pings_since_reset[7]=0;
				num_pings_since_reset[8]=0;		
			}
		}


		if (RESET){
			if (!reset_up){
				CLKOUT_OFF; //goes off for 10uS
				num_pings_since_reset[0]=0;num_pings_since_reset[1]=0;
				num_pings_since_reset[2]=0;num_pings_since_reset[3]=0;
				num_pings_since_reset[4]=0;num_pings_since_reset[5]=0;
				num_pings_since_reset[6]=0;num_pings_since_reset[7]=0;
				num_pings_since_reset[8]=0;	
				reset_offset_time=get_tmr_reset(); //time elapsed since last ping
				reset_now_flag=1;
				reset_up=1;
				ready_to_reset=0;
			}
		}	else {
			reset_up=0;
		}
		
		if (clock_divide_amount>1){ //dividing
			if (reset_offset_time>divclk_time) {
				reset_offset_time=0;
			}
		} else { //multiplying
			if (reset_offset_time>ping_time) {
				reset_offset_time=0;
			}
		}

		if (((get_tmr_reset()) >= reset_offset_time) && ready_to_reset) {
				reset_now_flag=1;
				ready_to_reset=0;
		}


		if (divclk_time){

			if (reset_now_flag){
				reset_now_flag=0;
				cli();
					tmr_clkout=0;
				sei();
				CLKOUT_ON;
			}

			now=get_tmr_clkout();

			if (now>=pw_time){
				CLKOUT_OFF;
			}
			if (now>divclk_time){
				t=(now-divclk_time)>>8;
				cli();
					tmr_clkout=t;
				sei();

				CLKOUT_ON;
			}


		} else {
			CLKOUT_OFF;
		}



		/***************** READ ADC ****************/
	


		if ((++poll_user_input>USER_INPUT_POLL_TIME) && (ADCSRA & (1<<ADIF))){
		
			poll_user_input=0;

			/** READ ADC **/

			ADCSRA |= (1<<ADIF);		// Clear the flag by sending a logical "1"
			adch=ADCH;

			if (cur_adc==ADC_DIVMULT){

				cur_adc=ADC_PW;
				ADMUX = (1<<ADLAR) | cur_adc;	//Setup for next conversion
				ADCSRA |= (1<<ADSC);			//Start Conversion

				if (divmult_adc>adch) t=divmult_adc-adch;
				else t=adch-divmult_adc;

				if (t>=DIV_ADC_DRIFT){
					divmult_adc=adch;
					old_clock_divide_amount=clock_divide_amount;

					clock_divide_amount=get_clk_div_nominal(divmult_adc);
					divclk_time=get_clk_div_time(clock_divide_amount,ping_time);
					//if (!output_is_high)
						pw_time=calc_pw(pw_adc,divclk_time);

					if (clock_divide_amount==-16 && old_clock_divide_amount!=-16)
						reset_offset_time=0;
				}


			} else {
				cur_adc=ADC_DIVMULT;
				ADMUX = (1<<ADLAR) | cur_adc; //Setup for next conversion
				ADCSRA |= (1<<ADSC);		//Start Conversion

				if (pw_adc>adch) t=pw_adc-adch;
				else t=adch-pw_adc;

				if (t>PW_ADC_DRIFT){
					pw_adc=adch;

					pw_time=calc_pw(pw_adc,divclk_time);
				}
				
			}

		
		}

	} //main loop

} //void main()





