// libraries
#include <Keypad.h>              //(included with Arduino IDE)
#include <Timer.h>               //http://playground.arduino.cc/Code/Timer
#include <SoftwareSerial.h>      //(included with Arduino IDE)
#include <DFPlayer_Mini_Mp3.h>   //http://github.com/DFRobot/DFPlayer-Mini-mp3
#include <EEPROM.h>              //(included with Arduino IDE)

//#define debug_mode
//#define interrupt_mode 

// === Game SFX machine - SOUNDS TO PLAY ===========================================
//
// --- Usage - Keypad actions ------------------------------------------------------
// press A..D to quickly choose sound library(folder)
// press * and then repeatedly 0..9 to enter up to 2-digit number (99 max) followed 
//       by # to choose sound bank
// press # and then press 0..9 to set sound volume level
// press repeatedly 0..9 to enter up to 3-digit number (255 max) followed by # to 
//       choose sound in active bank
// ---------------------------------------------------------------------------------
// PIN Usage 
//                  INT INT
// DIGITAL  TX1 RX1 D02 D03 D04 D05 D06 D07 D08 D09 D10 D11 D12 D13
//                   X       X   X   X   X          SRX STX
//
// ANALOG   A00  A01  A02  A03  A04  A05  A06  A07
//           X    X    X    X         X
// ---------------------------------------------------------------------------------

// ===============================================================
// ==                      HW peripherals                       ==
// ===============================================================

// --- LED - Integrated LED diode --------------------------------
#define STATUS_LED_PIN LED_BUILTIN

// --- RTC - real time clock module DS3231 -----------------------
// RTC external object from DS3232RTC

// --- Keypad 4x4 ------------------------------------------------
// Keypad rows and columns
const byte rows = 4;
const byte columns = 4;
// Keypad char map
char keys[rows][columns] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Keypad pins
#ifdef interrupt_mode
  uint8_t RowPins[rows]       = {4, 5, 6, 7};      // digital pins
  uint8_t ColumnPins[columns] = {A3, A2, A1, A0};  // analog pins
#else
  uint8_t RowPins[rows]       = {2, 3, 4, 5};      // digital pins
  uint8_t ColumnPins[columns] = {A3, A2, A1, A0};  // analog pins
#endif

// Keypad object
Keypad keyboard = Keypad( makeKeymap(keys), RowPins, ColumnPins, rows, columns);

// --- MP3 - DFPlayer mini mp3 module ----------------------------
SoftwareSerial mp3Serial(10, 11); // RX, TX
#ifdef interrupt_mode
  #define MP3_STATUS_PIN  2  // pin mp3 status flag LOW means Busy/Playing
#else
  #define MP3_STATUS_PIN  9  // pin mp3 status flag LOW means Busy/Playing
#endif

// --- Vin analog voltmeter (divider 100k/10k) -------------------
#define VOLTMETER_PIN A5
const float Aref = 1.18;
const float magic_const = (110/10 * Aref / 1024);

// ===============================================================
// ==                       SW modules                          ==
// ===============================================================

// --- Core ------------------------------------------------------
#define MAX_KEYPAD_BUFFER_LENGTH 3
String keypadbuffer = ""; // input string buffer to read up to 3-digit numbers
uint8_t glsb;             // current base - mp3 file folder - A-1, B-2, C-3, D-4..99

const int E_ADDR_SIGNATURE               = 0;
const int E_ADDR_PRIMARY_VOLUME_MEMORY   = 1;
const int E_ADDR_SECONDARY_VOLUME_MEMORY = 2;
const int E_ADDR_LAST_LIBRARY            = 3;

const float MINIMUM_BATTERY_LEVEL = 6.2;  // low battery voltage limit

// --- Action battery timer, key input timer, idle timer ---------------------
Timer b, k, t;

// --- State machines --------------------------------------------
// (SM: STATE: 1-Start,2-SelectMode,3-SelectSound,4-SetVolume TRANSITION: 1-1,1-2,2-1,1-3,3-3,3-1,1-4,4-1)
#define SM_START    1
#define SM_SELECTMODE 2
#define SM_SELECTSOUND  3 
#define SM_SETVOLUME  4

// (IDLE: STATE: 1-Start,2-Waiting,3-Nervous,4-Forgotten TRANSITION: 1-2,2-1,2-3,3-1,3-4,4-1)
#define IDLE_START    1
#define IDLE_WAITING  2
#define IDLE_NERVOUS  3
#define IDLE_FORGOTTEN  4

struct StateMachine{
  uint8_t state;
  uint8_t last_state;
} sm, idlesm;

// --- MP3 - DFPlayer mini mp3 module ----------------------------
#define MAX_VOLUME_LEVEL 30
#define DEFAULT_SOUND_VOLUME 15   // 0..30
#define SYS_WARNING_VOLUME_LEVEL 20  // 0..30

#ifdef interrupt_mode
  volatile bool mp3_status_changed = false;
#endif

uint8_t act_volume = DEFAULT_SOUND_VOLUME;
bool sys_volume_level_active = false;

// ===============================================================
// ==                      S E T U P                            ==
// ===============================================================
void setup() {
  // Serial communication 38400 baud
  Serial.begin(38400);
  Serial.println(">> Serial communication Initialized 38400");
  Serial.println("------ Speaking library, Firmware version 0.9");

  Serial.println(">> Core initialization");
    Serial.print("  >> Status LED on PIN ");
	  Serial.print(STATUS_LED_PIN);
	  Serial.print(" ...");
	  // Pin 13 has an LED connected on most Arduino boards:
	  PinMode(STATUS_LED_PIN, OUTPUT);
	  digitalWrite(STATUS_LED_PIN, HIGH);
	Serial.println("Initialized OK");

	Serial.print("  >> State machines...");
	  sm.state = SM_START;
	  idlesm.state = IDLE_START;
	Serial.println("Initialized OK");

	Serial.print("  >> Battery monitor...");
	  // battery level monitoring
	  analogReference(INTERNAL); // use the internal ~1.1volt reference  | change (INTERNAL) to (INTERNAL1V1) for a Mega

	#ifdef debug_mode
	  b.every(5 * 1000L, oncheckBatteryLevel);     // every 5 sec low battery test
	#else
	  b.after(10 * 1000L, oncheckBatteryLevel);    // 10 sec to low battery test
	  b.every(1 * 60 * 1000L, oncheckBatteryLevel);  // every 5 min low battery test
	#endif

	Serial.println("Initialized OK");
  Serial.println("Core Initialized OK");

  Serial.println(">> MP3 Player Initialization");
    pinMode(MP3_STATUS_PIN, INPUT);
    mp3Serial.begin(9600);  // MP3 serial communication setup
    mp3_set_serial (mp3Serial); //set Serial for DFPlayer-mini mp3 module
    delay(1);  //wait 1ms for mp3 module to recover
    mp3_set_volume (e_readVolume());
    delay(10);  //wait 10ms for mp3 module to recover
  	sound_syswelcome();
    glsb = e_readLastLibrary();  // set last library

	Serial.print("  >> Volume (0..30)...");
	Serial.println(act_volume);
	Serial.print("  >> Library (0..100)...");
	Serial.println(glsb);

	Serial.print("  >> Playback monitor on PIN");
	Serial.print(MP3_STATUS_PIN);

#ifdef interrupt_mode
	// stop sound playback detection
	attachInterrupt(digitalPinToInterrupt(MP3_STATUS_PIN), onSoundPlayingStoped, RISING);
	Serial.print("Interrupt mode");
#endif

	Serial.println();
  Serial.println("MP3 Initialized OK");

  digitalWrite(STATUS_LED_PIN, LOW);
}

// ===============================================================
// ==                        L O O P                            ==
// ===============================================================

void loop() {

  // mandatory timer call
  t.update();
  b.update();
  k.update();

  // read pressed key from keypad
  char _key = keyboard.getKey();

#ifdef interrupt_mode
  // check sound activity
  if (mp3_status_changed){
    mp3_status_changed = false;
	_restartidletimer();  // sound playing stopped. restart idle timer
  }
#endif

  // pressed key processing
  if (_key){
	// Some key pressed. user is active. restart idle timers
	_restartidletimers();  

    #ifdef debug_mode
	  Serial.print("Pressed key: ");
	  Serial.println(_key);
	  Serial.print(sm.state);
	  Serial.print("->");
    #endif

	// Keypressed-based decision machine
    switch (sm.state){
    case SM_START: // Initial
      if (CNs1t1(_key)) { sm.last_state = SM_START; TNs1t1(_key); }        // A..D game legend set 1..4 shortcut
      else if (CNs1t2(_key)) { sm.last_state = SM_START;  TNs1t2(); }      // * -> SM_SELECTMODE:
      else if (CNs1t4(_key)) { sm.last_state = SM_START; TNs1t4(); }       // # -> SM_SETVOLUME:
      else if (CNs1t3(_key)) { sm.last_state = SM_START; TNs1t3(_key); }   // 0..9 -> SM_SELECTSOUND:
        break;
    case SM_SELECTMODE: // * reading base
      if (CNs2t1(_key)) { sm.last_state = SM_SELECTMODE; TNs2t1(_key); }   // A..D/0..9# +keybuffer || -> SM_START
      else { sm.last_state = SM_SELECTMODE; TNs2t2(); }                    // * -> SM_SELECTMODE:
        break;
    case SM_SELECTSOUND: // 0..9 reading sound index
      if (CNs3t3(_key)) { sm.last_state = SM_SELECTSOUND; TNs3t3(_key); }  // 0..9 +keybuffer
      else if (CNs3t1(_key)) { sm.last_state = SM_SELECTSOUND; TNs3t1(); } // # -> SM_START:
      else if (CNs3t2(_key)) { sm.last_state = SM_SELECTSOUND; TNs3t2(); } // * -> SM_SELECTMODE:
      else { sm.last_state = SM_SELECTSOUND; TNs2t1(_key); }               // A..D game legend set 1..4 shortcut -> SM_START:
        break;
    case SM_SETVOLUME: // # reading volume setting 
      if (CNs4t1(_key)) { sm.last_state = SM_SETVOLUME;  TNs4t1(_key); }   // 0..9 -> SM_START:
      else if (CNs4t4(_key)) { sm.last_state = SM_SETVOLUME; TNs4t4(); }   // # -> SM_SETVOLUME: 
      else if (CNs4t2(_key)) { sm.last_state = SM_SETVOLUME; TNs4t2(); }   // * -> SM_SELECTMODE:
      else { sm.last_state = SM_SETVOLUME; TNs2t1(_key); }                 // A..D game legend set 1..4 shortcut -> SM_START:
        break;
    }

    #ifdef debug_mode
      Serial.println(sm.state);
    #endif
  }  //else if (_key)
  else {
	  if (sound_isbusy() && (idlesm.state == IDLE_START)){
	  //if playback is active and idle timer is in IDLE_START state restart idle timer
  	    _restartidletimer();
      //else do nothing, because any idle message is being played
    }
  } // if (_key)

// =========================== EEPROM ================================

void e_InitializeVolumeMemory(){
  }

uint8_t e_readVolume(){
  uint8_t value = EEPROM.read(E_ADDR_PRIMARY_VOLUME_MEMORY);
  if ((value > MAX_VOLUME_LEVEL) || (value == 0)) {
    value = EEPROM.read(E_ADDR_SECONDARY_VOLUME_MEMORY);
    if ((value > MAX_VOLUME_LEVEL) || (value == 0)) {
      value = DEFAULT_SOUND_VOLUME;  
      }
    }
    act_volume = value;
    return value;
  }

void e_saveVolume(uint8_t volume){
  if (act_volume != volume){
    EEPROM.write(E_ADDR_PRIMARY_VOLUME_MEMORY, volume); 
    EEPROM.write(E_ADDR_SECONDARY_VOLUME_MEMORY,volume); 
    act_volume = volume;
    }
  }

uint8_t e_readLastLibrary(){
  uint8_t value = EEPROM.read(E_ADDR_LAST_LIBRARY);
  if ((value > 100) || (value == 0)) {
      value = 1;  
    }
    return value;
  }

void e_saveLastLibrary(uint8_t library){
  if (library != e_readLastLibrary()){
    EEPROM.write(E_ADDR_LAST_LIBRARY, library); 
    }
  }
    
// ============================ MAIN STATE MACHINE =========================== 

bool CNs1t1(char c) {
  return (c >= 'A') && (c <= 'D');
}

bool CNs1t2 (char c) {
  return c == '*';
}

bool CNs1t3 (char c) {
  return (c >= '0') && (c <= '9');
}

bool CNs1t4 (char c) {
  return c == '#';
}

bool CNs2t1 (char c) {
  return ((c >= 'A') && (c <= 'D')) || ((c >= '0') && (c <= '9')) || (c == '#');
}

bool CNs3t1 (char c) {
  return c == '#';
}

bool CNs3t2 (char c) {
  return c == '*';
}

bool CNs3t3 (char c) {
  return (c >= '0') && (c <= '9');
}

bool CNs4t1 (char c) {
  return (c >= '0') && (c <= '9');
}

bool CNs4t2 (char c) {
  return c == '*';
}

bool CNs4t4 (char c) {
  return c == '#';
}

void TNs1t2 () {
  sound_stop();
  sm.state = SM_SELECTMODE;
}

void TNs1t3 (char c) {
  keypadbuffer += c;
  sm.state = SM_SELECTSOUND;
}

void TNs1t4 () {
  sm.state = SM_SETVOLUME;
}

void TNs1t1(char c) {
  TNs2t1(c);
}

void TNs2t1 (char c) {
  // choose sound library
  sound_stop();
  
  switch (c) {
  case 'A': { glsb = 1; sound_libraryenter(glsb); e_saveLastLibrary(glsb); _restart (); }
    break;
  case 'B': { glsb = 2; sound_libraryenter(glsb); e_saveLastLibrary(glsb); _restart (); }
    break;
  case 'C': { glsb = 3; sound_libraryenter(glsb); e_saveLastLibrary(glsb); _restart (); }
    break;
  case 'D': { glsb = 4; sound_libraryenter(glsb); e_saveLastLibrary(glsb); _restart (); }
    break;
  case '#': { 
        if (keypadbuffer.length() > 0){
          uint8_t new_glsb = keypadbuffer.toInt(); 
          if ((new_glsb < 100) && (new_glsb > 0)) {
            glsb = new_glsb;
            sound_libraryenter(glsb);
            e_saveLastLibrary(glsb);
          }
          else {
            sound_syserror(); //err
          }
        }
        _restart ();
       }
    break;
  default: { if (keypadbuffer.length() < MAX_KEYPAD_BUFFER_LENGTH) keypadbuffer += c; }
  } 
}

void TNs2t2 () {
  _clearbuffer();
  TNs1t2 ();
}

void TNs3t1 () {
  // play sound
  #ifdef debug_mode 
    Serial.print(glsb);
    Serial.print("/");
    Serial.print(keypadbuffer.toInt());
    Serial.print(":");
  #endif

  if (keypadbuffer.length() > 0){
    sound_libraryplayitem(keypadbuffer.toInt(),glsb);
  }
  _restart ();
}

void TNs3t2 () {
  _clearbuffer();
  TNs1t2 ();
}

void TNs3t3 (char c) {
  if (keypadbuffer.length() < MAX_KEYPAD_BUFFER_LENGTH) keypadbuffer += c;
}

void TNs4t1 (char c) {
  // set volume
  if (keypadbuffer.length() < MAX_KEYPAD_BUFFER_LENGTH) keypadbuffer += c;  
  
  #ifdef debug_mode 
    Serial.print(keypadbuffer.toInt());
    Serial.print(":");
    Serial.println(map(keypadbuffer.toInt(),0,9,0,30));
  #endif

  uint8_t volume;
  volume = map(keypadbuffer.toInt(), 0, 9, 0, 30);
  mp3_set_volume(volume);
  e_saveVolume(volume);
  _restart ();
}

void TNs4t2() {
  _clearbuffer();
  TNs1t2 ();
}

void TNs4t4() {
  _clearbuffer();
  TNs1t4 ();
}

void _clearbuffer ()
{
  keypadbuffer = "";
}

void _restart ()
{
  sm.state = SM_START;
  keypadbuffer = "";
  _clearkeypadidletimer();
}

// ========================= SOUND PLAYBACK =======================

void sound_stop(){
  if(sound_isbusy()){
    mp3_stop();
    delay(10);
  }
}

bool sound_isbusy(){
  return (digitalRead(MP3_STATUS_PIN) == LOW);
}
 
void sound_syswelcome(){
  mp3_play(1);
}

void sound_syserror(){
  sound_stop();
  mp3_play(5);
}

void sound_sysinputtimeout(){
  mp3_play_intercut(1);
}

void sound_libraryenter(uint16_t lib){
  sound_stop();
  mp3_play(255, lib);
  b.after(2*1000L,onLibraryWelcome);          //2 sec delay for library enter sound 
}

void onLibraryWelcome(){
  sound_librarywelcome(glsb); 
}

void sound_librarywelcome(uint16_t lib){
  sound_stop();
  mp3_play(254, lib);
}

void sound_libraryplayitem(uint16_t i, uint16_t lib){
  sound_stop();
  mp3_play(i, lib);
}

void sound_syslowbattery(){
  sound_setsysvolumelevel();
  mp3_play_intercut(2);
}

void sound_setsysvolumelevel(){
    if (act_volume < SYS_WARNING_VOLUME_LEVEL){
      mp3_set_volume(SYS_WARNING_VOLUME_LEVEL);
      delay(10);  //wait 10ms for mp3 module to recover
      sys_volume_level_active = true;
    }
}

// ========================= BATTERY LEVEL =========================

void oncheckBatteryLevel(){
  float batteryLevel = readVin();

  Serial.print("Battery Vin: ");
  Serial.println(batteryLevel);

  #ifdef debug_mode
    if ( batteryLevel > 6.2) {
      for (int i=0; i < 20; i++){
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(50);      
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(50);      
      }
    }
  #else
    if (batteryLevel < MINIMUM_BATTERY_LEVEL) {
      sound_syslowbattery();
    }
  #endif
}

float readVin() {
  unsigned int total; // can hold max 64 readings
  float Vin; // converted to volt

  for (int x = 0; x < 64; x++) { // multiple analogue readings for averaging
    total = total + analogRead(VOLTMETER_PIN); // add each value to a total
  }
  // if (total == (1023 * 64)) { // if overflow

  Vin = (total / 64) * magic_const; // convert readings to volt
 
  return Vin; 
}

long readVcc(){
  long result; // Read 1.1V reference against AVcc 
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1); 
  delay(2); // Wait for Vref to settle 
  ADCSRA |= _BV(ADSC); // Convert 
  while (bit_is_set(ADCSRA,ADSC)); 
  result = ADCL; result |= ADCH<<8; 
  result = 1126400L / result; // Back-calculate AVcc in mV 
  return result; 
}

// ========================= TIMERS ============================

void _restartidletimer ()
{
  idlesm.state = IDLE_START;
  if (sys_volume_level_active){
    sys_volume_level_active = false;
    mp3_set_volume(act_volume);
	delay(10);  //wait 10ms for mp3 module to recover
  }
  for (int8_t i = 0; i < MAX_NUMBER_OF_EVENTS; i++){t.stop(i);};

  t.after(5*60*1000L,onIdleWhile);          // 5 min warning on idle
  t.after(10*60*1000L,onIdleLonger);        // 10 min warning on idle
  t.after(15*60*1000L,onIdleTooLong);       // 15 min warning on idle
}

void _clearkeypadidletimer()
{
	for (int8_t i = 0; i < MAX_NUMBER_OF_EVENTS; i++){ k.stop(i); };
	digitalWrite(STATUS_LED_PIN, LOW);
}

void _restartidletimers()
{
	_clearkeypadidletimer();
	k.after(10 * 1000L, onWaitTooLongForInput);  // set 10 sec input timeout
	digitalWrite(STATUS_LED_PIN, HIGH);
	_restartidletimer();
}

void onWaitTooLongForInput()
{
  sound_sysinputtimeout();
  _restart ();
}

// ===== Timer: no activity handlers ==============
void onIdleWhile()
{
  if(!sound_isbusy()){
    sound_setsysvolumelevel();
    mp3_play(2);
    idlesm.state = IDLE_WAITING;
  }
  else {
    _restartidletimer ();
  }
}

void onIdleLonger()
{
  if(!sound_isbusy()){
    mp3_play(3);
    idlesm.state = IDLE_NERVOUS;
  }
  else {
    _restartidletimer ();
  }
}

void onIdleTooLong()
{
  if(!sound_isbusy()){
    mp3_play(4);
    idlesm.state = IDLE_FORGOTTEN;
  }
  else {
    _restartidletimer ();
  }
}

#ifdef interrupt_mode
// interrupt routine
void onSoundPlayingStoped() 
{
  mp3_status_changed = true;
}
#endif