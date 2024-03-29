#include <serialize.h>

#include "packet.h"
#include "constants.h"
typedef enum
{
 STOP=0,
 FORWARD=1,
 BACKWARD=2,
 LEFT=3,
 RIGHT=4
} TDirection;
volatile TDirection dir = STOP;

/*
 * Vincent's configuration constants
 */

// Number of ticks per revolution from the 
// wheel encoder.
// Wheel circumference in cm.
// We will use this to calculate forward/backward distance traveled 
// by taking revs * WHEEL_CIRC

#define COUNTS_PER_REV      192
#define WHEEL_CIRC          20.42

// Motor control pins. You need to adjust these till
// Vincent moves in the correct direction
#define LF                  6   // Left forward pin
#define LR                  5   // Left reverse pin
#define RF                  10  // Right forward pin
#define RR                  11  // Right reverse pin

/*
 *    Vincent's State Variables
 */

volatile unsigned long leftForwardTicks; 
volatile unsigned long rightForwardTicks;
volatile unsigned long leftReverseTicks; 
volatile unsigned long rightReverseTicks;

volatile unsigned long leftForwardTicksTurns; 
volatile unsigned long rightForwardTicksTurns;
volatile unsigned long leftReverseTicksTurns; 
volatile unsigned long rightReverseTicksTurns;

// Store the revolutions on Vincent's left
// and right wheels
volatile unsigned long leftRevs;
volatile unsigned long rightRevs;

// Forward and backward distance traveled
volatile unsigned long forwardDist;
volatile unsigned long reverseDist;

// Variables to keep track of whether we have moved a commanded distance
unsigned long deltaDist;
unsigned long newDist;


/*
 * 
 * Vincent Communication Routines.
 * 
 */
 
 
TResult readPacket(TPacket *packet)
{
    // Reads in data from the serial port and
    // deserializes it.Returns deserialized
    // data in "packet".
    
    char buffer[PACKET_SIZE];
    int len;

    len = readSerial(buffer);

    if(len == 0)
      return PACKET_INCOMPLETE;
    else
      return deserialize(buffer, len, packet);
    
}

void sendStatus()
{
  TPacket statusPacket;
  statusPacket.packetType = PACKET_TYPE_RESPONSE;
  statusPacket.command = RESP_STATUS;
  statusPacket.params[0] = leftForwardTicks;
  statusPacket.params[1] = rightForwardTicks;
  statusPacket.params[2] = leftReverseTicks;
  statusPacket.params[3] = rightReverseTicks;
  statusPacket.params[4] = leftForwardTicksTurns;
  statusPacket.params[5] = rightForwardTicksTurns;
  statusPacket.params[6] = leftReverseTicksTurns;
  statusPacket.params[7] = rightReverseTicksTurns;
  statusPacket.params[8] = forwardDist;
  statusPacket.params[9] = reverseDist;
  sendResponse(&statusPacket);
}

void sendMessage(const char *message)
{
  // Sends text messages back to the Pi. Useful
  // for debugging.
  
  TPacket messagePacket;
  messagePacket.packetType=PACKET_TYPE_MESSAGE;
  strncpy(messagePacket.data, message, MAX_STR_LEN);
  sendResponse(&messagePacket);
}

void sendBadPacket()
{
  // Tell the Pi that it sent us a packet with a bad
  // magic number.
  
  TPacket badPacket;
  badPacket.packetType = PACKET_TYPE_ERROR;
  badPacket.command = RESP_BAD_PACKET;
  sendResponse(&badPacket);
  
}

void sendBadChecksum()
{
  // Tell the Pi that it sent us a packet with a bad
  // checksum.
  
  TPacket badChecksum;
  badChecksum.packetType = PACKET_TYPE_ERROR;
  badChecksum.command = RESP_BAD_CHECKSUM;
  sendResponse(&badChecksum);  
}

void sendBadCommand()
{
  // Tell the Pi that we don't understand its
  // command sent to us.
  
  TPacket badCommand;
  badCommand.packetType=PACKET_TYPE_ERROR;
  badCommand.command=RESP_BAD_COMMAND;
  sendResponse(&badCommand);

}

void sendBadResponse()
{
  TPacket badResponse;
  badResponse.packetType = PACKET_TYPE_ERROR;
  badResponse.command = RESP_BAD_RESPONSE;
  sendResponse(&badResponse);
}

void sendOK()
{
  TPacket okPacket;
  okPacket.packetType = PACKET_TYPE_RESPONSE;
  okPacket.command = RESP_OK;
  sendResponse(&okPacket);  
}

void sendResponse(TPacket *packet)
{
  // Takes a packet, serializes it then sends it out
  // over the serial port.
  char buffer[PACKET_SIZE];
  int len;

  len = serialize(buffer, packet, sizeof(TPacket));
  writeSerial(buffer, len);
}


/*
 * Setup and start codes for external interrupts and 
 * pullup resistors.
 * 
 */
// Enable pull up resistors on pins 2 and 3
void enablePullups()
{
  DDRD &= 0b11110011; //Set PD2 and PD3 to inputs
  PORTD |= 0b00001100; //enable pullup PD2 and PD3
}

// Functions to be called by INT0 and INT1 ISRs.
void leftISR()
{
  if (dir == FORWARD){
    leftForwardTicks++;
    forwardDist = (unsigned long) ((float) leftForwardTicks / COUNTS_PER_REV * WHEEL_CIRC);
  }
  if (dir == BACKWARD){
    leftReverseTicks++;
    reverseDist = (unsigned long) ((float) leftReverseTicks / COUNTS_PER_REV * WHEEL_CIRC);
  }
  if (dir == LEFT){
    leftReverseTicksTurns++;
  }
  if (dir == RIGHT){
    leftForwardTicksTurns++;
  }
  Serial.print("LEFT: ");
  Serial.println(leftForwardTicksTurns);
}

void rightISR()
{
  if (dir == FORWARD){
    rightForwardTicks++;
  }
  if (dir == BACKWARD){
    rightReverseTicks++;
  }
  if (dir == LEFT){
    rightForwardTicksTurns++;
  }
  if (dir == RIGHT){
    rightReverseTicksTurns++;
  }
  Serial.print("RIGHT: ");
  Serial.println(rightForwardTicksTurns);
}

// Set up the external interrupt pins INT0 and INT1
// for falling edge triggered. Use bare-metal.
void setupEINT()
{
  // Use bare-metal to configure pins 2 and 3 to be
  // falling edge triggered. Remember to enable
  // the INT0 and INT1 interrupts.
  EICRA |= 0b00001010; //configure pins 2 and 3 to be falling edge triggered.
  EIMSK |= 0b00000011; //Enable the INT0 and INT1 interrupts.
}

// Implement the external interrupt ISRs below.
// INT0 ISR should call leftISR while INT1 ISR
// should call rightISR.
ISR (INT0_vect)
{
  leftISR(); // INT0 ISR should call leftISR
}

ISR (INT1_vect)
{
  rightISR(); //INT1 ISR should call rightISR.
}

/*
 * Setup and start codes for serial communications
 * 
 */
// Set up the serial connection. For now we are using 
// Arduino Wiring, you will replace this later
// with bare-metal code.
void setupSerial()
{
  // To replace later with bare-metal.
  Serial.begin(9600);
}

// Start the serial connection. For now we are using
// Arduino wiring and this function is empty. We will
// replace this later with bare-metal code.

void startSerial()
{
  // Empty for now. To be replaced with bare-metal code
  // later on.
  
}

// Read the serial port. Returns the read character in
// ch if available. Also returns TRUE if ch is valid. 
// This will be replaced later with bare-metal code.

int readSerial(char *buffer)
{

  int count=0;

  while(Serial.available())
    buffer[count++] = Serial.read();

  return count;
}

// Write to the serial port. Replaced later with
// bare-metal code

void writeSerial(const char *buffer, int len)
{
  Serial.write(buffer, len);
}

/*
 * Vincent's motor drivers.
 * 
 */

// Set up Vincent's motors. Right now this is empty, but
// later you will replace it with code to set up the PWMs
// to drive the motors.
void setupMotors()
{
  /* Our motor set up is:  
   *    A1IN - Pin 5, PD5, OC0B
   *    A2IN - Pin 6, PD6, OC0A
   *    B1IN - Pin 10, PB2, OC1B
   *    B2In - pIN 11, PB3, OC2A
   */
  DDRD |= 0b01100000;
  DDRB |= 0b00001100;
  
  TCNT0 = 0;
  TCNT1H = 0;
  TCNT1L = 0;
  
  
  OCR0A = 0;
  OCR0B = 0;
  OCR1AH = 0;
  OCR1AL = 0;
  OCR1BH = 0;
  OCR1BL = 0;
  
  TIMSK0 |= 0b00000110;
  TIMSK1 |= 0b00000110;
}

// Start the PWM for Vincent's motors.
// We will implement this later. For now it is
// blank.
void startMotors()
{
  TCCR0B = 0b00000011; //Prescale 64, WGM02 = 0
  TCCR1B = 0b00000011; //Prescale 64, WGM13,12=0
  sei();
}

// Convert percentages to PWM values
int pwmVal(float speed)
{
  if(speed < 0.0)
    speed = 0;

  if(speed > 100.0)
    speed = 100.0;

  return (int) ((speed / 100.0) * 255.0);
}

// Move Vincent forward "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// move forward at half speed.
// Specifying a distance of 0 means Vincent will
// continue moving forward indefinitely.
void forward(float dist, float speed)
{ 
  // Code to tell us how far to move
  if(dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;

  newDist = forwardDist + deltaDist;
  
  dir = FORWARD;
  
  OCR0A = pwmVal(speed); //LF PWM Val
  OCR1AH = 0;            //RF PWM Val
  OCR1AL = pwmVal(speed);
   
  // For now we will ignore dist and move
  // forward indefinitely. We will fix this
  // in Week 9.

  TCCR0A = 0b10000001; // OC0A PWM LF
  TCCR1A = 0b10000001; // OC1A PWM RF
  PORTD &= 0b11011111; // Clear PD5 LR
  PORTB &= 0b11111011; // Clear PB2 RR
}

// Reverse Vincent "dist" cm at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// reverse at half speed.
// Specifying a distance of 0 means Vincent will
// continue reversing indefinitely.
void reverse(float dist, float speed)
{
  // Code to tell us how far to move
  if(dist == 0)
    deltaDist = 999999;
  else
    deltaDist = dist;

  newDist = reverseDist + deltaDist;
  
  dir = BACKWARD;

  OCR0B = pwmVal(speed); //LR PWM Val
  OCR1BH = 0;            //RR PWM Val
  OCR1BL = pwmVal(speed);
  
  // For now we will ignore dist and 
  // reverse indefinitely. We will fix this
  // in Week 9.

  TCCR0A = 0b00100001; // OC0B PWM LR
  TCCR1A = 0b00100001; // OC1B PWM RR
  PORTD &= 0b10111111; // Clear PD6 LF
  PORTB &= 0b11111101; // Clear PB1 RF
}

// Turn Vincent left "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Vincent to
// turn left indefinitely.
void left(float ang, float speed)
{
  dir = LEFT;
  
  OCR0B = pwmVal(speed); //LR PWM Val
  OCR1AH = 0;            //RF PWM Val
  OCR1AL = pwmVal(speed);
  
  // For now we will ignore ang. We will fix this in Week 9.
  // To turn left we reverse the left wheel and move
  // the right wheel forward.
  
  TCCR0A = 0b00100001; // OC0B PWM LR
  TCCR1A = 0b10000001; // OC1A PWM RF
  PORTD &= 0b10111111; // Clear PD6 LF
  PORTB &= 0b11111011; // Clear PB2 RR
}

// Turn Vincent right "ang" degrees at speed "speed".
// "speed" is expressed as a percentage. E.g. 50 is
// turn left at half speed.
// Specifying an angle of 0 degrees will cause Vincent to
// turn right indefinitely.
void right(float ang, float speed)
{
  dir = RIGHT;
  
  OCR0A = pwmVal(speed); //LF PWM Val
  OCR1BH = 0;            //RR PWM Val
  OCR1BL = pwmVal(speed);
  
  // For now we will ignore ang. We will fix this in Week 9.
  // To turn right we reverse the right wheel and move
  // the left wheel forward.
  
  TCCR0A = 0b10000001; // OC0A PWM LF
  TCCR1A = 0b00100001; // OC1B PWM RR
  PORTD &= 0b11011111; // Clear PD5 LR
  PORTB &= 0b11111101; // Clear PB1 RF
}

// Stop Vincent. To replace with bare-metal code later.
void stop()
{
  dir = STOP;

  PORTD &= 0b10111111; // Clear PD6 LF
  PORTB &= 0b11111101; // Clear PB1 RF
  PORTD &= 0b11011111; // Clear PD5 LR
  PORTB &= 0b11111011; // Clear PB2 RR
}

/*
 * Vincent's setup and run codes
 * 
 */

// Clears all our counters
void clearCounters()
{
  leftForwardTicks=0;
  rightForwardTicks=0;
  leftReverseTicks=0;
  rightReverseTicks=0;
  
  leftForwardTicksTurns=0;
  rightForwardTicksTurns=0;
  leftReverseTicksTurns=0;
  rightReverseTicksTurns=0;
  
  leftRevs=0;
  rightRevs=0;
  
  forwardDist=0;
  reverseDist=0; 
}

// Clears one particular counter
void clearOneCounter(int which)
{
  switch(which)
  {
    case 0:
      clearCounters();
      break;

    case 1:
      leftForwardTicks=0;
      break;

    case 2:
      rightForwardTicks=0;
      break;

    case 3:
      leftReverseTicks=0;
      break;

    case 4:
      rightReverseTicks=0;
      break;

    case 5:
      forwardDist=0;
      break;

    case 6:
      reverseDist=0;
      break;
  }
}
// Intialize Vincet's internal states

void initializeState()
{
  clearCounters();
}

void handleCommand(TPacket *command)
{
  switch(command->command)
  {
    // For movement commands, param[0] = distance, param[1] = speed.
    case COMMAND_FORWARD:
        sendOK();
        forward((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_TURN_LEFT:
        sendOK();
        left((float) command->params[0], (float) command->params[1]);
      break;

    case COMMAND_GET_STATS:
        sendStatus();
      break;
      
    case COMMAND_CLEAR_STATS:
        clearOneCounter(command->params[0]);
        sendOK();
      break;

    /*
     * Implement code for other commands here.
     * 
     */
        
    default:
      sendBadCommand();
  }
}

void waitForHello()
{
  int exit=0;

  while(!exit)
  {
    TPacket hello;
    TResult result;
    
    do
    {
      result = readPacket(&hello);
    } while (result == PACKET_INCOMPLETE);

    if(result == PACKET_OK)
    {
      if(hello.packetType == PACKET_TYPE_HELLO)
      {
     

        sendOK();
        exit=1;
      }
      else
        sendBadResponse();
    }
    else
      if(result == PACKET_BAD)
      {
        sendBadPacket();
      }
      else
        if(result == PACKET_CHECKSUM_BAD)
          sendBadChecksum();
  } // !exit
}

void setup() {
  // put your setup code here, to run once:

  cli();
  setupEINT();
  setupSerial();
  startSerial();
  setupMotors();
  startMotors();
  enablePullups();
  initializeState();
  sei();
}

void handlePacket(TPacket *packet)
{
  switch(packet->packetType)
  {
    case PACKET_TYPE_COMMAND:
      handleCommand(packet);
      break;

    case PACKET_TYPE_RESPONSE:
      break;

    case PACKET_TYPE_ERROR:
      break;

    case PACKET_TYPE_MESSAGE:
      break;

    case PACKET_TYPE_HELLO:
      break;
  }
}

void loop() {

// Uncomment the code below for Step 2 of Activity 3 in Week 8 Studio 2

// forward(0, 100);

// Uncomment the code below for Week 9 Studio 2

 // put your main code here, to run repeatedly:
  TPacket recvPacket; // This holds commands from the Pi

  TResult result = readPacket(&recvPacket);
  
  if(result == PACKET_OK)
    handlePacket(&recvPacket);
  else
    if(result == PACKET_BAD)
    {
      sendBadPacket();
    }
    else
      if(result == PACKET_CHECKSUM_BAD)
      {
        sendBadChecksum();
      } 
      
  if(deltaDist > 0){
    if(dir==FORWARD){
      if(forwardDist > newDist){
        deltaDist=0;
        newDist=0;
        stop();
      }
    }
    else 
      if(dir == BACKWARD){
        if(reverseDist > newDist){
          deltaDist=0;
          newDist=0;
          stop();
        }
      }
    else
      if(dir == STOP){
        deltaDist=0;
        newDist=0;
        stop();
      }
  } 
}
