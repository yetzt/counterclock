#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>
#include <ESP8266WiFi.h>
#include "CounterClockFont.h"
#include "CounterClockTimer.h"

// todo: button library

Adafruit_SSD1306 display = Adafruit_SSD1306();

#if defined(ESP8266)
  #define BUTTON_A 0
  #define BUTTON_B 16
  #define BUTTON_C 2
  #define LED      0
#elif defined(ARDUINO_STM32F2_FEATHER)
  #define BUTTON_A PA15
  #define BUTTON_B PC7
  #define BUTTON_C PC5
  #define LED PB5
#elif defined(TEENSYDUINO)
  #define BUTTON_A 4
  #define BUTTON_B 3
  #define BUTTON_C 8
  #define LED 13
#else 
  #define BUTTON_A 9
  #define BUTTON_B 6
  #define BUTTON_C 5
  #define LED      13
#endif

// pin for vibration motor
#define VIBE 14

// vibration states
#define BUZZER_OFF 0
#define BUZZER_SHORT 200
#define BUZZER_MEDIUM 500
#define BUZZER_LONG 1000

// clock states
#define STATE_WAIT 0
#define STATE_LINEUP 1
#define STATE_JAM 2
#define STATE_TIMEOUT 3
#define STATE_HALFTIME 4
#define STATE_END 5

#if (SSD1306_LCDHEIGHT != 32)
 #error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// timer values for drawing
int  last_draw_t = 0;
int  current_draw_t = 0;

// timer values for button events
int  last_button_t = 0;
int  current_button_t = 0;

int  a_last = LOW;    // last state of the button
int  a_state = LOW;   // current state of the button
int  a_pressed = 0;   // time button was pressed
int  a_duration = 0;  // duration button was pressed
bool a_down = false;  // button pressed in this iteration
bool a_up = false;    // button released in this iteration

int  b_last = LOW;    // last state of the button
int  b_state = LOW;   // current state of the button
int  b_pressed = 0;   // time button was pressed
int  b_duration = 0;  // duration button was pressed
bool b_down = false;  // button pressed in this iteration
bool b_up = false;    // button released in this iteration

int  c_last = LOW;    // last state of the button
int  c_state = LOW;   // current state of the button
int  c_pressed = 0;   // time button was pressed
int  c_duration = 0;  // duration button was pressed
bool c_down = false;  // button pressed in this iteration
bool c_up = false;    // button released in this iteration

// timer values for buzzer
int buzzer_t = 0;
int buzzer_state = 0;
int buzzer_last = 0;

// type of timeout
int timeout_state = 0;

// keep numbers of timeouts and reviews
int timeouts_a = 3;
int timeouts_b = 3;
int review_a = true;
int review_b = true;
int review_t = 0;

Timer PeriodClock(1800000);   // 30:00
Timer JamClock(120000);       // 02:00
Timer LineupClock(30000);     // 00:30
Timer TimeoutClock(3600000);  // 60:00 (just a large number to accomodate official timeouts)
Timer HalftimeClock(1800000); // 30:00

// initial states
int state = STATE_WAIT;
int next_state = STATE_WAIT;

// counter for period and jam
int period = 0;
int jam = 0;

// overtime mode bit
bool overtime = false;

void setup() {

  // send wifi to rest, since we don't use it
  WiFi.forceSleepBegin();

  // initialize with the I2C addr 0x3C (for the 128x32)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  

  // turn display 180
  display.setRotation(2);

  // set the buzzer gpio to output
  pinMode(VIBE, OUTPUT);

  // set the button modes
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  
  // clear the display buffer.
  display.clearDisplay();
  display.display();
  
  // show fancy splash screen
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setFont(&CounterClockFont);
  display.setTextWrap(false);
  display.setCursor(0,16);
  display.print("COUNTER");
  display.setCursor(0,32);
  display.print("CLOCK");
  display.display();

  // one long buzz
  buzzer_state = BUZZER_LONG;

  // wait a second...
  delay(1000);
  
}

void loop() {

  // run clocks
  PeriodClock.run();
  JamClock.run();
  LineupClock.run();
  TimeoutClock.run();
  HalftimeClock.run();

  // update button states
  buttons();

  // run buzzer
  buzzer();

  // action
  switch (state) {
    
    case STATE_WAIT: // waiting for game to start
    
      if (a_down || b_down || c_down) { // any button starts the game

        // change state to lineup, start lineup
        next_state = STATE_LINEUP;
        LineupClock.start();
        
        // increment period number
        period++;
      
      }
      
    break;
    case STATE_LINEUP: // lineup mode

      if (c_down) { // start timeout

        // pause period clock
        PeriodClock.pause();
        
        // start timeout clock
        TimeoutClock.start();

        // timeout  
        next_state = STATE_TIMEOUT;
        timeout_state = 0;

        // stop lineup
        LineupClock.reset();

      } else if (b_down && (PeriodClock.paused() || jam == 0 || PeriodClock.total() < 30000)) { // five seconds!
        // go to five second immediately, but enable only after timeout, on start of period
        // or when the period clock runs out (because the period clock gets restarted after 
        // an official timeout when it there's <30 sec left)
        
        LineupClock.set(5000);

      } else if (a_down) { // start jam
        
        // start jam by setting the lineup clock to 0
        LineupClock.set(0);
      
      }
      
      if (PeriodClock.done() && !overtime) { // when period clock has ran out and this is not an overtime jam, go to halftime or end of game
          next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
          HalftimeClock.start();
      }

      // buzzing: medium on 5, short on 4-1, long on 0
      if (LineupClock.left() <= 5 && LineupClock.ticked()) buzzer_state = (LineupClock.left() == 0) ? BUZZER_LONG : (LineupClock.left() == 5) ? BUZZER_MEDIUM : BUZZER_SHORT;
      
      if (LineupClock.done()) { // when lineup clock has ran out, start jam
        next_state = STATE_JAM;
                
        // start jam clock
        JamClock.reset();
        JamClock.start();

        // start period clock
        PeriodClock.start();
        
        // increment jam number
        jam++;

      }
    break;
    case STATE_JAM: // i'm jammin'; hope you like jammin' too

      if (c_down) { // end jam and time out immediately

        // if jam is on less than 3 seconds, "cancel" jam
        // this is strictly speaking not compliant with the rules
        // but official timeouts are really often called extremely late
        if (JamClock.left() >= 117) jam--;

        // stop jam and timeout
        next_state = STATE_TIMEOUT;
        timeout_state = 0;

        // stop lineup
        JamClock.reset();

        // pause period clock
        PeriodClock.pause();

        // start timeout clock
        TimeoutClock.reset();
        TimeoutClock.start();
      
      } else if ((a_down || b_down) && !overtime && JamClock.left() < 117) { // end jam
        // as a safeguard against ending a jam by trying to start it on mark,
        // allow calling off a jam after 3 seconds only
        // no one ever will gain lead and call in 3 seconds
        // also: no calling off overtime jams
      
        // end jam and lineup
        JamClock.set(0);
        buzzer_state = BUZZER_SHORT;
      
      } else if (JamClock.left() <= 5 && JamClock.ticked()) {
    
        // buzz end of jam
        buzzer_state = (JamClock.left() == 0) ? BUZZER_LONG : BUZZER_SHORT;
    
      }

      if (JamClock.done()) { // has the jam come to an end?

        if (PeriodClock.done()) { // is the period over?

            next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
            HalftimeClock.reset();
            HalftimeClock.start();
          
        } else {
          
          // new lineup
          next_state = STATE_LINEUP;
          JamClock.reset();
          LineupClock.reset();   
          LineupClock.start();        

        }
        
      }

    break;
    case STATE_TIMEOUT: // welcome to the marvellous world of timeouts

      // cycle through the six timeout states (ot, tto, or and adjustments)
      if (c_down) timeout_state = (timeout_state+1)%6;

      // FIXME: this should better be a switch()
      if (timeout_state == 0) { // official timeout

        if (a_down) { // end official timeout

          TimeoutClock.reset();

          // if period clock is done, go to halftime
          if (PeriodClock.done()) {

            next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
            HalftimeClock.reset();
            HalftimeClock.start();

          } else {
            
            // resume to lineup
            next_state = STATE_LINEUP;
            LineupClock.reset();
            LineupClock.start();

            // resume period clock after official timeout when < 30 sec on period clock
            // in accordance with wftda rule 1.4.3.1
            if (PeriodClock.total() <= 30000) PeriodClock.start();
            
          }
          
        }

      } else if (timeout_state == 1) { // team timeout, auto resume after 1:00

        // set number of timeouts, button b is confusingly team a and vice versa
        // the nifty modulo rollover is intentional, in case of nessecary correction
        if (b_down && TimeoutClock.gone() < 60) timeouts_a = (timeouts_a+3)%4;
        if (a_down && TimeoutClock.gone() < 60) timeouts_b = (timeouts_b+3)%4;

        // in case we missed the automatic end, A resumes the 
        if (a_down && TimeoutClock.gone() >= 60) TimeoutClock.set(3540);

        if (TimeoutClock.ticked()) { // only bother to do stuff on full seconds
        
          if (TimeoutClock.gone() == 60) { // one minute has passed, proceed to lineup or end
          
            // long buzz
            buzzer_state = BUZZER_LONG;
          
            // reset the timeout clock
            TimeoutClock.reset();

            // if period clock is done, go to halftime
            if (PeriodClock.done()) {

              next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
              HalftimeClock.reset();
              HalftimeClock.start();

            } else {
            
              // resume to lineup
              next_state = STATE_LINEUP;
              LineupClock.reset();
              LineupClock.start();
            
            }
          
          } else if (TimeoutClock.gone() >= 55) { // a short buzz on the last 5 seconds

            buzzer_state = BUZZER_SHORT;

          } else if (TimeoutClock.gone() == 50) { // give 10 second notice buzz (some statsbooks recommend giving a 10 second warning)

            buzzer_state = BUZZER_MEDIUM;

          }

        }

      } else if (timeout_state == 2) { // official review

        // set the team who has taken the review with B or A button
        if (b_down && TimeoutClock.gone() < 60) review_t = 1;
        if (a_down && TimeoutClock.gone() < 60) review_t = 2;  

        // after 60 seconds, game may resume
        if ((a_down || b_down) && TimeoutClock.gone() >= 60) {

          // if review is not retained (B button), set review to false
          if (b_down && review_t == 1) review_a = false;
          if (b_down && review_t == 2) review_b = false;

          // reset review team 
          review_t = 0;

          // reset timeout clock
          TimeoutClock.reset();

          // if period clock is done, go to halftime
          if (PeriodClock.done()) {

            next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
            HalftimeClock.reset();
            HalftimeClock.start();

          } else {
            
            // resume to lineup
            next_state = STATE_LINEUP;
            LineupClock.reset();
            LineupClock.start();
            
          }
          
        } else if (TimeoutClock.ticked() && TimeoutClock.gone() == 60) {
          // medium buzz on one minute mark
          // to inform the jam timer the game may be resumed now
          buzzer_state = BUZZER_MEDIUM;
        }


      } else if (timeout_state == 3) { // adjust period clock seconds

        if (b_down) PeriodClock.set(PeriodClock.total()-1000);
        if (a_down) PeriodClock.set(PeriodClock.total()+1000);
        
      } else if (timeout_state == 4) { // adjust period clock minutes

        if (b_down) PeriodClock.set(PeriodClock.total()-60000);
        if (a_down) PeriodClock.set(PeriodClock.total()+60000);
        
      } else if (timeout_state == 5) { // adjust jam number

        if (b_down) jam--;
        if (a_down) jam++;

      }
      
    break;
    case STATE_HALFTIME: // halftime

      if (a_down || b_down) { // end halftime

        // stop halftime clock, end period clock
        HalftimeClock.reset();
        PeriodClock.reset();   
        
        // change state to lineup
        next_state = STATE_LINEUP;
        LineupClock.reset();   
        LineupClock.start();
        
        // increment period number, reset jam number
        period++;
        jam = 0;

        // reset reviews
        review_a = true;
        review_b = true;
        
      }
      
      if (c_down) { // stop halftime and timeout
        // because an official review may occur even after the period has ended
        
        next_state = STATE_TIMEOUT;
        timeout_state = 0;

        // reset halftime clock
        HalftimeClock.reset();

        // start timeout clock
        TimeoutClock.reset();
        TimeoutClock.start();
        
      }
      
    break;
    case STATE_END: // end of game

      if (c_down) { // back to timeout
        // because the bench had that one official review left and is using it now

        timeout_state = 0;
        next_state = STATE_TIMEOUT;

        // reset halftime clock
        HalftimeClock.reset();

        // start timeout clock
        TimeoutClock.reset();
        TimeoutClock.start();

      }

      if (b_down) { // start an overtime jam
        // section 1.6 of the rules

        next_state = STATE_LINEUP;
        overtime = true;

        // reset halftime clock
        HalftimeClock.reset();
        
        // change lineup time to 1 minute
        LineupClock.change(60000);
        LineupClock.reset();
        LineupClock.start();

      }

    break;
  }
    
  // update display
  draw();

  // update state
  state = next_state;
  
  delay(20); // give the poor little arduino some breathing time
  
}

void draw(){

  current_draw_t = millis();

  // only update the display after 125 ms have passed
  if ((current_draw_t - last_draw_t) > 125) {
  
    // display stuff
    display.clearDisplay();
    display.setTextSize(2);
    display.setFont(&CounterClockFont);
    display.setTextColor(WHITE);
    display.setCursor(0,10);
    display.printf("P%1d J%02d",period,jam);
    display.setCursor(0,25);
    display.printf("%02d:%02d",PeriodClock.min(),PeriodClock.sec());

    // display state
    display.setCursor(0,32);
    display.setTextSize(1);
    switch (state) {
      case STATE_WAIT: 
        display.printf("WAITING"); 
      break;
      case STATE_LINEUP: 
        if (overtime) {
          display.printf("OVERTIME LINEUP"); 
        } else {
          display.printf("LINEUP"); 
        }
      break;
      case STATE_JAM: 
        if (overtime) {
          display.printf("OVERTIME JAM"); 
        } else {
          display.printf("JAMMING"); 
        }
      break;
      case STATE_TIMEOUT: 
        switch (timeout_state) {
          case 0: display.printf("OFFICIAL T/O"); break;
          case 1: display.printf("TEAM TIMEOUT"); break;
          case 2: display.printf("OFFCL REVIEW"); break;
          case 3: display.printf("CLOCK ADJUST SEC"); break;
          case 4: display.printf("CLOCK ADJUST MIN"); break;
          case 5: display.printf("JAM# ADJUST"); break;
        }
      break;
      case STATE_HALFTIME: 
        display.printf("HALFTIME"); 
      break;
      case STATE_END: 
        display.printf("END OF GAME"); 
      break;
    }
  
    // draw big fat clock
    display.setTextSize(5);      
    switch (state) {
      case STATE_WAIT: break;
      case STATE_LINEUP: 
        display.setCursor(93,25);
        display.printf("%02d", LineupClock.sec());
      break;
      case STATE_JAM: 
        display.setCursor(63,25);
        display.printf("%1d:%02d", JamClock.min(), JamClock.sec());
      break;
      case STATE_TIMEOUT: 
        if (TimeoutClock.left()<3000) {
          display.setTextSize(4);
          display.setCursor(60,25);
          display.printf("%2d:%02d", TimeoutClock.upmin(), TimeoutClock.upsec());        
        } else {
          display.setCursor(63,25);
          display.printf("%1d:%02d", TimeoutClock.upmin(), TimeoutClock.upsec());
        }  
      break;
      case STATE_HALFTIME:
        display.setTextSize(4);
        display.setCursor(60,25);
        display.printf("%02d:%02d", HalftimeClock.upmin(), HalftimeClock.upsec());
      break;
      case STATE_END: break;
    }

    // official review indicators, with nifty blinking
    if (review_a == true && (state != STATE_TIMEOUT || review_t != 1 || (millis()%1000) > 500)) display.drawRect(105,28,3,3, WHITE);
    if (review_b == true && (state != STATE_TIMEOUT || review_t != 2 || (millis()%1000) > 500)) display.drawRect(113,28,3,3, WHITE);


    // timeout indicators
    switch (timeouts_a) {
      case 3:
        display.drawFastHLine(93,28,3, WHITE);
        display.drawFastVLine(94,28,3, WHITE);
      case 2:
        display.drawFastHLine(97,28,3, WHITE);
        display.drawFastVLine(98,28,3, WHITE);
      case 1:
        display.drawFastHLine(101,28,3, WHITE);
        display.drawFastVLine(102,28,3, WHITE);
      break;
    }

    switch (timeouts_b) {
      case 3:
        display.drawFastHLine(125,28,3, WHITE);
        display.drawFastVLine(126,28,3, WHITE);
      case 2:
        display.drawFastHLine(121,28,3, WHITE);
        display.drawFastVLine(122,28,3, WHITE);
      case 1:
        display.drawFastHLine(117,28,3, WHITE);
        display.drawFastVLine(118,28,3, WHITE);
      break;
    }

    display.display(); 
    last_draw_t = millis();
  }
}

void buzzer(){

  if (buzzer_last != buzzer_state) {
    buzzer_t = millis();
    digitalWrite(VIBE, (buzzer_state == BUZZER_OFF)?LOW:HIGH);
    buzzer_last = buzzer_state;
  }
  if ((buzzer_state != BUZZER_OFF) && ((millis() - buzzer_t) > buzzer_state)) buzzer_state = BUZZER_OFF;
  
}

void buttons(){
  
  // get current time
  current_button_t = millis();

  // reset button states
  a_up = false;
  a_down = false;
  b_up = false;
  b_down = false;
  c_up = false;
  c_down = false;

  // button states
  if ((current_button_t - last_button_t) > 10) {

    a_state = digitalRead(BUTTON_A);
    b_state = digitalRead(BUTTON_B);
    c_state = digitalRead(BUTTON_C);

    if (a_last != a_state) {
      if (a_state == LOW) {
        a_down = true;
        a_pressed = millis();
        a_duration = 0;
      } else {
        a_up = true;
        a_duration = millis() - a_pressed;
      }
      a_last = a_state;
    }
    
    if (b_last != b_state) {
      if (b_state == LOW) {
        b_down = true;
        b_pressed = millis();
        b_duration = 0;
      } else {
        b_up = true;
        b_duration = millis() - b_pressed;
      }
      b_last = b_state;
    }
    
    if (c_last != c_state) {
      if (c_state == LOW) {
        c_down = true;
        c_pressed = millis();
        c_duration = 0;
      } else {
        c_up = true;
        c_duration = millis() - c_pressed;
      }
      c_last = c_state;
    }
    
    last_button_t = millis();
  }
}


