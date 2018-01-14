// Play a file from the SD card in looping mode, from the SD card.
// Example program to demonstrate the use of the MIDFile library
//
//  Hardware required:
//  SD card interface - change SD_SELECT for SPI comms
//  SDCARD softspi
//  ----------------
//  MISO PIN 12
//  SCK PIN 13
//  MOSI PIN 11
//  CS PIN 10
//  
//  MIDI
//  -----------------
//  PIN2
//  PIN1
//  
//  LCD I2C
//  -----------------
//  SDAA4
//  SCLA5
//  
//  BUTTON_PLAY_PAUSE 0 // Digital PIN0 / Atmega328 PIN2
//  BUTTON_LOOP 3 // Digital PIN3 / Atmega328 PIN5
//  BUTTON_NEXT 4 //Digital PIN4 / Atmega328 PIN6
//
//  FUNC
//  -----------------
//  1.PLAY/PAUSE
//  2.LOOP ON / LOOP OFF
//  3.NEXT

//SDFAT & MIDI
#include <SdFat.h>
#include <MD_MIDIFile.h>
//OLED
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
SSD1306AsciiWire oled;
//CONFIG
#define USE_MIDI 1 // 0: Debug Serial 1:Send Midi Signal
#define BUTTON_PLAY_PAUSE 0 //Digital PIN0 / Atmega328 PIN2
#define BUTTON_LOOP 3 //Digital PIN3 / Atmega328 PIN5
#define BUTTON_NEXT 4 //Digital PIN4 / Atmega328 PIN6
#define WAIT_DELAY  2000  // WAIT before playing next song (ms)
#define I2C_ADDRESS 0x3C // Oled i2c address, use i2cscanner
#define ENABLE_TEMPO

#if USE_MIDI // set up for direct MIDI serial output
  #define DEBUGS(s)
  #define DEBUG(s, x)
  #define DEBUGX(s, x)
  #define SERIAL_RATE 31250
#else // don't use MIDI to allow printing debug statements
  #define DEBUGS(s)     Serial.print(s)
  #define DEBUG(s, x)   { Serial.print(F(s)); Serial.print(x); }
  #define DEBUGX(s, x)  { Serial.print(F(s)); Serial.print(x, HEX); }
  #define SERIAL_RATE 57600
#endif // USE_MIDI

// SD chip select pin for SPI comms.
// Arduino Ethernet shield, pin 4.
// Default SD chip select is the SPI SS pin (10).
// Other hardware will be different as documented for that hardware.
#define  SD_SELECT  10
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define  FNAME_SIZE    13        // 8.3 + '\0' character file names

SdFat SD;
MD_MIDIFile SMF;

uint16_t  plCount = 0; //how many songs in the playlist?

char playlist_array[10][FNAME_SIZE];
int  playlist_tempo[10];

uint8_t player_index = 0;
uint8_t play_button_prev = 1;
uint8_t next_button_prev = 1;
uint8_t loop_button_prev = 1;
uint8_t player_pause_state = 0;
uint8_t player_loop_state = 0;

size_t readField(SdFile* file, char* str, size_t size, const char* delim) {
  char ch;
  size_t n = 0;
  while ((n + 1) < size && file->read(&ch, 1) == 1) {
    // Delete CR.
    if (ch == '\r') {
      continue;
    }
    str[n++] = ch;
    if (strchr(delim, ch)) {       
        break;
    }
  }
  str[n-1] = '\0';
  return n;
}

void setPlaylistArray()
{
    SdFile    plFile;    // MIDI file
    uint16_t  count = 0;// count of files
    char      line[13];
    
    size_t n;      // Length of returned field with delimiter.
    char str[13];  // Must hold longest field with delimiter and zero byte.
    uint8_t i = 0;     // First array index.
    char inputChar;
    
    if (!plFile.open("playlist.txt", O_READ)){
      DEBUGS("PL file no open");
      displayError("PLAYLIST.TXT\nNOTFOUND");
      while(true);
    }
    
    //find playlist count 
    while(inputChar != EOF) {
      inputChar = plFile.read();
      if(inputChar == '\n') count++;
    }        

    //reset 0
    plFile.rewind();
    plCount = count;

    for (i = 0; i < plCount; i++) {
         readField(&plFile, str, sizeof(str), "|,\n");
         strcpy(playlist_array[i],str);
         DEBUGS(playlist_array[i]);
         readField(&plFile, str, sizeof(str), "|,\n");
         playlist_tempo[i] = atoi(str);
         DEBUGS(playlist_tempo[i]);
         DEBUGS("\n");
    }

    plFile.close();
}

void midiCallback(midi_event *pev)
// Called by the MIDIFile library when a file event needs to be processed
// thru the midi communications interface.
// This callback is set up in the setup() function.
{
#if USE_MIDI
  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
  {
    Serial.write(pev->data[0] | pev->channel);
    Serial.write(&pev->data[1], pev->size-1);
  }
  else
    Serial.write(pev->data, pev->size);
#endif
  DEBUG("\nM T", pev->track);
  DEBUG(":  Ch ", pev->channel+1);
  DEBUGS(" Data");
  for (uint8_t i=0; i<pev->size; i++)
  {
    DEBUGX(" ", pev->data[i]);
  }
}


void playMidi(uint8_t idx)
{
  int err = 0;
  // use the next file name and play it
  DEBUG("\nFile: ", playlist_array[idx]);
  SMF.setFilename(playlist_array[idx]);
  
  err = SMF.load();

  if (err != -1)
  {
    DEBUG("\nSMF load Error ", err);
    while (true);
  }
  //delay(WAIT_DELAY);
}

void initMidi()
{
  int err;
  // Initialize SD
  if (!SD.begin(SD_SELECT, SPI_FULL_SPEED))
  {
    DEBUGS("\nSD init fail!");
    displayError("INSERT\nSDCARD");
    while (true) ;
  }
  // Initialize MIDIFile
  SMF.begin(&SD);
  SMF.setMidiHandler(midiCallback);
  //SMF.looping(true);
}

void midiSilence(void)
// Turn everything off on every channel.
// Some midi files are badly behaved and leave notes hanging, so between songs turn
// off all the notes and sound
{
  midi_event  ev;

  // All sound off
  // When All Sound Off is received all oscillators will turn off, and their volume
  // envelopes are set to zero as soon as possible.
  ev.size = 0;
  ev.data[ev.size++] = 0xb0;
  ev.data[ev.size++] = 120;
  ev.data[ev.size++] = 0;

  for (ev.channel = 0; ev.channel < 16; ev.channel++)
    midiCallback(&ev);
}

void initDisplay()
{
  Wire.begin();                
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
  oled.setFont(System5x7);
}

void displayError( char * str)
{
    oled.clear();  
    oled.set2X();
    oled.println("ERROR");
}

void displaySongTitle(uint8_t idx)
{
  oled.clear();  
  
  oled.print("loop: ");
  oled.print(player_loop_state);

  if(player_pause_state)
  {
    oled.print("    paused");
  }
  
  oled.println();
  oled.println();
  oled.set2X();
  oled.println(playlist_array[idx]);
  oled.set1X();
  oled.print("\ntmpo: ");
  oled.print(playlist_tempo[idx]);
  oled.print(" song:");
  oled.print(idx);
  oled.print("/");
  oled.print(plCount);  
}

void initButton()
{
     pinMode(BUTTON_PLAY_PAUSE, INPUT_PULLUP);
     pinMode(BUTTON_LOOP, INPUT_PULLUP);
     pinMode(BUTTON_NEXT, INPUT_PULLUP);
}

void setup(void)
{
  Serial.begin(SERIAL_RATE);

  initDisplay();
  initMidi(); //init midi library and sdfat
  initButton(); //init player button
  
  setPlaylistArray(); //open playlist.txt and store it in song and tempo array

  displaySongTitle(0); // display first song
  playMidi(0); //play first song in the playlist

  DEBUGS("\n[MidiFile Looper]");
}

void playSongIndex(uint8_t idx)
{
    SMF.close();
    midiSilence();
    displaySongTitle(idx);
    playMidi(idx);
}

void playNextSong()
{
    player_index++;
    if (player_index > (plCount-1) ) player_index = 0; // loop playlist  
    playSongIndex(player_index);          
}

void loop(void)
{
  // play the file
  if (!SMF.isEOF())
  {
    SMF.getNextEvent();

    #ifdef ENABLE_TEMPO
    SMF.setTempo(playlist_tempo[player_index]);
    #endif
    
    // PLAY BUTTON
    // PLAY BUTTON
    // PLAY BUTTON
    // PLAY BUTTON

    uint8_t btnVal = digitalRead(BUTTON_PLAY_PAUSE);
    if (btnVal != play_button_prev && btnVal == LOW)
    {
      player_pause_state = !player_pause_state;
      SMF.pause(player_pause_state);
      displaySongTitle(player_index);      
    }
    play_button_prev = btnVal;

    // NEXT BUTTON
    // NEXT BUTTON
    // NEXT BUTTON
    // NEXT BUTTON

    btnVal = digitalRead(BUTTON_NEXT);
    if (btnVal != next_button_prev && btnVal == LOW)
    {
       player_pause_state = 0;
       playNextSong();
    }
    next_button_prev = btnVal;

    // LOOP BUTTON
    // LOOP BUTTON
    // LOOP BUTTON
    // LOOP BUTTON

    btnVal = digitalRead(BUTTON_LOOP);
    if (btnVal != loop_button_prev && btnVal == LOW)
    {
      player_loop_state = !player_loop_state;
      displaySongTitle(player_index);
    }
    loop_button_prev = btnVal;
    
  }else{
    if (player_loop_state == false)
    {
       playNextSong();
    }else{
       //play/loop song based on player_index
       playSongIndex(player_index);      
    }
  }
}
