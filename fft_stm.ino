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
#define SAMPLE_SIZE 1024 // Saniyede alınan örnek sayısı(sabit değil)
#define SAMPLING_SIZE 256 // Her 256 örnekte bir fft gerçekleştir
#define FREQ_WINDOW 15 // Notaları +-15 Hz aralığında ara
#define FREQ_WINDOW_GROW 3
#define FREQ_SLOWDOWN_FACTOR 5

class Complex{   // Complex sayı class'ı 
  public: 
    float real;
    float img;
    Complex(){
      real = 0;
      img  = 0;
      }
    void fromAngle(float aci){  // açıdan kompleks sayı oluştur
     
      this->real = SpeedTrig.cos(aci/PI*180);
      this->img  = SpeedTrig.sin(aci/PI*180);
      }  
    Complex(float initialR, float initialI){ // a+bi'den kompleks sayı oluştur
      this->real = initialR;
      this->img  = initialI;
      } 
    void times(Complex c){   // c kompleks sayısı ile çarp
      float yedekReal = this->real;
      
      this->real = this->real * c.real - this->img * c.img;
      this->img  = yedekReal * c.img  + this->img * c.real;
      }
    void equal(Complex c){  // c kompleks sayısına eşitle
      this->real = c.real;
      this->img  = c.img;
      }
    void plus(Complex c){ // c kompleks sayısı ile topla
      this->real = this->real + c.real;
      this->img  = this->img  + c.img;
      } 
     void set(float initialR, float initialI){ // a+bi'ye eşitle
      this->real = initialR;
      this->img  = initialI;
      }
     void minus(Complex c){ // c kompleks sayısını çıkar
      this->real = this->real - c.real;
      this->img  = this->img  - c.img;
      } 
  
  };

byte adaptiveSampleRate = 2;
long buttonHoldCounter;
float frekans = 0; // Bulunan frekans
boolean isScreenChanged = true; // Ekranda değişiklik var mı
long screenSayac = 0; // Nota adı göstermek için sayaç 
byte screenMode = 0; // Nota adı ve frekans grafiği arasında mod seçimi
U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ CLK, /* data=*/ DIN, /* cs=*/ CE, /* dc=*/ DC, /* reset=*/ RST);  // Nokia 5110 Display
long sayac = 0; // çeşitli görevler için sayaç
float notaFrekans [] = {82.5,110,147,196,247,330}; //sırayla notaların frekansları
int seciliNota = 0; // şu an seçili olan nota
Complex sinyal[SAMPLE_SIZE]; //fft çıkışı için kompleks sayı dizisi 
int sample[SAMPLE_SIZE]; //fft girişi için dizi
int sampling[SAMPLING_SIZE]; //Interrupt'ın sinyali yazacağı dizi
int sampling_counter = 0; //En son kaçıncı örnek yazıldı
Complex yedek; //Çeşitli görevler için yedek kompleks sayı
Complex omega; //fft'de kullanılan omega
int bitSayisi; //Giriş sinyalini kaç bit ile gösterebiliriz
boolean FFTavailable = false; // Yeterli örnek sayısına ulaşıldı mı
boolean readyToOperate = false;
boolean isSet = false;
long freqCounter = 0;

extern const uint8_t welcome_animation[11][528] PROGMEM;


 


void setup() {
//Serial.begin(500000);
pinMode(MIC_IN,INPUT); 
pinMode(MOTOR_A,OUTPUT);
pinMode(MOTOR_B,OUTPUT);
 u8g2.begin();
pinMode(BUTTON_PIN,INPUT_PULLUP);



bitSayisi = kacBit(SAMPLE_SIZE); //fft girişi kaç bit, bit ters çevirme işleminde işe yarayacak
yedek = Complex();
omega = Complex();
  initTimerInterrupt();
  for(int i = 0; i < 11; i++){
      u8g2.clearBuffer();
      u8g2.drawBitmap(0,0,11,48,welcome_animation[i]);
      u8g2.sendBuffer();
      delay(50);
      if(i == 0) delay(1000);
    }

}




void initTimerInterrupt(){   //976 mikrosaniyede bir tetiklenen interruptı hazırla
  Timer2.pause();
  Timer2.setPeriod(1952);
  Timer2.setChannel1Mode(TIMER_OUTPUT_COMPARE);
  Timer2.attachInterrupt(1,TIMER_HANDLER);
  Timer2.refresh();
  Timer2.resume();
  }


void changeSampleRate(int multiplier){
  if(multiplier != adaptiveSampleRate){
    Timer2.pause();
    Timer2.setPeriod((multiplier == 1)? 976 : 1952);
    for(int i = 0; i < SAMPLE_SIZE; i++){
      sample[i] = 0;
      }
        Timer2.refresh();
        Timer2.resume();
    }
  adaptiveSampleRate = multiplier;
  
  }
 
void loop() {
if(FFTavailable){  //Yeterli örnek sayısına ulaşıldı ise fft'yi gerçekleştir
  FFTavailable = false;
   long a = millis();
   FFT(); // FFT fonksiyonu
  // Serial.print("FFT done, elapsed time:");
  // Serial.println(millis()-a);
  }
  if(isScreenChanged){ //Ekranda değişiklik varsa ekranı yeniden çizdir
  drawScreen();
  isScreenChanged = false;
  }
  if(screenMode == 1 && millis() - screenSayac > 1000){ // Seçilen notayı ekranda 1000 ms gösterdikten sonra ekranı eski haline getir
    screenMode = 0;
    isScreenChanged = true;
    }

if(frekans == notaFrekans[seciliNota] && readyToOperate){
  if(millis() - freqCounter > 500)
  {
    isSet = true;
  isScreenChanged = true;
  }
  }else{
    freqCounter = millis();
    }


if(frekans!=0 && readyToOperate && !isSet){
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
    if(digitalRead(BUTTON_PIN) == 0){
      if(millis()- buttonHoldCounter > 1000 && millis() - buttonHoldCounter < 2000){
        changeNota();
        buttonHoldCounter -= 1000;
        }
    }else{
      buttonHoldCounter = millis();
      }
  
    
}


int getSpeedValue(int fark){  
  if(fark < notaFrekans[seciliNota]*0.03){ if(seciliNota < 3){return 120 - FREQ_SLOWDOWN_FACTOR * (seciliNota+2);}else{
    return 120 - FREQ_SLOWDOWN_FACTOR * (seciliNota+4);
    }};
  return 145;
  }

int bitReverse(int a){  // Verilen sayının bitlerini ters çevir
  int reversed = 0;
  for(int i = 0; i < bitSayisi-1; i++){

      reversed |= a & 0x01;
      reversed = reversed << 1;
      a = a >> 1;
    }
  reversed |= a & 0x01;  
  return reversed;  
  }


void drawScreen(){ 
  u8g2.clearBuffer();// Ekranı temizle

  
  if(screenMode == 0){ // Eğer ekran frekans gösterme modunda ise göstergeyi çizdir
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
  else{                // Eğer ekran nota gösterme modunda ise büyük harflerle notayı ekrana yazdır
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

void changeNota(){  // Butona basıldığında aktif olan interrupt. Seçili notayı değiştiriyor

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
  for(int i = 0; i < SAMPLE_SIZE; i++){  // Örnekleri kompleks diziye aktar
    sinyal[i].set(sample[bitReverse(i)],0);
    }
  int STEP = 2;                         // İkili gruplardan başla
  while(STEP != SAMPLE_SIZE*2){
    for(int m = 0; m < SAMPLE_SIZE/STEP; m++){
        for(int n = 0; n < STEP/2; n++){
          int ilkEleman = m*STEP+n;    

          yedek.equal(sinyal[ilkEleman]); // Grubun ilk örneğini daha sonra kullanacağımız için yedekliyoruz

          sinyal[ilkEleman].plus(sinyal[ilkEleman+STEP/2]);  // Grubun birinci örneğine ikinciyi ekle
          sinyal[ilkEleman+STEP/2].minus(yedek);             // Grubun ikinci örneğinden birinciyi çıkar
          if(m % 2 != 0){                                    // Grup numarası tek ise omega ile çarp
                      
              float omegaAci = -2*(float) n * (SAMPLE_SIZE/2/STEP)*PI/((float)SAMPLE_SIZE);    // w = e^(-2*pi*k/N)
        
              omega.fromAngle(omegaAci);
              sinyal[ilkEleman].times(omega);
              omegaAci = -2*(float)(n+STEP/2) * (SAMPLE_SIZE/2/STEP)*PI/((float)SAMPLE_SIZE); // w = e^(-2*pi*k/N)
              omega.fromAngle(omegaAci);
              sinyal[ilkEleman+STEP/2].times(omega);
            }
          
          }
      
      }
    
    STEP = STEP*2;                                           // Sıradaki aşamaya geç
    }

    
  }




void FFT(){ // fft alındıktan sonra belirlenen aralıktaki en yüksek genlikli frekansı bul
  fft();
  float enyuksek = 25;
  int enyuksekid = 0;
  for(int i = notaFrekans[seciliNota]*adaptiveSampleRate - (FREQ_WINDOW + seciliNota * FREQ_WINDOW_GROW)*adaptiveSampleRate; i < notaFrekans[seciliNota]*adaptiveSampleRate + (FREQ_WINDOW + seciliNota * FREQ_WINDOW_GROW)*adaptiveSampleRate; i++){ 
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




void TIMER_HANDLER(void){ //976 mikrosaniyede bir ateşlenen interrupt. Her interrupt'da bir örnek

  int okunan = analogRead(MIC_IN); // Örneği al
  sampling[sampling_counter] = okunan - 2048; // Örnek Vcc/2 DC bileşene sahip olduğu için çıkar
  sampling_counter++;                         // Alınan örnek sayısını arttır
  if(sampling_counter == SAMPLING_SIZE){      // Yeterince örnek alındıysa diziyi büyük dizinin sonuna aktar
    sampling_counter = 0;
    for(int i = 0; i < SAMPLE_SIZE; i++){
      if(i < SAMPLE_SIZE - SAMPLING_SIZE){
        sample[i] = sample[i+SAMPLING_SIZE];
        }else{
          sample[i] = sampling[i+SAMPLING_SIZE-SAMPLE_SIZE];
          }
      }
      FFTavailable = true;                // Yeterince örnek alındı
      //Serial.println((millis()-sayac));

    
    }

  }



  
int kacBit(int a){             // Bit ters çevirme işlemi için örnek sayısının kaç bitle temsil edilebileceğini bul
  int b = 0;
  while(a != 1){
    a = a/2;
    b++;
    }
  return b;
  
  }  
 
