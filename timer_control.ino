


ISR(TIMER1_CAPT_vect)        // interrupt service routine 
{
//  TCNT1 = timer1_counter;   // preload timer
  
  digitalWrite(11, HIGH);
  ICR1 = timer1_top;
  //debugMsg = DBG_MSG2;
}

void initStepperTimer()
{
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  OCR1A = 50;
  ICR1 = INT16_MAX;

  TCCR1A = 0x80;  //OC1A set
  TCCR1B = 0x19;  //CTC ICR1, CLK/1

  TIFR1 = 0;  //clear flags
  TIMSK1 = bit(ICIE1);     // enable ICR1 interrupt
  
  pinMode(11, OUTPUT);
}

