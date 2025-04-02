#include "BluetoothSerial.h"
#include "DHT.h"

#define PWM1_Ch    0
#define PWM1_Res   8
#define PWM1_Freq  40

#define DHTPIN 26
#define DHTTYPE DHT11 
 
int PWM1_DutyCycle = 0;
int i = 0;
long re = 0;
char var;
long tmp = 30;
String strFloat;
char result[7];

int LEDROJO = 27;    // pin 9 a anodo LED rojo
int LEDVERDE = 32;    // pin 10 a anodo LED verde
int LEDAZUL = 33;   // pin 11 a anodo LED azul

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;

DHT dht(DHTPIN, DHTTYPE);
 
void setup()
{
  ledcAttachPin(LED_GPIO, PWM1_Ch);
  ledcSetup(PWM1_Ch, PWM1_Freq, PWM1_Res);
  pinMode(LEDROJO, OUTPUT); // todos los pines como salida
  pinMode(LEDVERDE, OUTPUT);
  pinMode(LEDAZUL, OUTPUT);
  digitalWrite(LEDAZUL, LOW);
  digitalWrite(LEDROJO, HIGH);
  digitalWrite(LEDVERDE, LOW);
  Serial.begin(115200);
  SerialBT.begin("ESP32test"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");
  Serial.println(F("DHT test!"));
  dht.begin();
}
 
void loop()
{
  if (SerialBT.available()) {
    var = SerialBT.read();
    Serial.println(var);
  }
  
  if(var == '6'){
    i = 6;
  }
  while (i == 6){
    
    digitalWrite(LEDAZUL, HIGH);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, LOW);
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float hic = dht.computeHeatIndex(t, h, false);
    
    strFloat = "";
    strFloat += hic;
    
    uint8_t buf[strFloat.length()];
    memcpy(buf,strFloat.c_str(),strFloat.length());
    SerialBT.write(buf,strFloat.length());
    SerialBT.println();
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
      if (var == 'a'){
        tmp = 16;
      }
      if (var == 'b'){
        tmp = 19;
      }
      if (var == 'c'){
        tmp = 22;
      }
      if (var == 'd'){
        tmp = 25;
      }
      if (var == 'e'){
        tmp = 28;
      }
      if (var == 'f'){
        tmp = 31;
      }
      if (var == 'g'){
        tmp = 34;
      }
      if (var == 'h'){
        tmp = 37;
      }
      if (var == 'i'){
        tmp = 40;
      }
      if (var == 'j'){
        tmp = 43;
      }
      if (var == 'k'){
        tmp = 46;
      }
      if (var == 'l'){
        tmp = 49;
      }
      if (var == 'm'){
        tmp = 52;
      }
      if (var == 'n'){
        tmp = 55;
      }
      if (var == 'o'){
        tmp = 58;
      }
      if (var == 'p'){
        tmp = 61;
      }
      if (var == 'q'){
        tmp = 64;
      }
      if (var == 'r'){
        tmp = 67;
      }
      if (var == 's'){
        tmp = 70;
      }
    }
    
    if (hic >= tmp) {
      ledcWrite(PWM1_Ch, 255);
    } else {
        ledcWrite(PWM1_Ch, 0);
      }
    
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }

    delay(100);
  }

  if(var == '7'){
    i = 7;
  }
  while (i == 7){
    digitalWrite(LEDAZUL, HIGH);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float hic = dht.computeHeatIndex(t, h, false);
    
    strFloat = "";
    strFloat += hic;
    
    uint8_t buf[strFloat.length()];
    memcpy(buf,strFloat.c_str(),strFloat.length());
    SerialBT.write(buf,strFloat.length());
    SerialBT.println();
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
      if (var == 'a'){
        tmp = 16;
      }
      if (var == 'b'){
        tmp = 19;
      }
      if (var == 'c'){
        tmp = 22;
      }
      if (var == 'd'){
        tmp = 25;
      }
      if (var == 'e'){
        tmp = 28;
      }
      if (var == 'f'){
        tmp = 31;
      }
      if (var == 'g'){
        tmp = 34;
      }
      if (var == 'h'){
        tmp = 37;
      }
      if (var == 'i'){
        tmp = 40;
      }
      if (var == 'j'){
        tmp = 43;
      }
      if (var == 'k'){
        tmp = 46;
      }
      if (var == 'l'){
        tmp = 49;
      }
      if (var == 'm'){
        tmp = 52;
      }
      if (var == 'n'){
        tmp = 55;
      }
      if (var == 'o'){
        tmp = 58;
      }
      if (var == 'p'){
        tmp = 61;
      }
      if (var == 'q'){
        tmp = 64;
      }
      if (var == 'r'){
        tmp = 67;
      }
      if (var == 's'){
        tmp = 70;
      }
    }
    
    if (hic <= tmp) {
      re = 255 - (((tmp - hic) * 204) / tmp);
      Serial.println(re);
      ledcWrite(PWM1_Ch, re);
    } else {
        ledcWrite(PWM1_Ch, 255);
      }
    
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }

    delay(100);
  }
    
  if(var == '0'){
    i = 0;
  }
  while (i == 0){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, HIGH);
    digitalWrite(LEDVERDE, LOW);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 0);
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
  if(var == '1'){
    i = 1;
  }
  while (i == 1){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 51);
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
  if(var == '2'){
    i = 2;
  }
  while (i == 2){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 102);
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
  if(var == '3'){
    i = 3;
  }
  while (i == 3){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 153);
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
  if(var == '4'){
    i = 4;
    }
  while (i == 4){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 204);
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '5'){
      ledcWrite(PWM1_Ch, 0);
      i = 5;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
  if(var == '5'){
    i = 5;
    } 
  while (i == 5){
    digitalWrite(LEDAZUL, LOW);
    digitalWrite(LEDROJO, LOW);
    digitalWrite(LEDVERDE, HIGH);
    
    if (SerialBT.available()) {
      var = SerialBT.read();
      Serial.println(var);
    }
    ledcWrite(PWM1_Ch, 255);
    if (var == '0'){
      ledcWrite(PWM1_Ch, 0);
      i = 0;
    }
    if (var == '2'){
      ledcWrite(PWM1_Ch, 0);
      i = 2;
    }
    if (var == '3'){
      ledcWrite(PWM1_Ch, 0);
      i = 3;
    }
    if (var == '4'){
      ledcWrite(PWM1_Ch, 0);
      i = 4;
    }
    if (var == '1'){
      ledcWrite(PWM1_Ch, 0);
      i = 1;
    }
    if (var == '6'){
      ledcWrite(PWM1_Ch, 0);
      i = 6;
    }
    if (var == '7'){
      ledcWrite(PWM1_Ch, 0);
      i = 7;
    }
  }
}
