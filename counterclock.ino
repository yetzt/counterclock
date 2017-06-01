#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "CounterClockFont.h"
#include "CounterClockTimer.h"

// wifi config
const char* ssid     = "scorehub";
const char* password = "********";

// enable wifi?
bool wifi = false;

WiFiUDP Udp;

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

#define WIFI_OFF 0
#define WIFI_CLOCK 1
#define WIFI_ALL 2

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

// remaining period time at end of jam
int remtime = 0;

// overtime mode bit
bool overtime = false;

// wifi connection state
unsigned long wifi_init;
int wifi_level = 0;

char clck[8];

void setup() {

  // initialize the wifi
  if (wifi) {
    WiFi.mode(WIFI_STA);
    WiFi.hostname("counterclock");
    // WiFi.setNoDelay(true);
    WiFi.begin(ssid, password);
    wifi_init = millis();
  } else {
    // send wifi to rest, since we don't use it
    WiFi.forceSleepBegin();
  }

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

  delay(1500);

  if (wifi) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setFont(&CounterClockFont);
    display.setCursor(0,32);
    display.print("WIFI ");
    while (WiFi.status() != WL_CONNECTED && wifi == true) {
      if ((millis() - wifi_init) > 20000) {
        wifi = false;
        WiFi.forceSleepBegin();
      } else {
        delay(500);
        display.print(".");
        display.display();
      }
    }
    // conclude wifi status
    if (wifi == true) {
      display.print(" OK");
    } else {
      display.print(" ERR");
    }
    delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont(&CounterClockFont);
  display.setTextWrap(false);
  display.setCursor(0,7);
  display.println(WiFi.localIP());
  display.print(WiFi.gatewayIP());
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

        if (c_down) {
          wifi_level = WIFI_OFF;
          wifi = false;
          WiFi.forceSleepBegin();
        }
        
        if (b_down) wifi_level = WIFI_CLOCK;
        if (a_down) wifi_level = WIFI_ALL;

        // send game start
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("gst");
          Udp.endPacket();
        }

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

        // send timeout
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("oto");
          Udp.endPacket();
        }


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

          // send end of period/game
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write((period == 1)?"pen":"eog");
            Udp.endPacket();
          }


      }

      // buzzing: medium on 5, short on 4-1, long on 0
      if (LineupClock.left() <= 5 && LineupClock.ticked()) buzzer_state = (LineupClock.left() == 0) ? BUZZER_LONG : (LineupClock.left() == 5) ? BUZZER_MEDIUM : BUZZER_SHORT;

      if (LineupClock.done()) { // when lineup clock has ran out, start jam
        next_state = STATE_JAM;

        // start jam clock
        JamClock.reset();
        JamClock.start();

        // adjust clock
        clocksync();

        // send jam start
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("jst");
          Udp.endPacket();
        }

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

        // keep remaining time on clock to check
        remtime = PeriodClock.total();

        // start timeout clock
        TimeoutClock.reset();
        TimeoutClock.start();

        // send timeout
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("oto");
          Udp.endPacket();
        }

      } else if ((a_down || b_down) && !overtime && JamClock.left() < 117) { // end jam
        // as a safeguard against ending a jam by trying to start it on mark,
        // allow calling off a jam after 3 seconds only
        // no one ever will gain lead and call in 3 seconds
        // also: no calling off overtime jams

        // keep remaining time on clock to check
        remtime = PeriodClock.total();

        // end jam and lineup
        JamClock.set(0);
        buzzer_state = BUZZER_SHORT;

        // FIXME: send un-jam

      } else if (JamClock.left() <= 5 && JamClock.ticked()) {

        // buzz end of jam
        buzzer_state = (JamClock.left() == 0) ? BUZZER_LONG : BUZZER_SHORT;

      }

      if (JamClock.done()) { // has the jam come to an end?

        remtime = PeriodClock.total();

        if (PeriodClock.done()) { // is the period over?

          next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
          HalftimeClock.reset();
          HalftimeClock.start();

          // send end of jam + end of period/game
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("jen");
            Udp.endPacket();

            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write((period == 1)?"pen":"eog");
            Udp.endPacket();
          };

        } else {

          // new lineup
          next_state = STATE_LINEUP;
          JamClock.reset();
          LineupClock.reset();
          LineupClock.start();

          // send end of jam
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("jen");
            Udp.endPacket();
          };

          clocksync();

        }

      }

    break;
    case STATE_TIMEOUT: // welcome to the marvellous world of timeouts

      // cycle through the six timeout states (ot, tto, or and adjustments)
      if (c_down) timeout_state = (timeout_state+1)%6;

      // FIXME: this should better be a switch()
      if (timeout_state == 0) { // official timeout

        if (a_down || b_down) { // end official timeout

          TimeoutClock.reset();

          // if period clock is done, go to halftime
          if (PeriodClock.done()) {

            next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
            HalftimeClock.reset();
            HalftimeClock.start();

            // send resume + end of period/game
            if (wifi && wifi_level == WIFI_ALL) {
              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write("rsm");
              Udp.endPacket();

              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write((period == 1)?"pen":"eog");
              Udp.endPacket();
            };

          } else {

            // resume to lineup
            next_state = STATE_LINEUP;
            LineupClock.reset();
            LineupClock.start();

            // send resume
            if (wifi && wifi_level == WIFI_ALL) {
              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write("rsm");
              Udp.endPacket();
            };

            // resume period clock after official timeout when < 30 sec was on the period clock at the end of the last jam and a is pressed
            // keep paused if b is pressed, to avoid cancellation of jams through ot after tt/or
            if (remtime < 30000 && a_down) {
              PeriodClock.start();

              // send resume period clock
              if (wifi && wifi_level == WIFI_ALL) {
                Udp.beginPacket(WiFi.gatewayIP(), 16016);
                Udp.write("rpc");
                Udp.endPacket();
              };
            }

            clocksync();

          }

        }

      } else if (timeout_state == 1) { // team timeout, auto resume after 1:00

        // set number of timeouts, button b is confusingly team a and vice versa
        // the nifty modulo rollover is intentional, in case of nessecary correction
        if (b_down && TimeoutClock.gone() < 60) {
          timeouts_a = (timeouts_a+3)%4;

          // send team timeout for team 1
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("tto:1");
            Udp.endPacket();
          };

        }

        if (a_down && TimeoutClock.gone() < 60) {
          timeouts_b = (timeouts_b+3)%4;

          // send team timeout for team 2
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("tto:2");
            Udp.endPacket();
          };

        }

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

              // send resume + end of period/game
              if (wifi && wifi_level == WIFI_ALL) {
                Udp.beginPacket(WiFi.gatewayIP(), 16016);
                Udp.write("rsm");
                Udp.endPacket();

                Udp.beginPacket(WiFi.gatewayIP(), 16016);
                Udp.write((period == 1)?"pen":"eog");
                Udp.endPacket();
              };

            } else {

              // resume to lineup
              next_state = STATE_LINEUP;
              LineupClock.reset();
              LineupClock.start();

              // resume game
              if (wifi && wifi_level == WIFI_ALL) {
                Udp.beginPacket(WiFi.gatewayIP(), 16016);
                Udp.write("rsm");
                Udp.endPacket();
              };

              clocksync();

            }

          } else if (TimeoutClock.gone() >= 55) { // a short buzz on the last 5 seconds

            buzzer_state = BUZZER_SHORT;

          } else if (TimeoutClock.gone() == 50) { // give 10 second notice buzz (some statsbooks recommend giving a 10 second warning)

            buzzer_state = BUZZER_MEDIUM;

          }

        }

      } else if (timeout_state == 2) { // official review

        // set the team who has taken the review with B or A button
        if (b_down && TimeoutClock.gone() < 60) {
          review_t = 1;

          // send official review for team 1
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("orv:1");
            Udp.endPacket();
          };

        }
        if (a_down && TimeoutClock.gone() < 60) {
          review_t = 2;

          // send official review for team 2
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("orv:2");
            Udp.endPacket();
          };

        }

        // after 60 seconds, game may resume
        if ((a_down || b_down) && TimeoutClock.gone() >= 60) {

          // if review is not retained (B button), set review to false
          if (b_down && review_t == 1) {
            review_a = false;

            // send lost official review for team 1
            if (wifi && wifi_level == WIFI_ALL) {
              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write("lrv:1");
              Udp.endPacket();
            };

          }

          if (b_down && review_t == 2) {
            review_b = false;

            // send lost official review for team 2
            if (wifi && wifi_level == WIFI_ALL) {
              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write("lrv:2");
              Udp.endPacket();
            };


          }

          // send retained review for team 1
          if (a_down && review_t == 1 && wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("rrv:1");
            Udp.endPacket();
          }

          // send retained review for team 2
          if (a_down && review_t == 2 && wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("rrv:2");
            Udp.endPacket();
          }

          // reset review team
          review_t = 0;

          // reset timeout clock
          TimeoutClock.reset();

          // resume game
          if (wifi && wifi_level == WIFI_ALL) {
            Udp.beginPacket(WiFi.gatewayIP(), 16016);
            Udp.write("rsm");
            Udp.endPacket();
          };

          // if period clock is done, go to halftime
          if (PeriodClock.done()) {

            next_state = (period == 1) ? STATE_HALFTIME : STATE_END;
            HalftimeClock.reset();
            HalftimeClock.start();

            if (wifi && wifi_level == WIFI_ALL) {
              Udp.beginPacket(WiFi.gatewayIP(), 16016);
              Udp.write((period == 1)?"pen":"eog");
              Udp.endPacket();
            };

          } else {

            // resume to lineup
            next_state = STATE_LINEUP;
            LineupClock.reset();
            LineupClock.start();

            clocksync();

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

        // resume game
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("rsm");
          Udp.endPacket();
        };

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

        // official timeout
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("oto");
          Udp.endPacket();
        };

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

        // official timeout
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("oto");
          Udp.endPacket();
        };

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

        // overtime jam
        if (wifi && wifi_level == WIFI_ALL) {
          Udp.beginPacket(WiFi.gatewayIP(), 16016);
          Udp.write("otj");
          Udp.endPacket();
        };

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
    display.printf("%02d:%02d",PeriodClock.cmin(),PeriodClock.csec());

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
        display.printf("%02d", LineupClock.csec());
      break;
      case STATE_JAM:
        display.setCursor(63,25);
        display.printf("%1d:%02d", JamClock.cmin(), JamClock.csec());
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

void clocksync(){
  if (!wifi || wifi_level == WIFI_OFF) return;
  Udp.beginPacket(WiFi.gatewayIP(), 16016);
  Udp.write("clk:");
  itoa(PeriodClock.total(), clck, 10);
  Udp.write(clck);
  Udp.endPacket();
}
