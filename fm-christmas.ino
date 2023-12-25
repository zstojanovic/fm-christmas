#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/triangle_warm8192_int8.h>
#include <mozzi_midi.h>
#include <ADSR.h>
#include <EventDelay.h>
#include <RollingAverage.h>
#include "song.h"

#define CONTROL_RATE 128

// Mozzi audio output is on pin 9
#define ECHO_PIN 12 // HY-SRF05 ultrasonic sensor with echo and trig on pins 12 and 13
#define TRIG_PIN 13
#define INPUT_PIN 0 // 10K potentiometer middle pin
#define BUTTON_PIN 7 // button to change tempo
#define LED_PIN 8 // LED to blink in rythm

Oscil <TRIANGLE_WARM8192_NUM_CELLS, AUDIO_RATE> carrier(TRIANGLE_WARM8192_DATA);
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> modulator(SIN2048_DATA);
ADSR <CONTROL_RATE, AUDIO_RATE> envelope;

RollingAverage <float, 16> average; // used to smooth out the distance measurement

float distance = 0.0;  // in cm

typedef enum state {
  ready,
  pulseStarted,
  pulseSent,
  waitForEchoEnd,
  waitForNewPulse
} state_t;

state_t sonarState = ready;

// sizeof gives the number of bytes, each int value is composed of two bytes (16 bits)
// there are two values per note (pitch and duration), so for each note there are four bytes
int notes = sizeof(melody) / sizeof(melody[0]);
int tempo = 120;
int wholenote = (60000 * 4) / tempo; // this calculates the duration of a whole note in ms
int divider = 0, noteDuration = 0;
int thisNote = 0;
int currentNote;

EventDelay startNote;
EventDelay endNote;

int sensorValue;
float deviation;

int lastButtonState = HIGH;

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  envelope.setADLevels(255,64);
  envelope.setTimes(50,150,10000,50); // 10000 is so the note will sustain 10 seconds unless a noteOff comes

  carrier.setFreq(440);
  modulator.setFreq(100);
  startMozzi(CONTROL_RATE);
}

void updateControl() {
  int buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == LOW && lastButtonState == HIGH) {
    tempo = tempo + 40;
    if (tempo >= 280) tempo = 80;
    wholenote = (60000 * 4) / tempo;
  }
  lastButtonState = buttonState;

  sensorValue = mozziAnalogRead(INPUT_PIN);
  if (distance <= 85) {
    deviation = (10 * (85 - distance));
  } else {
    deviation = 0;
  }

  playSong();
  envelope.update();
}

AudioOutput_t updateAudio(){
  nonBlockingPing();

  return MonoOutput::from16Bit(envelope.next() * carrier.phMod(modulator.next()*deviation));
}

void loop() {
  audioHook(); // required here
}

void playSong() {
  if(startNote.ready()){
    // calculates the duration of each note
    divider = melody[thisNote + 1];
    if (divider > 0) {
      // regular note, just proceed
      noteDuration = (wholenote) / divider;
    } else if (divider < 0) {
      // dotted notes are represented with negative durations
      noteDuration = (wholenote) / abs(divider);
      noteDuration *= 1.5; // increases the duration in half for dotted notes
    }

    currentNote = melody[thisNote] - 12; // quick and dirty transpose
    handleNoteOn(1,currentNote,127);
    startNote.set(noteDuration);
    startNote.start();
    endNote.set(noteDuration * (sensorValue / 1024.0f));
    endNote.start();

    thisNote = thisNote + 2;
    if (thisNote >= notes) thisNote = 0;
  }
  if(endNote.ready()){
    handleNoteOff(1,currentNote,0);
  }
}

void handleNoteOn(byte channel, byte note, byte velocity) {
  float f = mtof(float(note));
  carrier.setFreq(f);
  modulator.setFreq(f / distance * 20);
  envelope.noteOn();
  digitalWrite(LED_PIN,HIGH);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  envelope.noteOff();
  digitalWrite(LED_PIN,LOW);
}

inline void nonBlockingPing(void) {
  static long trigTime = 0;  // us
  long elapsed = mozziMicros() - trigTime;

  switch (sonarState) {
    case ready:  // start triggering a pulse
      {
        if (0 == digitalRead(ECHO_PIN)) {
          trigTime = mozziMicros();
          digitalWrite(TRIG_PIN, HIGH);
          sonarState = pulseStarted;
        }
      }
      break;

    case pulseStarted:  // finish the pulse
      {
        if (elapsed > 10) {
          digitalWrite(TRIG_PIN, LOW);
          sonarState = pulseSent;
        }
      }
      break;

    case pulseSent:  // start counting when echo raises
      {
        if (1 == digitalRead(ECHO_PIN)) {
          sonarState = waitForEchoEnd;
        }
      }
      break;

    case waitForEchoEnd:  // stop counting when echo falls
      {
        if (0 == digitalRead(ECHO_PIN)) {
          distance = average.next(us2cm(elapsed));

          sonarState = waitForNewPulse;
        }

        // did a time-out occur ?
        if (elapsed > 120000) {
          sonarState = ready;
        }
      }
      break;

    case waitForNewPulse:  // wait before new pulse
      {
        if (elapsed > 120000) {  // TODO: find minimum possible
          sonarState = ready;
        }
      }
      break;

    default:
      // nothing for now
      break;
  }
}

inline float us2cm(float microseconds) {
  // The speed of sound is 340 m/s or 29 microseconds per centimeter.
  return microseconds / 58.0;  // twice 29 for round trip
}

inline float cm2us(float centimeters) {
  // The speed of sound is 340 m/s or 29 microseconds per centimeter.
  return centimeters * 58.0;  // twice 29 for round trip
}
