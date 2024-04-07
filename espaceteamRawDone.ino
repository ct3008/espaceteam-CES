/*
  ESP-NOW Multi Unit Demo
  esp-now-multi.ino
  Broadcasts control messages to all devices in network
  Load script on multiple devices

  DroneBot Workshop 2022
  https://dronebotworkshop.com
*/

// Include Libraries
#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#define RED TFT_RED

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

String cmd1 = "";
String cmd2 = "";
volatile bool scheduleCmd1Send = false;
volatile bool scheduleCmd2Send = false;
int numSlow = 0;
String room = "";

enum ScreenState {
  START_SCREEN,
  GAME_SCREEN
};

ScreenState currentScreen = START_SCREEN;

String cmdRecvd = "";
const String waitingCmd = "Wait for cmds";
bool redrawCmdRecvd = false;

int progress = 0;
bool redrawProgress = true;

int lastRedrawTime = 0;

int level = 0;

//we could also use xSemaphoreGiveFromISR and its associated fxns, but this is fine
volatile bool scheduleCmdAsk = true;
hw_timer_t * askRequestTimer = NULL;
volatile bool askExpired = false;
hw_timer_t * askExpireTimer = NULL;
int expireLength = 25;
bool hardLevel = false;

#define ARRAY_SIZE 10
const String commandVerbs[ARRAY_SIZE] = {"Buzz", "Engage", "Floop", "Bother", "Twist", "Jingle", "Jangle", "Yank", "Press", "Play"};
const String commandNounsFirst[ARRAY_SIZE] = {"foo", "dev", "bob", "jaw", "tot", "wu", "fry", "rot", "tea", "bee"};
const String commandNounsSecond[ARRAY_SIZE] = {"bars", "ices", "pins", "nobs", "zops", "tang", "bell", "wels", "pops", "bops"};
const String commandNounsThird[ARRAY_SIZE] = {"twix", "flop", "drip", "quad", "spin", "whim", "ding", "puff", "vibe", "loop"};

int lineHeight = 30;

// Define LED and pushbutton pins
#define BUTTON_LEFT 0
#define BUTTON_RIGHT 35


void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}


void receiveCallback(const uint8_t *macAddr, const uint8_t *data, int dataLen)
/* Called when data is received
   You can receive 3 types of messages
   1) a "ASK" message, which indicates that your device should display the cmd if the device is free
   2) a "DONE" message, which indicates the current ASK? cmd has been executed
   3) a "PROGRESS" message, indicating a change in the progress of the spaceship
   
   Messages are formatted as follows:
   [A/D]: cmd
   For example, an ASK message for "Twist the wutangs":
   A: Twist the wutangs
   For example, a DONE message for "Engage the devnobs":
   D: Engage the devnobs
   For example, a PROGESS message for 75% progress
   P: 75
*/

{
  // Only allow a maximum of 250 characters in the message + a null terminating byte
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  // Make sure we are null terminated
  buffer[msgLen] = 0;
  String recvd = String(buffer);
  Serial.println(recvd);
  // Format the MAC address
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);

  // Send Debug log message to the serial port
  Serial.printf("Received message from: %s \n%s\n", macStr, buffer);
  if (recvd[0] == 'A' && recvd[1] == room[0] && cmdRecvd == waitingCmd && random(100) < 30) //only take an ask if you don't have an ask already and only take it XX% of the time
  {
    Serial.println("---------------------------A" + room);
    recvd.remove(0,3);
    cmdRecvd = recvd;
    redrawCmdRecvd = true;
    timerStart(askExpireTimer); //once you get an ask, a timer starts
  }
  else if (recvd[0] == 'D' && recvd[1] == room[0] && recvd.substring(3) == cmdRecvd)
  {
    Serial.println("---------------------------D" + room);
    timerWrite(askExpireTimer, 0);
    timerStop(askExpireTimer);
    cmdRecvd = waitingCmd;
    progress = progress + 1;
    broadcast("P" + room + ": " + String(progress));
    // broadcast("P: " + String(progress));
    redrawCmdRecvd = true;
    
  }
  else if (recvd[0] == 'P' && recvd[1] == room[0])
  {
    recvd.remove(0,3);
    progress = recvd.toInt();
    redrawProgress = true;
  }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
  Serial.print("Last Packet Sent to: ");
  Serial.println(macStr);
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void broadcast(const String &message)
// Emulates a broadcast
{
  // Broadcast a message to every device in range
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress))
  {
    esp_now_add_peer(&peerInfo);
  }
  // Send message
  esp_err_t result = esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());

}

void IRAM_ATTR sendCmd1(){
  scheduleCmd1Send = true;
}

void IRAM_ATTR sendCmd2(){
  scheduleCmd2Send = true;
}

void IRAM_ATTR onAskReqTimer(){
  scheduleCmdAsk = true;
}

void IRAM_ATTR onAskExpireTimer(){
  askExpired = true;
  timerStop(askExpireTimer);
  timerWrite(askExpireTimer, 0);
}

void espnowSetup() {
  // Set ESP32 in STA mode to begin with
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.println("ESP-NOW Broadcast Demo");

  // Print MAC address
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Disconnect from WiFi
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK)
  {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sentCallback);
  }
  else
  {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void buttonSetup(){
  pinMode(BUTTON_LEFT, INPUT);
  pinMode(BUTTON_RIGHT, INPUT);

  attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), sendCmd1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), sendCmd2, FALLING);
}

void textSetup(){
  tft.init();
  tft.setRotation(0);
  
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  drawControls();

  cmdRecvd = waitingCmd;
  redrawCmdRecvd = true;
}

void timerSetup(){
  askRequestTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(askRequestTimer, &onAskReqTimer, true);
  timerAlarmWrite(askRequestTimer, 5*1000000, true);//send out an ask every 5 secs
  timerAlarmEnable(askRequestTimer);

  askExpireTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(askExpireTimer, &onAskExpireTimer, true);
  timerAlarmWrite(askExpireTimer, expireLength*1000000, true);
  timerAlarmEnable(askExpireTimer);
  timerStop(askExpireTimer);

}
void setup()
{
  Serial.begin(115200);

  textSetup();
  buttonSetup();
  espnowSetup();
  timerSetup();

}

String genCommand() {
  String verb = commandVerbs[random(ARRAY_SIZE)];
  String noun1 = commandNounsFirst[random(ARRAY_SIZE)];
  String noun2 = commandNounsSecond[random(ARRAY_SIZE)];
  String cmd = noun1 + noun2;
  if (hardLevel) {
    String noun3 = commandNounsThird[random(ARRAY_SIZE)];
    cmd = noun1 + noun2 + noun3;
    int indexToChange = random(cmd.length());
    char newChar = random(26) + 'a';
    cmd[indexToChange] = newChar;

    return verb + " " + cmd;
  }

  return verb + " " + cmd;
}

void drawControls(){
  cmd1 = genCommand();
  cmd2 = genCommand();
  cmd1.indexOf(' ');
  tft.drawString("B1: " + cmd1.substring(0, cmd1.indexOf(' ')), 0, 90, 2);
  tft.drawString(cmd1.substring(cmd1.indexOf(' ')+1), 0, 90+lineHeight, 1);
  tft.drawString("B2: " + cmd2.substring(0, cmd2.indexOf(' ')), 0, 170, 2);
  tft.drawString(cmd2.substring(cmd2.indexOf(' ')+1), 0, 170+lineHeight, 1);
  
}

void displayStartScreen() {
  while(currentScreen == START_SCREEN){
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextWrap(true);
    tft.drawString("LEFT", 5, 50);
    tft.drawString("for room 1", 5, 80);
    tft.drawString("RIGHT", 5, 110);
    tft.drawString("for room 2", 5, 140);
    if (digitalRead(BUTTON_LEFT) == LOW){
      room = "1";
      
      tft.fillScreen(TFT_BLACK);
      Serial.println("------------------------" + room);
      textSetup();
      // buttonSetup();
      // espnowSetup();
      // timerSetup();
      currentScreen = GAME_SCREEN;
      
    }

    else if (digitalRead(BUTTON_RIGHT) == LOW){
      room = "2";
      tft.fillScreen(TFT_BLACK);
      textSetup();
      Serial.println("------------------------" + room);
      // buttonSetup();
      // espnowSetup();
      // timerSetup();
      currentScreen = GAME_SCREEN;
      

    }
    delay(100);
  }
  
}

void loop()
{
  if(currentScreen == START_SCREEN){
    displayStartScreen();
  }
  
  
  
  if (scheduleCmd1Send){
    broadcast("D" + room + ": "+cmd1);
    // broadcast("D: "+cmd1);
    scheduleCmd1Send = false;
  }
  if (scheduleCmd2Send){
    broadcast("D" + room + ": "+cmd2);
    // broadcast("D: "+cmd2);
    scheduleCmd2Send = false;
  }
  if (scheduleCmdAsk) {
    String cmdAsk = random(2) ? cmd1 : cmd2;
    broadcast("A" + room + ": "+cmdAsk);
    // broadcast("A: "+cmdAsk);
    scheduleCmdAsk = false;
  }
  if (askExpired) {
    progress = max(0, progress - 1);
    broadcast("P" + room + ": " + String(progress));
    // broadcast(String(progress));
    numSlow = numSlow + 1;
    //tft.fillRect(0, 0, 135, 90, TFT_RED);
    cmdRecvd = waitingCmd;
    redrawCmdRecvd = true;
    askExpired = false;
    int multiplier = level + 1;
    for (int i = 1; i <= (numSlow * multiplier); i++) 
    {
      tft.fillCircle(random(135), random(120, 240), 10, TFT_RED);
      delay(5); 
    }
  }

  if ((millis() - lastRedrawTime) > 50){
    tft.fillRect(15, lineHeight*2+14, 100, 6, TFT_GREEN);
    tft.fillRect(16, lineHeight*2+14+1, (((expireLength * 1000000.0)-timerRead(askExpireTimer))/(expireLength * 1000000.0)) * 98, 4, TFT_RED);
    lastRedrawTime = millis();
  }

  if (redrawCmdRecvd || redrawProgress) {
    tft.fillRect(0, 0, 135, 90, TFT_BLACK);
    tft.setTextWrap(true);
    String tempCmd = cmdRecvd;
    if (cmdRecvd[0] == ' ') {
      tempCmd = tempCmd.substring(1);
    }
    tft.drawString(tempCmd.substring(0, tempCmd.indexOf(' ')), 0, 0, 2);
    tft.drawString(tempCmd.substring(tempCmd.indexOf(' ')+1), 0, 0+lineHeight, 1);

    redrawCmdRecvd = false;
    
    if (progress%20 == 0 && progress != 0) {
      expireLength = expireLength - ((floor(progress/20))*2);
      tft.setTextWrap(true);
      tft.fillScreen(TFT_BLUE);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
      tft.drawString("LEVEL", 45, 20, 2);
      tft.drawString("UP!!!", 20, 80, 2);
      tft.drawString("Level: "+String(floor(progress/20)), 18, 130, 2);
      progress = progress + 1;
      delay(1500);
      if (floor(progress/20)>=3) {
        hardLevel = true; 
      }
      textSetup();
      //buttonSetup();
      //espnowSetup();
      //timerSetup();
    } else if (progress >= 100)
    {
      tft.fillScreen(TFT_BLUE);
      tft.setTextSize(3);
      tft.setTextWrap(true);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
      tft.drawString("GO", 45, 20, 2);
      tft.drawString("COMS", 20, 80, 2);
      tft.drawString("3930!", 18, 130, 2);
      delay(6000);
      ESP.restart();
    }
    else {
      tft.fillRect(15, lineHeight*2+5, 100, 6, TFT_GREEN);
      tft.fillRect(16, lineHeight*2+5+1, progress, 4, TFT_BLUE);
    }
    redrawProgress = false;
  }

}
