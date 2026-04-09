#include <Arduino.h>
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <Wire.h>
#include <DFRobot_ID809.h>
#include <Modulino.h>
#include <SoftwareSerial.h>
#include <Motoron.h>
SoftwareSerial CommSerial(8, 4); // RX=8, TX=4
// declare all th variables that we use
int count = 0;
MotoronI2C mc(16);
// buzzer
int buzzerPin = 2;
bool status = true;
enum WeightStatus
{
  STATUS_NORMAL = 0,
  STATUS_ITEM_NO_LONGER_SAFE = 1,
  STATUS_SENSOR_ERROR = 2
};
int alram_command = 0;
bool alarmOn = false;
bool doorAlarmOn = false;
// scale
NAU7802 scale;
float baseWeight = 0;
WeightStatus state;
// finger print
DFRobot_ID809 fingerprint;
int finger_command = 0;
// door
int door_time = 0;
bool door_state = 1;
int dt = 1;
//button
ModulinoButtons myButtons;


// function that delcare
void alarm();
void doorAlarm();
WeightStatus checkWeightChange(NAU7802 &scale, float baseWeight);
float calibrateAndSetBaseWeight(NAU7802 &scale);
void enrollFingerprint();
void matchFingerprint();
void deleteAllFingerprints();
void unlock_servo();
void lock_servo();
void buzzer();

// setup
void setup()
{
  Wire.begin(); 
  mc.reinitialize();
  mc.disableCrc();
  mc.clearResetFlag();
  mc.clearMotorFault();
  CommSerial.begin(9600); // 启动与从板的通讯
  Serial.begin(115200);
  Serial1.begin(115200);
  fingerprint.begin(Serial1);
  Modulino.begin();
  myButtons.begin();
  // 等待串口准备就绪 (最多等3秒)
  unsigned long startTime = millis();
  while (!Serial && millis() - startTime < 3000)
    ; 

  Serial.println("Starting System...");

  // 初始化称重传感器
  Wire1.begin();
  delay(500);

  if (!scale.begin(Wire1))
  {
    Serial.println("Scale not detected. Please check wiring.");
    while (1)
      ; // 如果找不到传感器，程序卡死在这里
  }
  Serial.println("Scale detected!");

     // ==========================================
  // 系统引导菜单 (使用 Modulino 物理按键)
  // ==========================================
  Serial.println("\n=== SYSTEM SETUP MENU ===");
  Serial.println("[Button A] -> Enroll a new fingerprint (录入新指纹)");
  Serial.println("[Button B] -> Delete ALL fingerprints (删除所有指纹)");
  Serial.println("[Button C] -> Continue to Calibration (进入下一步)");

  bool setupComplete = false;
  while (!setupComplete)
  {
    // 【核心】必须在 while 循环内不断刷新按键状态
    myButtons.update(); 
    
    if (myButtons.isPressed(0)) // 按下按钮 A
    {
      Serial.println("\n--- Starting Fingerprint Enrollment ---");
      enrollFingerprint();
      Serial.println("\nAction complete. Press A, B, or C.");
      delay(500); // 防抖
    }
    else if (myButtons.isPressed(1)) // 按下按钮 B
    {
      Serial.println("\n--- Deleting Fingerprints ---");
      deleteAllFingerprints();
      Serial.println("\nAction complete. Press A, B, or C.");
      delay(500); // 防抖
    }
    else if (myButtons.isPressed(2)) // 按下按钮 C (确认/继续)
    {
      Serial.println("\n--- Proceeding to Scale Calibration ---");
      setupComplete = true; 
      delay(500); // 防抖
    }
  }



  // 开始称重传感器的校准逻辑（复用你原有的函数）
  baseWeight = calibrateAndSetBaseWeight(scale);
  
  Serial.println("\n=== SYSTEM ACTIVE & SECURED ===");
}
void loop()
{
  // 1. 门处于开启状态的逻辑
  if (door_state == 0) 
  {
    door_time += dt;
    if(door_time > 30) // 如果开门时间过长
    {
      doorAlarm();
      doorAlarmOn = true;
      lock_servo();
    }
  } 
  // 2. 门处于锁定状态且未报警的逻辑
  else if (alarmOn == false) 
  {
    // 这里现在是非阻塞的了！没有手指时瞬间通过
    matchFingerprint(); 
    
    // 处理指纹比对结果
    if (finger_command == 1) // 指纹正确
    {
      unlock_servo();
      door_time = 0;
      count = 0; // 错误计数清零
      finger_command = 0; 
      
      // 【新增】指纹正确，发送 'S' 给从板，解除它的报警状态
      CommSerial.write('S'); 
      Serial.println("System Safe. Sent 'S' to Slave.");
    }
    else if (finger_command == 2) // 指纹错误
    {
      count++;
      finger_command = 0;
      Serial.print("Wrong fingerprint! Attempts: ");
      Serial.println(count);
      
      if (count >= 3)
      {
        alarmOn = true;
        
        // 【新增】指纹错误3次，发送 'A' 给从板触发马达
        CommSerial.write('A'); 
        Serial.println("ALARM! Sent 'A' to Slave.");
        
        alarm(); 
      }
    }
    
    // 3. 实时检测重量！现在只要物品被拿走，最多延迟0.5秒就会报警
    state = checkWeightChange(scale, baseWeight);
    if (state == STATUS_ITEM_NO_LONGER_SAFE)
    {
      Serial.println("Weight changed! THIEF!");
      
      // 【新增】东西被拿走，也发送 'A' 给从板触发马达
      CommSerial.write('A'); 
      Serial.println("ALARM! Sent 'A' to Slave.");
      
      alarm();
      alarmOn = true;
    }
    else {
      Serial.println("safe!");
    }
  }
  
  // 循环底部的延迟。加上这个0.5秒延迟，你的重量和指纹检测频率是 2次/秒。
  // 这对于安防防盗来说足够灵敏了，也不会让串口打印卡死。
  delay(500); 
}
WeightStatus checkWeightChange(NAU7802 &scale, float baseWeight)
{
  const float threshold = 5.0;

  if (!scale.available())
  {
    return STATUS_SENSOR_ERROR;
  }

  float currentWeight = scale.getWeight();
  float weightDifference = abs(currentWeight - baseWeight);

  if (weightDifference > threshold)
  {
    return STATUS_ITEM_NO_LONGER_SAFE;
  }

  return STATUS_NORMAL;
}

float calibrateAndSetBaseWeight(NAU7802 &scale)
{
  Serial.println("\n=== Calibrating ===");
  Serial.println("1. Make sure the plate is empty");
  scale.calculateZeroOffset();

  Serial.println("2. Place calibration weight on the plate (放置校准物)");
  Serial.println("3. Type the known weight in Serial monitor, then [PRESS BUTTON C] to confirm.");

  String inputBuffer = "";
  float knownWeight = 0.0;
  bool isConfirmed = false;

  // 第一段：等待输入数字并按下按钮 C 确认
  while (!isConfirmed)
  {
    // 不断刷新按键状态
    myButtons.update();

    // 接收串口输入的数字
    if (Serial.available() > 0)
    {
      char incomingChar = Serial.read();
      if (isDigit(incomingChar) || incomingChar == '.')
      {
        inputBuffer += incomingChar;
        Serial.print(incomingChar); // 回显你输入的数字
      }
    }

    // 检测是否按下按钮 C 作为“确认键”
    if (myButtons.isPressed(2)) 
    {
      if (inputBuffer.length() > 0)
      {
        knownWeight = inputBuffer.toFloat();
        isConfirmed = true;
        Serial.println(); // 换行
      }
      else
      {
        Serial.println("\nPlease type a number in Serial monitor FIRST!");
        delay(500); // 防抖并给用户看提示的时间
      }
    }
  }

  Serial.print("Confirmed! Calibration weight: ");
  Serial.println(knownWeight);

  // 执行校准
  scale.calculateCalibrationFactor(knownWeight);

  // ==========================================
  // 第二段：放置真实物品并按下按钮 C 确认基准
  // ==========================================
  Serial.println("\n=== Set Base Weight ===");
  Serial.println("4. REMOVE the calibration weight. (拿走校准物)");
  Serial.println("5. Place the ACTUAL ITEM on the plate. (放上你要保护的物品)");
  Serial.println("6. [PRESS BUTTON C] when ready to set the base weight. (准备好后按按钮 C 确认)");

  bool itemPlaced = false;
  
  // 等待按下按钮 C
  while (!itemPlaced)
  {
    myButtons.update(); // 刷新按键
    
    if (myButtons.isPressed(2)) 
    {
      itemPlaced = true; 
      delay(500); // 防抖
    }
  }

  // 延时一下等托盘稳定
  delay(1000); 
  
  // 读取真实重量
  float measuredBase = scale.getWeight();
  Serial.print("Success! Base weight set to: ");
  Serial.println(measuredBase);

  return measuredBase; 
}

void doorAlarm()
{
  buzzer();
  delay(200);
  printf("the lock is no long safe1");
}
void alarm()
{
  for (int i = 0; i < 20; i += 20)
  {
    buzzer();
    delay(20);
  }
  printf("the lock is no long safe2");
}

void enrollFingerprint()

{
  uint8_t id = fingerprint.getEmptyID();

  if (id == ERR_ID809)
  {
    Serial.print("Failed to get empty ID: ");
    Serial.println(fingerprint.getErrorDescription());
    return;
  }

  Serial.print("Empty ID = ");
  Serial.println(id);
  Serial.println("Use the SAME finger 3 times.");

  for (int i = 1; i <= 3; i++)
  {
    Serial.print("Step ");
    Serial.print(i);
    Serial.println(": press finger.");

    while (true)
    {
      uint8_t ret = fingerprint.collectionFingerprint(10);
      if (ret == 0)
      {
        Serial.println("Sample OK");
        break;
      }
      else if (ret == ERR_ID809)
      {
        Serial.print("Sample failed: ");
        Serial.println(fingerprint.getErrorDescription());
      }
      delay(200);
    }

    Serial.println("Now remove finger...");
    while (fingerprint.detectFinger())
    {
      delay(20);
    }
    delay(300);
  }

  uint8_t ret = fingerprint.storeFingerprint(id);
  if (ret == 0)
  {
    Serial.print("Store success, ID = ");
    Serial.println(id);
    Serial.print("Enroll count = ");
    Serial.println(fingerprint.getEnrollCount());
  }
  else
  {
    Serial.print("Store failed: ");
    Serial.println(fingerprint.getErrorDescription());
  }
}
void matchFingerprint()
{
  // 【非阻塞核心】瞬间检测是否有手指放在传感器上，如果没有直接跳过，不浪费CPU时间
  if (fingerprint.detectFinger()) 
  {
    Serial.println("Finger detected! Capturing...");
    
    // 既然有手指了，再进行采集。这里把超时时间缩短为 2 秒足够了
    if (fingerprint.collectionFingerprint(2) != ERR_ID809)
    {
      Serial.println("Capturing succeeds");
      Serial.println("Please release your finger");

      // 等待手指离开（防止手指一直按着导致重复触发开锁/报警）
      while (fingerprint.detectFinger())
      {
        delay(20);
      }

      // 搜索指纹库比对
      uint8_t ret = fingerprint.search();

      if (ret != 0 && ret != ERR_ID809)
      {
        Serial.print("Matching succeeds, ID = ");
        Serial.println(ret);
        finger_command = 1; // 成功，准备开锁
      }
      else
      {
        Serial.println("Matching fails");
        finger_command = 2; // 失败，准备记录错误次数
      }
    }
    else
    {
      Serial.print("Capturing fails: ");
      Serial.println(fingerprint.getErrorDescription());
      finger_command = 2; // 如果采集出错了（比如手指滑了一下），也算作一次失败
    }
  }
}

void deleteAllFingerprints()
{
  uint8_t ret = fingerprint.delFingerprint(DELALL);
  if (ret == 0)
  {
    Serial.println("All fingerprints deleted.");
  }
  else
  {
    Serial.print("Delete failed: ");
    Serial.println(fingerprint.getErrorDescription());
  }
}
void unlock_servo()
{
  Serial.println(">>> Action: Servo UNLOCKED! <<<");
  door_state = 0;
}

void lock_servo()
{
  Serial.println(">>> Action: Servo LOCKED! <<<");
  door_state = 1;
  
  // 使用电机 3 (对应端口 10)
  Serial.println("Motor 3: Locking movement...");
  mc.setSpeed(3, 100);  // 很低速度（轻轻动）
  delay(1000);          // 1秒
  mc.setSpeed(3, 0);    // 停止
}
void buzzer()
{
    if (status== true)
    {
      Serial.println("Beep");
      tone(buzzerPin, 2400);
      delay(500);
    }
    else
    {
      noTone(buzzerPin);
      delay(1000);
    }
}

