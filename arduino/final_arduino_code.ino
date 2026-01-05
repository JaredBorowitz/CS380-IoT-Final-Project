#include <DHT11.h>

#define DHT_PIN 10
#define DHT_TYPE DHT11
#define US_TRIG 3
#define US_ECHO 2
#define IN1 6
#define IN2 7
#define  IN3 8
#define IN4 9

const int ENA = 5;    //left
const int ENB = 11;    //right

long time;
float distance;
float speed = 0.0343;
bool isInit = true;
bool trueRight = true;
float runningTemp;
int count;
int obstacleCounter = 0;
int freeCounter = 0;
unsigned long startMillis;
unsigned long shortMillis;  //used to print only ever 1 sec
unsigned long currentMillis;
unsigned long turnMillis;
unsigned long pauseMillis;
unsigned long turnDurationR = 550;
unsigned long turnDurationL = 520;
unsigned long turnDuration180 = 975;
unsigned long pauseDuration = 750;

unsigned long waitTime = 10000;
DHT11 dht11(DHT_PIN);

enum STATE {
  INIT,
  DRIVE,
  TEMP,
  DIST,
  STOP,
  DESC     
};
enum DECISION {
  NO_DESC,
  L_TURN,
  R_TURN,
  TURN_180
};
STATE currentState = INIT;
DECISION decisionState = NO_DESC;

unsigned long driveStartMillis = 0;
float getDistance();
bool checkObstacle();
void ClearBuffer();
void FullStop();
void DriveForward();
void LeftTurn();
void RightTurn();
void Turn180();

void stopAndReportDriveTime();

void sendEvent(const char* name) {
  Serial.print("EV,");
  Serial.println(name);
}

void sendTurn(const char* name, int deg) {
  Serial.print("EV,");
  Serial.print(name);
  Serial.print(",");
  Serial.println(deg);
}

void sendRange(float d_cm) {
  Serial.print("RANGE,");
  Serial.println(d_cm, 1);
}

void sendDriveDT(unsigned long dt_ms) {
  Serial.print("EV,DRIVE_DT,");
  Serial.println(dt_ms);
}

//added
void sendTemp(float tempF) {
  Serial.print("TEMP,");
  Serial.println(tempF, 1);
}


void setup() {
  Serial.begin(9600);
  pinMode(US_TRIG, OUTPUT);
  pinMode(US_ECHO, INPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  delay(1000);

  startMillis = millis();
  shortMillis = millis();

}

void loop() {
  char input = -1;

  if(Serial.available() > 0) {
    input = Serial.read();
  }
  if (input == '\n' || input == '\r') {
    input = -1;  
  }

  /*
  //added
  unsigned long now = millis();
    if (now - shortMillis >= 1000) {
      shortMillis = now;

      int tC = dht11.readTemperature();
      if (tC != DHT11::ERROR_CHECKSUM && tC != DHT11::ERROR_TIMEOUT) {
        float tempF = tC * 1.8f + 32.0f;
        sendTemp(tempF);
      }
    }
*/


  if(input != -1) {
    if(input == 'D') {
      currentState = DRIVE;
      sendEvent("DRIVE_START");
      driveStartMillis = millis();
      Serial.println("Starting Drive, Driving for 10 secs or until obstacle...");


      distance = getDistance();
      DriveForward();
      startMillis = millis();
      shortMillis = millis();


      input = -1;
    }
    else if(input == 'T') {
      currentState = TEMP;
      startMillis = millis();
      shortMillis = millis();
      input = -1;
    }
    else if(input == 'C') {
      currentState = DIST;
      input = -1;
    }
    else if(input == 'L') {
      decisionState = L_TURN;
//added
      stopAndReportDriveTime();
      sendTurn("TURN_L", 90);


      Serial.println("Left turning...");
      LeftTurn();
      turnMillis = millis();


      input = -1;
    }
    else if(input == 'R') {
      decisionState = R_TURN;
      //added
      stopAndReportDriveTime();
      sendTurn("TURN_R", 90);


      RightTurn();
      turnMillis = millis();



      input = -1;
    }
    else if(input == 'S') {
      currentState = STOP;
      input = -1;
    }
    else {
      Serial.println("Invalid option, please try again");
    }
  }

  switch(currentState) {
    case INIT:
      if(isInit) {
        Serial.println("This is the IDLE state!");
        Serial.println("Please choose an Option:");
        Serial.println("D: put car in Drive");
        Serial.println("T: read temperatures");
        Serial.println("C: check distance");
        isInit = false;
      }
      break;
    case DRIVE:
      if(checkObstacle()) {
        decisionState = R_TURN;
        //shouldnt need anymore should stop from checkobstacle.stop and report time//FullStop();
        //if(driving for more than 2 seconds)
          //take temp
          //send distance data  
        currentState = DESC;
        //added
        sendTurn("TURN_R", 90);
        turnMillis = millis();
        RightTurn();
      }
      break;

    case TEMP:
      currentMillis = millis();
      if(currentMillis - startMillis >=500) {
        Serial.println("Reading temps...");
        int tC = dht11.readTemperature();
        float temperature = tC * 1.8f + 32.0f;
        runningTemp +=temperature;
        count++;
        startMillis = currentMillis;
      }
      if(count >=10) {
        runningTemp = runningTemp / count;
        Serial.print("The average Temp is: ");
        Serial.println(runningTemp);
        runningTemp = 0;
        count = 0;
        currentState = INIT;
        isInit = true;
      } 
      break;
    case DIST:
      if(checkObstacle()) {
        //again should be handled by checkobstacle//FullStop();
        currentState = INIT;
        isInit = true;
      }
      else{
        Serial.println("Clear to drive2");
        currentState = INIT;
        isInit = true;
      }
      break;
    case STOP:
      stopAndReportDriveTime();
      currentState = INIT;
      isInit = true;
      break;
    case DESC:
      break;
  }


switch(decisionState) {  
  case NO_DESC:
    break;
  case L_TURN:
      if (millis() - turnMillis >= turnDurationL) {
        FullStop();
        delay(250);
        //added
        sendEvent("DRIVE_START");
        driveStartMillis = millis();
        currentState = DRIVE;
        DriveForward();
        startMillis = millis();
        shortMillis = millis();
        decisionState = NO_DESC;
      }
    break;
  case R_TURN:
    Serial.println("R_TURN");
    //changed to turndurationR from L
    if(millis() - turnMillis >=turnDurationR) {
        FullStop();
        delay(250);
        if(checkObstacle()) {
          decisionState = TURN_180;
          //added
          sendTurn("TURN_180", 180);
          turnMillis = millis();
          Turn180();
        }
        else {
          decisionState = NO_DESC;
          //TAKE TEMP DATA
          int tC = dht11.readTemperature();
          if (tC != DHT11::ERROR_CHECKSUM && tC != DHT11::ERROR_TIMEOUT) {
            float tempF = tC * 1.8f + 32.0f;
            sendTemp(tempF);
          }

          //added
          sendEvent("DRIVE_START");
          driveStartMillis = millis();
          currentState = DRIVE;
          distance = getDistance();
          DriveForward();
          startMillis = millis();
          shortMillis = millis();
          
        }
    }
    break;
  case TURN_180:
    if(millis() - turnMillis >=turnDuration180) {
        FullStop();
        delay(250);
        if(checkObstacle()) {
          decisionState = L_TURN;
          //added
          sendTurn("TURN_L", 90);
          turnMillis = millis();
          LeftTurn();
        }
        else {
          decisionState = NO_DESC;


          //TAKE TEMP DATA
          int tC = dht11.readTemperature();
          if (tC != DHT11::ERROR_CHECKSUM && tC != DHT11::ERROR_TIMEOUT) {
            float tempF = tC * 1.8f + 32.0f;
            sendTemp(tempF);
          }


          //added
          sendEvent("DRIVE_START");
          driveStartMillis = millis();
          currentState = DRIVE;
          distance = getDistance();
          DriveForward();
          startMillis = millis();
          shortMillis = millis();;
        }
    }
    break;
}




}


bool checkObstacle() {
  while(freeCounter !=2 || obstacleCounter !=2) {
    distance = getDistance();
    //added
    sendRange(distance);

    if (distance < 65 && distance != 0) {
      obstacleCounter++;
      freeCounter = 0;      
      Serial.print("TEST: ");
      Serial.println(distance);
    } else {
      freeCounter++;
      obstacleCounter = 0;    
      Serial.print("TEST: ");
      Serial.println(distance);
    }
    if (obstacleCounter >= 2) {
        Serial.println("There is an obstacle!");
        obstacleCounter = 0;
        freeCounter = 0;
        //added
        stopAndReportDriveTime();


        //should stop immediatley within new function //FullStop();  // stop immediately
        return true;
      }
    else if (freeCounter >= 2) {
      Serial.println("Clear to Drive!");
      obstacleCounter = 0;
      freeCounter = 0;
      return false;
    }
    delay(50);
  }
  return false;
}

void ClearBuffer() {
  while (Serial.available() > 0) {
    Serial.read();
  }
}
void FullStop() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}


//added
void stopAndReportDriveTime() {
  unsigned long now = millis();

  if (driveStartMillis != 0) {
    unsigned long dt = now - driveStartMillis;
    sendDriveDT(dt);
    driveStartMillis = 0;
  }

  FullStop();
  sendEvent("STOP");
  //added so when we call it when it sees an obsitcal it will stop
  delay(750);
}



void DriveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
 
  analogWrite(ENA, 140);
  analogWrite(ENB, 150);
}
void LeftTurn() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
 
  analogWrite(ENA, 255);
  analogWrite(ENB, 255);
}
void RightTurn() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
 
  analogWrite(ENA, 255);
  analogWrite(ENB, 255);
}
void Turn180() {
  // left forward, right backward (spin in place)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, 255);
  analogWrite(ENB, 255);
}
float getDistance() {
  digitalWrite(US_TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);

  time = pulseIn(US_ECHO, HIGH);
  distance = time * speed / 2.0;

  return distance;
}
