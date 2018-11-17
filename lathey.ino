/********************************************************************
 * Single Axis Stepper-Lathe Controller                             *
 * Control interface is currently just a rotary encoder and switch  *
 *******************************************************************/

#include <limits.h>
#define GND 17
#define VCC 18
#define SWT 19
#define CHA 20
#define CHB 21
#define LED LED_BUILTIN

#define TRUE 1
#define FALSE 0
#define PENDING 255
#define STOP 0
#define ACCEL 1
#define STEADY 2
#define DECEL 3

#define UPDATE_TIME 10000
#define DEBOUNCE_TIME 10
#define ENCODER_TIME 1

#define COUNTER_MAX ULONG_MAX

#define DIR_UP 1
#define DIR_DN 0

#define DBG_MSG1 1
#define DBG_MSG2 2
#define DBG_MSG3 3
#define DBG_MSG4 4
#define DBG_EN

typedef struct
{
  unsigned long ramp_pos;
  unsigned long ramp_rate;
}RAMP_TABLE_ROW;

typedef struct
{
  byte tableSize;
  RAMP_TABLE_ROW * tableData;
}RAMP_TABLE;

static RAMP_TABLE_ROW motorRampData[] = 
{
  {0, 100},
  {1000, 50},
  {2000, 25},
  {4000, 12},
  {8000, 6},
  {16000, 3},
  {ULONG_MAX, 1}
};

static RAMP_TABLE_ROW encoderStepData[] = 
{
  {0, 1},
//  {5, 2},
  {11, 2},
  {52, 5},
  {105, 10},
//  {300, 200},
  {910, 100},
//  {3000, 2000},
  {9100, 1000},
//  {30000, 20000},
  {91000, 10000},
//  {300000, 200000},
  {ULONG_MAX, 10000}
};

static RAMP_TABLE motorRampTable, encoderStepTable;

static byte pinStates = 0;
static volatile long counter = 0;
static volatile byte counterChanged = false;
static volatile byte encoderChanged = false;
static volatile byte buttonPressed = PENDING;
static byte motorState = STOP;
static byte motorEnable = false;
static long currentSpeed = 0;
static long targetSpeed = 0;
static byte rotaryState = 0;
static byte debugMsg = 0;

byte  invertEncoderDir = true;
volatile unsigned short int timer1_top = INT16_MAX;


byte getPinStates()
{
  byte newPinStates = 0;
  newPinStates |= digitalRead(SWT)<<2;
  newPinStates |= digitalRead(CHA)<<1;
  newPinStates |= digitalRead(CHB)<<0;
  return newPinStates;
}

RAMP_TABLE_ROW * fetchFromTable(RAMP_TABLE * table, unsigned long pos, byte dir)
{
  byte row = 0;
  if(dir > 0)
  {
    /* start at the beggining of the table and work up */
    while(table->tableData[row].ramp_pos < pos && row < table->tableSize)
    {
      row++;
    }
  }
  else
  {
    /* start at the end of the table and work down */
    row = table->tableSize-1;
    while(table->tableData[row].ramp_pos > pos && row > 0)
    {
      row--;
    }
  }
  return &table->tableData[row];
}

void buttonISR()
{
  if(buttonPressed == FALSE)
  {
    buttonPressed = TRUE;
  }
}

void stepCounter(byte dir)
{
  if(counterChanged == FALSE)
  {
    unsigned long stepSize = fetchFromTable(&encoderStepTable, counter, dir)->ramp_rate;
    if(dir)
    {
      if((COUNTER_MAX - counter) < stepSize)
      {
        counter = COUNTER_MAX;
      }
      else
      {
        counter += stepSize;
      }
    }
    else
    {
      if(counter < stepSize)
      {
        counter = 0;
      }
      else
      {
        counter -= stepSize;
      }
    }
    counterChanged = TRUE;
  }
}
void rotaryISRA()
{
  byte newState = digitalRead(CHB);
  //debugMsg = DBG_MSG1;
  if(rotaryState != 0 && encoderChanged == false)
  {
    //debugMsg = DBG_MSG2;

    if(!newState)stepCounter(DIR_DN);
    rotaryState = 0;
    encoderChanged = true;
  }
}
void rotaryISRB()
{
  byte newState = digitalRead(CHA);
  //debugMsg = DBG_MSG3;
  if(rotaryState == 0 && encoderChanged == false)
  {
    if(!newState)stepCounter(DIR_UP);
    rotaryState = 1;
    encoderChanged = true;
  }
}

void setMotorState(byte newState)
{
  bool updateState = true;
  switch (newState)
  {
    case STOP:
      Serial.println("Stopped");
      digitalWrite(LED,0);
      break;
    case STEADY:
      Serial.println("Steady");
      digitalWrite(LED,0);
      break;
    case ACCEL:
      Serial.println("Accelerating");
      digitalWrite(LED,1);
      break;
    case DECEL:
      Serial.println("Decelerating");
      digitalWrite(LED,1);
      break;
    default:
      updateState = false;
  }
  if (updateState)
  {
    motorState = newState;
    Serial.println(currentSpeed);
  }
}
void setup() {
  // put your setup code here, to run once:
  pinMode(GND,OUTPUT);
  digitalWrite(GND,0);
  pinMode(VCC,OUTPUT);
  digitalWrite(VCC,1);
  pinMode(SWT,INPUT_PULLUP);
  pinMode(CHA,INPUT);
  pinMode(CHB,INPUT);
  pinMode(LED,OUTPUT);
  digitalWrite(LED,0);
  
  Serial.begin(115200);
  
  Serial.println("Lathey v0.1");
  Serial.println(PORTB);
  Serial.println(LOW);
  attachInterrupt(digitalPinToInterrupt(CHA), rotaryISRA, FALLING);
  attachInterrupt(digitalPinToInterrupt(CHB), rotaryISRB, FALLING);
  attachInterrupt(digitalPinToInterrupt(SWT), buttonISR, FALLING);

  initStepperTimer();
  interrupts();

  /* initialise tables */
  encoderStepTable.tableSize = sizeof(encoderStepData)/sizeof(RAMP_TABLE_ROW);
  encoderStepTable.tableData = encoderStepData;
  motorRampTable.tableSize = sizeof(motorRampData)/sizeof(RAMP_TABLE_ROW);
  motorRampTable.tableData = motorRampData;

}

void loop() {
  static unsigned long buttonTimer = millis();
  static unsigned long encoderTimer = millis();
  static unsigned long rampTimer = micros() + UPDATE_TIME;
  /*
  byte newPinStates = getPinStates();
  if(newPinStates != pinStates)
  {
    pinStates = newPinStates;
    Serial.println(pinStates,BIN);
    Serial.println(counter);
    delay(10);
  }
  */
  if(counterChanged)
  {
    Serial.println(counter);
    counterChanged = false;
  }
  
  if(encoderChanged == TRUE)
  {
    
    encoderChanged = PENDING;
    encoderTimer = millis() + ENCODER_TIME;
  }
  else if(encoderChanged == PENDING && encoderTimer < millis())
  {
    encoderChanged = FALSE;
  }

  
  if(buttonPressed == TRUE && digitalRead(SWT) == HIGH)
  {
    //Serial.println("Button Pressed");
    buttonTimer = millis() + DEBOUNCE_TIME;
    buttonPressed = PENDING;

    motorEnable = !motorEnable;
    if(motorEnable)
    {
      Serial.println("Starting Motor");
    }
    else
    {
      Serial.println("Stopping Motor");
    }
    
  }
  else if( buttonPressed == PENDING && buttonTimer < millis())
  {
      buttonPressed = FALSE;
  }

  unsigned long elapsedTime = micros() - rampTimer;
  if(elapsedTime >= UPDATE_TIME)
  {
    rampTimer = micros() + UPDATE_TIME;
    long speedStep;// = fetchFromTable(&motorRampTable, currentSpeed);

    if(motorEnable)
    {
      targetSpeed = counter;
    }
    else
    {
      targetSpeed = 0;
    }
    
    switch (motorState)
    {
      case STOP:
      case STEADY:
        if(currentSpeed < targetSpeed)
        {
          setMotorState(ACCEL);
        }
        else if(currentSpeed > targetSpeed)
        {
          setMotorState(DECEL);
        }
        break;
        
      case DECEL:
        speedStep = fetchFromTable(&motorRampTable, currentSpeed, DIR_DN)->ramp_rate;

        if(targetSpeed > currentSpeed) // if speed step is larger than target, go straigt to target
        {
          setMotorState(ACCEL);
        }
        else if(targetSpeed < (currentSpeed - speedStep))
        {
          currentSpeed -= speedStep;
        }
        else
        {
          currentSpeed = targetSpeed;

          if(targetSpeed == 0)
          {
            setMotorState(STOP);
          }
          else
          {
            setMotorState(STEADY);
          }
        }
        break;

      case ACCEL:
        speedStep = fetchFromTable(&motorRampTable, currentSpeed, DIR_UP)->ramp_rate;
        
        if(targetSpeed < currentSpeed) // if speed step is larger than target, go straigt to target
        {
          setMotorState(DECEL);
        }
        else if(currentSpeed < (targetSpeed - speedStep) && targetSpeed > speedStep) 
        {
          currentSpeed += speedStep;
        }
        else
        {
          currentSpeed = targetSpeed;
          setMotorState(STEADY);
        }
        break;
    }
    static unsigned short foop = 0;
    foop++;
    if( (foop > 100 && (motorState == ACCEL || motorState == DECEL)))
    {
      foop = 0;
      //Serial.println("bloop");
//      if(motorState == ACCEL || motorState == DECEL) || motorState == STEADY)
//      {
        Serial.print(speedStep);
        Serial.print(", ");
        Serial.print(targetSpeed);
        Serial.print(", ");
        Serial.print(currentSpeed);
        Serial.println();
//      }
    }
    static unsigned short foop2 = 0;
    foop2++;
  static unsigned short farp =0;
  if((foop2&0xff) == 0)
  {
    farp = max(farp, TCNT1);
  }
    if( foop2 > 25500)
    {
      //farp = max(farp, TCNT1);
      Serial.print(farp);
      Serial.print(", ");
      Serial.println(timer1_top);
      farp = 0;
      if (timer1_top > 100)
      {
        timer1_top-=20;
      }
      else
      {
        timer1_top = UINT16_MAX;
      }
      foop2  = 0;

    }
#ifdef DBG_EN
    switch(debugMsg)
    {
      case DBG_MSG1:
        Serial.println("foop1");
        break;
      case DBG_MSG2:
        Serial.println("foop2");
        break;
      case DBG_MSG3:
        Serial.println("foop3");
        break;
      case DBG_MSG4:
        Serial.println("foop4");
        break;
    }
#endif
    debugMsg = 0;   
  }

}
