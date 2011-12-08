
#include <FatReader.h>
#include <SdReader.h>
#include <avr/pgmspace.h>
#include "WaveUtil.h"
#include "WaveHC.h"

#include "Arduino.h"
int freeRam(void);
void sdErrorCheck(void);
void setup();
void loop();
void playfile(char *name);
SdReader card;    // This object holds the information for the card
FatVolume vol;    // This holds the information for the partition on the card
FatReader root;   // This holds the information for the filesystem on the card
FatReader f;      // This holds the information for the file we're play
WaveHC wave;      // This is the only wave (audio) object, since we will only play one at a time

int freeRam(void)
{
  extern int  __bss_end; 
  extern int  *__brkval; 
  int free_memory; 
  if((int)__brkval == 0) {
    free_memory = ((int)&free_memory) - ((int)&__bss_end); 
  }
  else {
    free_memory = ((int)&free_memory) - ((int)__brkval); 
  }
  return free_memory; 
} 

void sdErrorCheck(void)
{
  if (!card.errorCode()) return;
  putstring("\n\rSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  putstring(", ");
  Serial.println(card.errorData(), HEX);
  while(1);
}


void setup() {
  byte i;

  Serial.begin(28800);
  putstring_nl("WaveHC with ");

  putstring("Free RAM: ");       // This can help with debugging, running out of RAM is bad
  Serial.println(freeRam());      // if this is under 150 bytes it may spell trouble!

  // Set the output pins for the DAC control. This pins are defined in the library
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);

  // pin13 LED
  pinMode(13, OUTPUT);

  //  if (!card.init(true)) { //play with 4 MHz spi if 8MHz isn't working for you
  if (!card.init()) {         //play with 8 MHz spi (default faster!)  
    putstring_nl("Card init. failed!");  // Something went wrong, lets print out why
    sdErrorCheck();
    while(1);                            // then 'halt' - do nothing!
  }

  // enable optimize read - some cards may timeout. Disable if you're having problems
  card.partialBlockRead(true);

  // Now we will look for a FAT partition!
  uint8_t part;
  for (part = 0; part < 5; part++) {     // we have up to 5 slots to look in
    if (vol.init(card, part)) 
      break;                             // we found one, lets bail
  }
  if (part == 5) {                       // if we ended up not finding one  :(
    putstring_nl("No valid FAT partition!");
    sdErrorCheck();      // Something went wrong, lets print out why
    while(1);                            // then 'halt' - do nothing!
  }

  // Lets tell the user about what we found
  putstring("Using partition ");
  Serial.print(part, DEC);
  putstring(", type is FAT");
  Serial.println(vol.fatType(),DEC);     // FAT16 or FAT32?

  // Try to open the root directory
  if (!root.openRoot(vol)) {
    putstring_nl("Can't open root dir!"); // Something went wrong,
    while(1);                             // then 'halt' - do nothing!
  }

  // Whew! We got past the tough parts.
  putstring_nl("Ready!");


  TCCR2A = 0;
  TCCR2B = 1<<CS22 | 1<<CS21 | 1<<CS20;

  //Timer2 Overflow Interrupt Enable
  TIMSK2 |= 1<<TOIE2;


}

SIGNAL(TIMER2_OVF_vect) {

}



#define FLATZ 28
#define MOVEZ 6

int16_t tiltx, tilty, tiltz;
int16_t tiltxp, tiltyp, tiltzp;
uint8_t current_file, file_just_changed, change_counter;
uint16_t wait_count = 0;
uint32_t new_position, stutter_time, stutter_count;
uint16_t average;
uint16_t bucket[4];
uint16_t bucket_position;
uint32_t newsamplerate;

char ff[][7] = {
  "01.WAV","02.WAV","03.WAV","04.WAV","05.WAV","06.WAV","07.WAV","08.WAV","09.WAV","10.WAV","11.WAV","12.WAV","13.WAV","14.WAV","15.WAV"};

byte i;
static byte playing = -1;



void loop() {


  if(current_file != playing) {

    playfile(ff[current_file]);
    wave.setSampleRate(newsamplerate);
    playing = current_file;
  }

  if (! wave.isplaying) {
    playing = -1;
  }


  delay(1);

  wait_count++;
  if(wait_count > 10) {

    // ========================================== switch sounds ==============
    tiltx = analogRead(0) >> 4;

    if (tiltx != tiltxp) {
      tiltxp = tiltx;

      if(tiltx > 46 && file_just_changed == 0) {
        current_file++;
        current_file %= 15;
        file_just_changed = 1;
        //change_counter = 0;
      }

      //change_counter++;

      //if(change_counter > 80) file_just_changed = 1;
      if(tiltx < 26 && file_just_changed == 1) file_just_changed = 0;
    }
    

    // ========================================== modify speed ==============
    tilty = analogRead(1) >> 4;
    
    if(tilty != tiltyp) {
      bucket[bucket_position] = tilty;
      average = (bucket[0] + bucket[1] + bucket[2] + bucket[3]) >> 2;
      bucket_position++;
      bucket_position %= 4;
      newsamplerate = 8500 + (average - 15)*500;

      wave.setSampleRate(newsamplerate);
    }

    tiltyp = tilty;


    // ========================================== stutter ==============

    stutter_count++;
    //tiltz = analogRead(0) >> 4;

    tiltz = tiltx;

    if(tiltz > (FLATZ - MOVEZ) && tiltz < (FLATZ + MOVEZ)) stutter_count = 0;

    stutter_time = 40 - (abs(FLATZ - tiltz) * 3);

    if(stutter_count > stutter_time) {
      if(tiltz > FLATZ) new_position = 0;
      else new_position = 10000;
      wave.seek(new_position);  
      stutter_count = 0;
    }

    tiltzp = tiltz;



    wait_count = 0;
  }
}



void playfile(char *name) {
  // see if the wave object is currently doing something
  if (wave.isplaying) {// already playing something, so stop it!
    wave.stop(); // stop it
  }
  // look in the root directory and open the file
  if (!f.open(root, name)) {
    putstring("Couldn't open file "); 
    Serial.print(name); 
    return;
  }
  // OK read the file and turn it into a wave object
  if (!wave.create(f)) {
    putstring_nl("Not a valid WAV"); 
    return;
  }

  // ok time to play! start playback
  wave.play();
}







