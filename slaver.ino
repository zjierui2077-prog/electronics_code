#include <Arduino.h>
#include <Wire.h>
#include <TFLI2C.h>
#include <SoftwareSerial.h> // 【新增】引入软串口库

// 【新增】定义与主板通讯的串口 (RX=8, TX=4)
SoftwareSerial MasterSerial(8, 4); 

// Motor A
int ena = 3;
int in1 = 5;
int in2 = 6;

// Motor B
int enb = 11;
int in3 = 9;
int in4 = 10;

// LiDAR
TFLI2C tfli2C;
int16_t tfDist;
int16_t tfAddr = TFL_DEF_ADR;

// 【新增】报警状态标志位
bool isAlarmed = false; 

void setup() {
  Serial.begin(115200);
  MasterSerial.begin(9600); // 【新增】初始化主从通讯串口
  Wire.begin();

  pinMode(ena, OUTPUT);
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);

  pinMode(enb, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
}

// 前进
void forward() {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
  analogWrite(ena, 180);
  analogWrite(enb, 180);
}

// 后退
void backward() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
  analogWrite(ena, 180);
  analogWrite(enb, 180);
}

// 停止
void stopMotor() {
  analogWrite(ena, 0);
  analogWrite(enb, 0);
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
}

void loop() {
  // 【新增】监听主板发来的消息
  if (MasterSerial.available() > 0) {
    char cmd = MasterSerial.read();
    if (cmd == 'A') {
      Serial.println("ALARM RECEIVED FROM MASTER! Starting motors!");
      isAlarmed = true; // 触发报警状态
    }
  }

  // 行为逻辑判断
  if (isAlarmed) {
    // 如果系统处于报警状态，执行报警动作（比如疯狂后退远离）
    backward(); 
    // 注意：在这里它会一直后退。如果你希望它只跑一段时间，可以加时间判断。
  } 
  else {
    // 如果没有报警，执行常规的雷达避障逻辑
    if (tfli2C.getData(tfDist, tfAddr)) {
      Serial.print("Distance: ");
      Serial.print(tfDist);
      Serial.println(" cm");

      if (tfDist < 40) {
        // 太近 → 后退
        forward();
      } else {
        // 安全距离可以停止或前进（根据你自己的需求）
        stopMotor(); 
      }
    } else {
      Serial.println("No data");
      stopMotor();  // 安全措施
    }
  }

  delay(100);
}
