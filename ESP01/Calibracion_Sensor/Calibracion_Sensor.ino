int RL = 10;
float sensorValue = 0.0;
void setup() {
  Serial.begin(9600);
}

void loop() {
  float sensor_volt;
  float RS_air;
  float RO;
  

  for(int x = 0; x<100; x++){
    sensorValue = sensorValue + analogRead(A0);
  }
  sensorValue = sensorValue/100;

  sensor_volt = sensorValue/1024*5.0;
  RS_air = RL * (5.0-sensor_volt)/sensor_volt;
  RO = RS_air/26.5;

  Serial.print("sensor_volt = ");
  Serial.print(sensor_volt);
  Serial.println("V");

  Serial.print("RO = ");
  Serial.println(RO);
  delay(1000);
}
