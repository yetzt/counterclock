
#ifndef Timer_h
#define Timer_h

#include<Arduino.h>

class Timer {

	private:

		// end time, the amount of time on start
		unsigned long v;

		// start time the millis() value on start or virtual start time on resume()
		unsigned long t;

		// current time, set when Timer() called, for internal purposes
		unsigned long c;

		// keep elapsed time on pause
		unsigned long e;

		// has one second elapsed?
		bool tick;

		// keep last second
		unsigned int lastsec = 0;

		// flag if timer is running
		bool running;

	public:

		Timer(unsigned long end) {
			running = false;
			v = end;
			e = 0;
		}
		
		// run timer
		boolean run(){
			
			tick = false;

			if (running) {
				// update internal clock
				c = millis();
				// update elapsed time
				e = (c-t);
				
				if (lastsec != sec()) {
					lastsec = sec();
					tick = true;
				}
				
				// if time is zero, halt and set elapsed to amount
				if (v <= e) {
					running = false;
					e = v;
					tick = true;
				}
				
			}
		}
		
		// start timer
		void start(){

			// update internal clock
			c = millis();
			// set start time
			t = (c - e);

			// set running status
			running = true;

		}
		
		// set remaining time to arbitrary number
		void set (unsigned int s) {
			c = millis();
			t = (c-(v-s));
			e = (c-t);
			tick = true;
		};
		
		// set end time to differen value
		void change (unsigned int s) {
			c = millis();
			v = s;
		};
		
		// pause timer
		void pause() {
			if (running) {
				// set running status
				running = false;
				// update internal clock
				c = millis();
				// get current elapsed time
				e = (c-t);
			}
		}
		
		// i spaused
		bool paused() {
			return (!running);
		}

		// second marker
		bool ticked() {
			return tick;
		}
		
		// reset
		void reset(){
			running = false;
			e = 0;
		}
		
		// check if timer is zero
		bool done() {
			return (e>=v);
		}
		
		// return hours
		unsigned long hour() {
			return (e<v) ? ((v-e)/3600000) : 0;
		}

		// return minutes
		unsigned long min() {
			return (e<v) ? (((v-e)/60000)%60) : 0;
		}

		// return seconds
		unsigned long sec() {
			return (e<v) ? (((v-e)/1000)%60) : 0;
		}

    // return ceiled hours
    unsigned long chour() {
      return (e<v) ? (int)ceil((float)((v-e)/3600000)) : 0;
    }

    // return ceiled minutes
    unsigned long cmin() {
      return (e<v) ? (((int)ceil((float)(v-e)/60000))%60) : 0;
    }

    // return ceiled seconds
    unsigned long csec() {
      return (e<v) ? (((int)ceil((float)(v-e)/1000))%60) : 0;
    }


		unsigned long ms() {
			return (e<v) ? ((v-e)%1000) : 0;
		}
		
		// return hours elapsed
		unsigned long uphour() {
			return (e<v) ? ((e)/3600000) : 0;
		}

		// return minutes elapsed
		unsigned long upmin() {
			return (e<v) ? (((e)/60000)%60) : 0;
		}

		// return seconds elapsed
		unsigned long upsec() {
			return (e<v) ? (((e)/1000)%60) : 0;
		}

		// return millis elapsed
		unsigned long upms() {
			return (e<v) ? ((e)%1000) : 0;
		}

		// return seconds left
		unsigned long left() {
			return (e<v) ? ((v-e)/1000) : 0;
		}
		
		// return seconds elapsed
		unsigned long gone() {
			return (e<v) ? (e/1000) : 0;
		}
		
		// return ms left
		unsigned long total() {
			return (e<v) ? (v-e) : 0;
		}

};

#endif
