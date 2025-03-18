#include <avr/interrupt.h>
#include <avr/io.h>
#define uchar unsigned char

//#define DBG
//#define NEGATIVE_LOGIC

#ifdef NEGATIVE_LOGIC
 #define PHIGH  LOW   // levels are swapped in negative logic
 #define PLOW   HIGH
#else
  #define PHIGH HIGH
  #define PLOW  LOW
#endif

#define EYES_PIN          5     // D5   active L
 #define EYES_ACTIVE      LOW
 #define EYES_INACTIVE    HIGH
#define PWROFF_PIN        14    // A0   N: L->shutdown; P:H->shutdown
 #define PWROFF_ACTIVE    PHIGH
 #define PWROFF_INACTIVE  PLOW
#define SENSOR_PIN        15    // A1   N: L=box is open; P: H=box is open
  #define SENSOR_ACTIVE   PHIGH // door opened
  #define SENSOR_INACTIVE PLOW  // door closed
#define SERVO_PIN         11    // D11  PWM OCR2A (PB3)


#define SENSOR_TO 200 // [ms] sensor signal glitches protection period

#ifdef DBG
 #define dbg(a) Serial.print(a)
 #define dbgln(a) Serial.println(a)
 #define LED1  PORTB|=1<<5
 #define LED0  PORTB&=~(1<<5)
 #define LED_  DDRB|=1<<5
#else
 #define dbg(a)   do {} while(0)
 #define dbgln(a) do {} while(0)
 #define LED1     do {} while(0)
 #define LED0     do {} while(0)
 #define LED_     do {} while(0)
#endif
//#define TRY(fnc, debugMsg) do if (fnc) return true; while(0)
#define TRY(fnc) do if (fnc) return true; while(0)



/* delay sensitive to box open */

#define BOX_UNKNOWN 0xAA
#define BOX_OPEN    PHIGH
#define BOX_CLOSE   PLOW
char boxState=BOX_UNKNOWN;

bool safeDelay(unsigned ms) {
  unsigned long t;
  unsigned long limit=millis()+ms;
  do {
    t=millis();
    static unsigned long nextPossibleChangeTime=0; 
    static char prevState=BOX_UNKNOWN;

    char newState=digitalRead(SENSOR_PIN);
    if (nextPossibleChangeTime<t) boxState=newState;

    if (prevState!=newState) {
      prevState=newState;
      nextPossibleChangeTime=t+SENSOR_TO;
    }
    
    if (boxState==BOX_OPEN) {
        dbg("box=");dbg((int)boxState);
        dbgln(" safeDelay returns TRUE!");
        return true;
    }
  } while (t<limit);
  return false;
}

/* servo management */

// servo SG90 expects input signal range 1.5-2.0 ms (T2 value: 96-128)
#define T2_CLOCK      6       // clock=clk/256=16us=62.5 kHz   T2_overflow=4.096ms=244Hz
#define T2_CLK_US     16      // T2 clock [us] (based on T2_CLOCK)
#define SERVO_PERIOD  20      // [ms] minimum PWM period for servo (from SG90 datasheet)
#define T2_DELAY_LOOPS ((SERVO_PERIOD)*1000/(T2_CLK_US)/256+1) // minimum T2 overflows per PWM pulse
#define REPEAT_FACTOR 10      // 

class Servo {
  static uchar volatile servoLast;    // last value submitted to servo
  static uchar volatile servoValue;   // value to be submitted
  static uchar volatile servoRepeat;  // number of times the servo should be set (to ensure stability)
  static uchar volatile servoDelay;   // number of timer periods until the next possible pulse

  protected:
  void servoAngle(uchar angle) {
    servoValue=angle;
    TCCR2B=T2_CLOCK;  // start timer if not running
  }

  Servo() {
    pinMode(SERVO_PIN, OUTPUT); digitalWrite(SERVO_PIN, LOW);
//    DDRB|=1<<PB3; PORTB&=~(1<<PB3); // much more efficient variant of the line above
    TCCR2B=0;       // stop timer
    TCNT2=0xFE;     // next interrupt will arrive soon
    servoDelay=1;   // need not to wait
    TIFR2=7;        // clear all pending interrupts
    TIMSK2=0x03;    // enable int on OC2A + TOVF2
    TCCR2A=0x03;    // stop output
  }

  public: static inline void timerOverflow() { // executed when TCNT2 becomes 0x00
    if (!--servoDelay) {
      if (servoValue!=servoLast) servoRepeat=REPEAT_FACTOR+1;  // new value?
      if (--servoRepeat) {  // servoRepeat the same value to ensure propper servo angle
        OCR2A=servoLast=servoValue;
        servoDelay=T2_DELAY_LOOPS+1;
        TCCR2A=0x83;        // OCR2A: set on BOTTOM and clear on compare match
        TCNT2=0xFF;         // update of OCR2A and start of pulse happens when TCNT2:0xFF->0x00
      } else {
        TCCR2B=0;           // stop timer
        TCNT2=0xFE;         // prepare for quick overflow
        servoDelay=1;       // no delay needed after overflow
        TCCR2A=0x03;        // stop output
      }
    }
  }
};  
volatile uchar Servo::servoLast, Servo::servoValue, Servo::servoRepeat, Servo::servoDelay;

// vector 7
ISR(TIMER2_COMPA_vect) { TCCR2A=3; };   // stop output, but keep counting
// vector 9
ISR(TIMER2_OVF_vect)   { Servo::timerOverflow(); };


/********************** DOOR **************/

class Door : protected Servo {
  private:
    static const uchar DOORCLOSE = 120;
    static const uchar DOOROPEN  = 90;
    static const int SPEED     = 600;   // steps/min
  public: 
    Door() : Servo() {
      servoAngle(DOORCLOSE);
    }
    bool open() {
      const char direction=DOORCLOSE<DOOROPEN ? 1 : -1;
      uchar angle = DOORCLOSE;
      while(angle!=DOOROPEN) {
        angle+=direction;
        servoAngle(angle);
        TRY(safeDelay(60000/SPEED));
      }
      dbgln("opened.");
      return false;
    }
    void close() {
      servoAngle(DOORCLOSE);
    }
};

class Devil : protected Door {
  static const uchar TotalShows = 3;
  static const uchar TotalActivities = 3;
  static const int DelayAfterDoorOpen = 300;
  static const int DelayBetweenShows = 5000;
  static const int DelayBetweenActivities = 30000;
  public:
  Devil() : Door() {
    pinMode(EYES_PIN, OUTPUT); 
    digitalWrite(EYES_PIN, EYES_INACTIVE);
  }
  private:
  void blinkEyes(uchar count) {
    //                   ON  OFF ON  OFF ON  END
    unsigned timing[] = {700,500,300,200,800,0};
    dbg("Devil::blinkEyes(");dbg((int)count);dbgln(")");
    count<<=1;
    digitalWrite(EYES_PIN, EYES_ACTIVE);
    for(uchar i=0; --count; i++) { 
      unsigned ms;
      ms=timing[i];
      if (!ms || safeDelay(ms)) {
        digitalWrite(EYES_PIN, EYES_INACTIVE);
        return;
      }
      digitalWrite(EYES_PIN, i&1 ? EYES_ACTIVE : EYES_INACTIVE);
    };
  }

  void show(uchar n) {
    dbg("Devil::show(");dbg((int)n);dbgln(")");
    if (!open()) if (!safeDelay(DelayAfterDoorOpen)) blinkEyes(n);
    close();
  }

  bool activity() {
    dbgln("Devil::activity()");
    for(uchar n=1;n<=TotalShows;n++) {
      show(n);
      TRY(safeDelay(DelayBetweenShows));
    }
    return false;
  }

  public: 
  bool live() {
    dbgln("Devil::live()");
    for(uchar n=TotalActivities;; n--) {
      TRY(activity());
      TRY(safeDelay(DelayBetweenActivities));
    }
    return activity();
  }

  ~Devil() {
    pinMode(PWROFF_PIN, OUTPUT);
    digitalWrite(PWROFF_PIN, PWROFF_ACTIVE);
    safeDelay(65535); // this should never happen
  }
};


void setup() {
#ifdef DBG
  Serial.begin(115200);
  Serial.println("Debug initialized.");
  LED_;
#endif
  pinMode(SENSOR_PIN, INPUT);
}

void loop() {
  Devil devil;
  dbgln("main loop starts");
  while (devil.live()) while (safeDelay(10000)) {dbgln("waiting for door close"); delay(1000); }
}
