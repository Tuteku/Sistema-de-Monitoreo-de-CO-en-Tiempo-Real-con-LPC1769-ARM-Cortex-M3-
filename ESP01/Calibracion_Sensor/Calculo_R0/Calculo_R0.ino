
int RL = 47;
float sensorValue = 0.0;
float Ro;


void setup() {
  Serial.begin(9600);
}

void loop() {
  float sensor_volt;

  for(int x = 0; x<100; x++){
    sensorValue = sensorValue + analogRead(A0);
  }
  sensorValue = sensorValue/100;

  sensor_volt = sensorValue / 1024 * 5.0;
  Ro = ((5.0 * RL)/sensor_volt) - RL;
  
  Serial.print("R0 = ");
  Serial.println(Ro);
  delay(1000);
}
