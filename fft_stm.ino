/**
 * Author:    Mertcan Özdemir
 * Created:   10.04.2020
 * 
 * STM32F103C Guitar tuner with fake multitasking(Async sampling)
 * 
 * (c) Copyright by Mertcan Özdemir
 **/

#include <SpeedTrig.h> 
#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif


#define CLK PB12
#define DIN PB13
#define DC PB14
#define CE PB15
#define RST PA8
#define MIC_IN PA1
#define BUTTON_PIN PA2
#define MOTOR_A PA6
#define MOTOR_B PA7
#define SAMPLE_SIZE 1024 // Samples per second
#define SAMPLING_SIZE 256 // Do fft every 256 samples
#define FREQ_WINDOW 15 // Scan notes in +-15 hz
#define FREQ_WINDOW_GROW 3
#define FREQ_SLOWDOWN_FACTOR 5

class Complex{   //Complex number class
  public: 
    float real;
    float img;
    Complex(){
      real = 0;
      img  = 0;
      }
    void fromAngle(float aci){  // Create complex number from angle
     
      this->real = SpeedTrig.cos(aci/PI*180);
      this->img  = SpeedTrig.sin(aci/PI*180);
      }  
    Complex(float initialR, float initialI){ // Create complex number from a+bi
      this->real = initialR;
      this->img  = initialI;
      } 
    void times(Complex c){   // Multiply by c
      float yedekReal = this->real;
      
      this->real = this->real * c.real - this->img * c.img;
      this->img  = yedekReal * c.img  + this->img * c.real;
      }
    void equal(Complex c){  // Equal to complex number
      this->real = c.real;
      this->img  = c.img;
      }
    void plus(Complex c){ // Sum with complex number
      this->real = this->real + c.real;
      this->img  = this->img  + c.img;
      } 
     void set(float initialR, float initialI){ // Set real and imaginal parts of complex number
      this->real = initialR;
      this->img  = initialI;
      }
     void minus(Complex c){ // Subtract with complex number
      this->real = this->real - c.real;
      this->img  = this->img  - c.img;
      } 
  
  };

byte adaptiveSampleRate = 2; // Changing sampling frequency based on note, first two notes sampling frequency is 512 hz
//For other notes sampling frequency is 1024 hz
long buttonHoldCounter;
float frekans = 0; // Frequency variable
boolean isScreenChanged = true; // Did screen change
long screenSayac = 0; // Counter for screen events
byte screenMode = 0; 
U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ CLK, /* data=*/ DIN, /* cs=*/ CE, /* dc=*/ DC, /* reset=*/ RST);  // Nokia 5110 Display
long sayac = 0; // Counter for various events
float notaFrekans [] = {82.5,110,147,196,247,330}; // Guitar note frequencies
int seciliNota = 0; // Selected note to tune 0:E2, 1:A2, 2:D3, 3:G3, 4:B3, 5:E4
Complex sinyal[SAMPLE_SIZE]; // Output array for fft
int sample[SAMPLE_SIZE]; // Input array for fft
int sampling[SAMPLING_SIZE]; // Async sampled data
int sampling_counter = 0; // Async sample counter
Complex yedek; // Variable for various duties
Complex omega; // Variable for w
int bitSayisi; // Bit length of samples
boolean FFTavailable = false; // Did async sampling finish
boolean readyToOperate = false; // Ready to turn the motor
boolean isSet = false; // Note frequency set
long freqCounter = 0; 

extern const uint8_t welcome_animation[11][528] PROGMEM; // Animation array


 


void setup() {
//Serial.begin(500000);
pinMode(MIC_IN,INPUT);  // Mic in with 1.66v offset
pinMode(MOTOR_A,OUTPUT); 
pinMode(MOTOR_B,OUTPUT);
 u8g2.begin();
pinMode(BUTTON_PIN,INPUT_PULLUP);



bitSayisi = kacBit(SAMPLE_SIZE); // Find out bit length of samples
yedek = Complex();
omega = Complex();
  initTimerInterrupt();  // Start async sampling
  for(int i = 0; i < 11; i++){
      u8g2.clearBuffer();
      u8g2.drawBitmap(0,0,11,48,welcome_animation[i]);
      u8g2.sendBuffer();
      delay(50);
      if(i == 0) delay(1000);
    }

}




void initTimerInterrupt(){   // Creating the interrupt
  Timer2.pause();
  Timer2.setPeriod(1952);
  Timer2.setChannel1Mode(TIMER_OUTPUT_COMPARE);
  Timer2.attachInterrupt(1,TIMER_HANDLER);
  Timer2.refresh();
  Timer2.resume();
  }


void changeSampleRate(int multiplier){           // Change between 1024 and 512 hz sampling frequency
  if(multiplier != adaptiveSampleRate){
    Timer2.pause();
    Timer2.setPeriod((multiplier == 1)? 976 : 1952);
    for(int i = 0; i < SAMPLE_SIZE; i++){        // If sampling frequency changed clear all of the previous samples
      sample[i] = 0;
      }
        Timer2.refresh();
        Timer2.resume();
    }
  adaptiveSampleRate = multiplier;
  
  }
 
void loop() {
if(FFTavailable){  // Did interrupt take enough sample
  FFTavailable = false;
   long a = millis();
   FFT(); // Do the fft
  // Serial.print("FFT done, elapsed time:");
  // Serial.println(millis()-a);
  }
  if(isScreenChanged){ // If screen changed, draw again
  drawScreen();
  isScreenChanged = false;
  }
  if(screenMode == 1 && millis() - screenSayac > 1000){ // If note changed show the new note for 1000ms 
    screenMode = 0;
    isScreenChanged = true;
    }

if(frekans == notaFrekans[seciliNota] && readyToOperate){  // If frequency is stable enough(for 500 ms) note is set
  if(millis() - freqCounter > 500)
  {
    isSet = true;
  isScreenChanged = true;
  }
  }else{
    freqCounter = millis();
    }


if(frekans!=0 && readyToOperate && !isSet){           //Turn the motor according to frequency
  if(frekans > notaFrekans[seciliNota]){
    analogWrite(MOTOR_A,getSpeedValue(frekans - notaFrekans[seciliNota]));
    analogWrite(MOTOR_B,0);
    }else if(frekans < notaFrekans[seciliNota]){
      analogWrite(MOTOR_A,0);
    analogWrite(MOTOR_B,getSpeedValue(notaFrekans[seciliNota] - frekans));
      }else{
       analogWrite(MOTOR_A,0);
    analogWrite(MOTOR_B,0);
        }
  
  }else{
      analogWrite(MOTOR_A,0);
    analogWrite(MOTOR_B,0);
    }

    
    if(digitalRead(BUTTON_PIN) == 0){        //Detect button press and hold
      if(millis()- buttonHoldCounter > 1000 && millis() - buttonHoldCounter < 2000){
        changeNota();                     // If button holded for 1 seconds change the selected note
        buttonHoldCounter -= 1000;
        }
    }else{
      buttonHoldCounter = millis();
      }
  
    
}


int getSpeedValue(int fark){   // Change speed of motor according to note and how close the frequency is
  if(fark < notaFrekans[seciliNota]*0.03){ if(seciliNota < 3){return 120 - FREQ_SLOWDOWN_FACTOR * (seciliNota+2);}else{
    return 120 - FREQ_SLOWDOWN_FACTOR * (seciliNota+4);
    }};
  return 145;
  }


int bitReverse(int a){  // Reversing the bit order for fft
  int reversed = 0;
  for(int i = 0; i < bitSayisi-1; i++){

      reversed |= a & 0x01;
      reversed = reversed << 1;
      a = a >> 1;
    }
  reversed |= a & 0x01;  
  return reversed;  
  }



void drawScreen(){    // Screen drawing
  u8g2.clearBuffer();// Clear Screen

  
  if(screenMode == 0){ // If screenmode is 0 show the frequency
       if(isSet){
  
      u8g2.setFont(u8g2_font_fub30_tr);
      u8g2.setCursor(12,46);
      u8g2.print("OK");
       u8g2.setFont(u8g2_font_7x14B_tf);
      u8g2.setCursor(36,15);  
      notayiYazdir();
      }else{
  for(int i = 0; i < 180; i++){
    u8g2.drawPixel(42 - SpeedTrig.cos(i) * 36, 48 - SpeedTrig.sin(i) * 36);
    }
   u8g2.drawPixel(42,46);
   u8g2.drawPixel(41,47);
   u8g2.drawPixel(42,47);
   u8g2.drawPixel(43,47);
   u8g2.setFont(u8g2_font_7x14B_tf);
   u8g2.setCursor(36,34);
   notayiYazdir();
   u8g2.drawFrame(34,22,17,14);
   
  if(frekans != 0){  

  u8g2.setFont(u8g2_font_5x7_mf);
  u8g2.setCursor(28,44);
  if(seciliNota < 2){
  u8g2.print(frekans,1);}else{
    u8g2.print((int)frekans);
    }
  u8g2.print(" Hz");
  int aci = (notaFrekans[seciliNota] - frekans) * 103 / (FREQ_WINDOW + seciliNota * FREQ_WINDOW_GROW);
  u8g2.drawLine(42, 48  , 42 - SpeedTrig.cos(90-aci) * 32, 48 - SpeedTrig.sin(90-aci) * 32);
  }
    
    if(!readyToOperate){
    u8g2.setCursor(0,5);
    u8g2.setFont(u8g2_font_u8glib_4_tf);
    u8g2.print(F("Baslamak icin tusa basiniz."));}
 
  }
  }
  else{                // Print the note to screen when changed
    u8g2.setCursor(12,39);
    u8g2.setFont(u8g2_font_fub30_tr);
    notayiYazdir();
    }
  u8g2.sendBuffer();

  
  }

void notayiYazdir(){
   switch(seciliNota){
      case 0:
        u8g2.print(F("E2"));
        break;
      case 1:
        u8g2.print(F("A2"));
        break;
      case 2:
        u8g2.print(F("D3"));
        break;
      case 3:
        u8g2.print(F("G3"));
        break;
      case 4:
        u8g2.print(F("B3"));
        break;
      case 5:
        u8g2.print(F("E4"));
        break;            
      }
  }

void changeNota(){  // Change the note and if required change the sampling freqency
  
  if(readyToOperate){
      screenMode = 1;
      screenSayac = millis();
      seciliNota++;
      if(seciliNota == 6) seciliNota = 0;
      changeSampleRate((seciliNota < 2) ? 2 : 1);
      isScreenChanged = true;
      readyToOperate = false;
    }else{
      readyToOperate = true;
      isScreenChanged = true;
      }
    isSet = false;
    analogWrite(MOTOR_A,0);
    analogWrite(MOTOR_B,0);

    
  }  
void fft(){
  for(int i = 0; i < SAMPLE_SIZE; i++){  // Copy the samples to the fft array while reversing bit order
    sinyal[i].set(sample[bitReverse(i)],0);
    }
   for(int i = SAMPLE_SIZE/2; i > 0; i=i/2){
  for(int j = 0; j <  SAMPLE_SIZE / i / 2; j++){
    for(int z = 0; z < i; z++){
      yedek.equal(sinyal[z + 2 * j * i]);
      sinyal[z + 2 * j * i].plus(sinyal[z + 2 * j * i + i]);
      sinyal[z + 2 * j * i + i].minus(yedek);
      omega.fromAngle(-2 * PI * (SAMPLE_SIZE / i / 2 * z) / SAMPLE_SIZE);
      sinyal[z + 2 * j * i + i].times(omega);
    }
  }
}
}




void FFT(){  // do fft and find the frequency with highest amplitude
  fft();
  float enyuksek = 25;
  int enyuksekid = 0; 
  for(int i = notaFrekans[seciliNota]*adaptiveSampleRate - (FREQ_WINDOW + seciliNota * FREQ_WINDOW_GROW)*adaptiveSampleRate; i < notaFrekans[seciliNota]*adaptiveSampleRate + (FREQ_WINDOW + seciliNota * FREQ_WINDOW_GROW)*adaptiveSampleRate; i++){ // Only scan within +-15hz
    float b = sqrt(sinyal[i].real * sinyal[i].real + sinyal[i].img * sinyal[i].img) / SAMPLE_SIZE * 2; 
    if(b > enyuksek){
      enyuksekid = i;
      enyuksek = b;
      }
 
    }
    if(frekans != enyuksekid){
      isScreenChanged = true;
      }
  frekans = (float)enyuksekid / (float)adaptiveSampleRate;
  //Serial.print("enyuksek:");
  //Serial.println(enyuksekid);
  
  }




void TIMER_HANDLER(void){ // Interrupt that takes the samples

  int okunan = analogRead(MIC_IN); // Take sample
  sampling[sampling_counter] = okunan - 2048; // Subtract the offset
  sampling_counter++;                         
  if(sampling_counter == SAMPLING_SIZE){      // If we took enough samples then pass the samples to the input array for fft
    sampling_counter = 0;
    for(int i = 0; i < SAMPLE_SIZE; i++){
      if(i < SAMPLE_SIZE - SAMPLING_SIZE){
        sample[i] = sample[i+SAMPLING_SIZE];
        }else{
          sample[i] = sampling[i+SAMPLING_SIZE-SAMPLE_SIZE];
          }
      }
      FFTavailable = true;                // Ready for fft
      //Serial.println((millis()-sayac));

    
    }

  }



  
int kacBit(int a){             // Find the bit length of the samplesize for bit reversing
  int b = 0;
  while(a != 1){
    a = a/2;
    b++;
    }
  return b;
  
  }  
 
