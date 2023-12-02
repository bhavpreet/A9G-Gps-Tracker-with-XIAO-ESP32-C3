#include <Arduino.h>
#define DEBUG true

#ifndef MQTT_DEFINED
#define MQTT_SERVER "io.adafruit.com"
#define MQTT_PORT 1883
#define MQTT_DEFINED 1
#define MQTT_USERNAME "boom"
#define MQTT_PASSWORD "bastic"
#define MQTT_FEED "boom/feeds/gps/csv"
#define MQTT_CLIENT "DogguDoggu"
#endif

#define MENU_BASED_SYSTEM false
#define LOCATION_INTERVAL 10000

// Notes:
/*
Battery status : AT+CBC?
~~~~~~~~~~~~~~~~~~~~~~~~
AT+CBC?

+CBC: 0, 89

OK

Low power with gps: AT+GPSLP=2

LOW POWER GSM: AT+SLEEP=1 (Connect IO25 go GND and continue using AT commands. Needed for this implementation)
~~~~~~~~~~~~
*/

HardwareSerial A9G(0);
String incomingData; // for storing incoming serial data
// String message = "";   // A String for storing the message
// int relay_pin = 2;    // Initialized a pin for relay module
char msg;
char call;
bool serialPassthrough = false;
static unsigned long lastLocation = 0;

void receive_message();
void SendMessage();
void MakeCall();
void RedialCall();
void HangupCall();
void ReceiveCall();
void ReadLocation();
void MQTTPush();
void resetA9G();
void SerialPassthrough();
String sendData(String command, const int timeout, boolean debug);
void menu_loop();
void gprsConnect();
void setLowPowerMode();
void MQTTConnect();

void printUsage()
{
  Serial.println("PIO GSM A9G BEGIN");
  Serial.println("Enter character for control option:");
  Serial.println("h : to disconnect a call");
  Serial.println("i : to receive a call");
  Serial.println("s : to send message");
  Serial.println("c : to make a call");
  Serial.println("e : to redial");
  Serial.println("l : to read location");
  Serial.println("m : MQTT call");
  Serial.println("r : reset");
  Serial.println("p : passthrough");
  Serial.println("- : print this message");
  Serial.println();
}

void setup()
{
  String resp;
  Serial.begin(115200); // baudrate for serial monitor

  Serial.println("Sleeping for 25 seconds");
  sleep(25);
  A9G.begin(115200); // baudrate for GSM shield

  printUsage();

  // Initialize GSM module with AT command
  while (true)
  {
    Serial.println("Initializing A9G");
    resp = sendData("AT", 2000, DEBUG);
    if (resp.indexOf("OK") >= 0)
    {
      Serial.println("A9G is ready");
      break;
    }
    Serial.println("A9G is not responding");
    sleep(5);
  }

  // Connect
  gprsConnect();
  MQTTConnect();
  // Initialize GPS

  resp = sendData("AT+CREG?", 500, DEBUG);
  // Enable GPS; let it aquire the fix
  resp = sendData("AT+GPS=1", 500, DEBUG);
  if (resp.indexOf("OK") >= 0)
  {
    Serial.println("A9G GPS is enabled");
  }
  else
  {
    Serial.println("A9G GPS is not enabled");
    // Call setup again??
    setup();
  }

  setLowPowerMode();
  /*SIM900.println("AT+GPS=1");
  delay(100);
  SIM900.println("AT+GPSRD=5");
  delay(5000);*/

  // // set SMS mode to text mode
  // A9G.print("AT+CMGF=1\r");
  // delay(100);

  // // set gsm module to tp show the output on serial out
  // A9G.print("AT+CNMI=2,2,0,0,0\r");
  // delay(100);
}

void loop()
{
  if (MENU_BASED_SYSTEM)
  {
    menu_loop();
    return;
  }

  // every Location interval seconds, check for location and send over mqtt
  if (millis() - lastLocation > LOCATION_INTERVAL)
  {
    lastLocation = millis();
    MQTTPush();
  }
}

void menu_loop()
{
  if (serialPassthrough)
  {
    SerialPassthrough();
    return;
  }

  receive_message();

  if (Serial.available() > 0)
    switch (Serial.read())
    {
    case 's':
      SendMessage();
      break;
    case 'c':
      MakeCall();
      break;
    case 'h':
      HangupCall();
      break;
    case 'e':
      RedialCall();
      break;
    case 'i':
      ReceiveCall();
      break;
    case 'l':
      ReadLocation();
      break;
    case 'm':
      MQTTPush();
      break;
    case 'r':
      resetA9G();
      break;
    case 'p':
      serialPassthrough = true;
      break;
    case '-':
      printUsage();
    }
}
void receive_message()
{
  if (A9G.available() > 0)
  {
    incomingData = A9G.readString(); // Get the data from the serial port.
    Serial.print(incomingData);
    delay(10);
  }
}

void SendMessage()
{
  A9G.println("AT+CMGF=1");                  // Sets the GSM Module in Text Mode
  delay(1000);                               // Delay of 1000 milli seconds or 1 second
  A9G.println("AT+CMGS=\"919868128432\"\r"); // Replace x with mobile number
  delay(1000);
  A9G.println("sim900a sms"); // The SMS text you want to send
  delay(100);
  A9G.println((char)26); // ASCII code of CTRL+Z
  delay(1000);
}

void ReceiveMessage()
{
  A9G.println("AT+CNMI=2,2,0,0,0"); // AT Command to recieve a live SMS
  delay(1000);
  if (A9G.available() > 0)
  {
    msg = A9G.read();
    Serial.print(msg);
  }
}

void MakeCall()
{
  A9G.println("ATD+919868128432;"); // ATDxxxxxxxxxx; -- watch out here for semicolon at the end, replace your number here!!
  Serial.println("Calling  ");      // print response over serial port
  delay(1000);
}

void HangupCall()
{
  A9G.println("ATH");
  Serial.println("Hangup Call");
  delay(1000);
}

void ReceiveCall()
{
  A9G.println("ATA");
  delay(1000);
  {
    call = A9G.read();
    Serial.print(call);
  }
}

void RedialCall()
{
  A9G.println("ATDL");
  Serial.println("Redialing");
  delay(1000);
}

void ReadLocation()
{
  A9G.println("AT+GPS=1");
  delay(1000);
  A9G.println("AT+GPSRD=5");
  delay(1000);
}

String sendData(String command, const int timeout, boolean debug)
{
  String response = "";
  A9G.println(command);
  long int time = millis();
  while ((time + timeout) > millis())
  {
    while (A9G.available())
    {
      char c = A9G.read();
      response += c;
    }
  }
  if (debug)
  {
    Serial.print(response);
  }
  return response;
}

void resetA9G()
{
  sendData("AT+RST=1", 1000, DEBUG);
  delay(1000);
}

String getBatteryPercentage(int delay, bool debug)
{
  String msg = sendData("AT+CBC?", delay, debug);
  if (msg.indexOf("OK") >= 0)
  {
    // Parse the message and return only the third line separated by CRLF
    int startIndex = msg.indexOf("\r\n", msg.indexOf("\r\n") + 1) + 2;
    int endIndex = msg.indexOf("\r\n", startIndex);
    String ret = msg.substring(startIndex, endIndex);
    // ret is of type +CBC: 0, 89, so we need to get the second value
    int commaIndex = ret.indexOf(",");
    if (commaIndex < 0)
    {
      Serial.println("A9G could not get battery percentage");
      return "0";
    }

    String batteryPercentage = ret.substring(commaIndex + 1);
    // trim white space
    batteryPercentage.trim();
    Serial.println("A9G battery percentage: " + batteryPercentage);
    return batteryPercentage;
  }
  return "0";
}

String getLatLong(int delay, bool debug)
{
  String msg = sendData("AT+LOCATION=2", delay, debug);
  if (msg.indexOf("OK") >= 0)
  {
    // Parse the message and return only the third line separated by CRLF
    int startIndex = msg.indexOf("\r\n", msg.indexOf("\r\n") + 1) + 2;
    int endIndex = msg.indexOf("\r\n", startIndex);
    String latLong = msg.substring(startIndex, endIndex);
    // check if latLong has AT+LOCATION
    if (latLong.indexOf("AT+LOCATION") >= 0)
    {
      Serial.println("A9G could not get location");
      return "";
    }
    return latLong;
  }
  return "";
}

void gprsConnect()
{
  sendData("AT+CGATT=1", 1000, DEBUG);
  sendData("AT+CGDCONT=1,\"IP\",\"WWW\"", 1000, DEBUG);
  sendData("AT+CGACT=1,1", 1000, DEBUG);
}

void gpsConnect()
{
  String resp = sendData("AT+GPS=1", 1000, DEBUG);
  if (resp.indexOf("OK") >= 0)
  {
    Serial.println("A9G GPS is enabled");
  }
  else
  {
    Serial.println("A9G GPS is not enabled enabling GPS");
    // Call setup again??
    resetA9G();
    setup();
  }
}

void agpsConnect()
{
  sendData("AT+GPS=0", 1000, DEBUG);     // Disable GPS
  sendData("AT+CGACT=1,1", 1000, DEBUG); // Attach to the network
  String resp = sendData("AT+AGPS=1", 2000, DEBUG);
  if (resp.indexOf("OK") >= 0)
  {
    Serial.println("A9G AGPS is enabled");
  }
  else
  {
    Serial.println("A9G AGPS is not enabled enabling GPS");
    // Call setup again??
    setup();
  }
}

void setLowPowerMode()
{
  sendData("AT+GPSLP=2", 1000, DEBUG);
  sendData("AT+SLEEP=1", 1000, DEBUG);
}

void MQTTConnect()
{
  sendData("AT+MQTTDISCONN", 1000, DEBUG);
  delay(1000);
  // AT+MQTTCONN=<host>,<port>,<clientid>,<aliveSeconds>,<cleansession>,<username>,<password>
  char mqttReq[1024];
  if (sprintf(
          mqttReq,
          "AT+MQTTCONN=\"%s\",1883,\"%s\",120,1,\"%s\",\"%s\"",
          MQTT_SERVER, MQTT_CLIENT, MQTT_USERNAME, MQTT_PASSWORD) == -1)
  {
    Serial.println("Error in sprintf");
  }

  String msg = sendData(mqttReq, 3000, DEBUG);
  if (msg.indexOf("OK") >= 0)
  {
    Serial.println("A9G CONNECT to the arduino MQTT broker");
  }
}

void MQTTPush()
{
  // gprsConnect();
  // MQTTConnect();
  // Get battery percentage and location
  String speed = getBatteryPercentage(1001, DEBUG);
  char elevation[] = "100";

  String latLong = getLatLong(1000, DEBUG);
  // AT+MQTTPUB=<topic>,<message>,<qos>,<duplicate>,<retain>
  if (latLong == "")
  {
    Serial.println("A9G could not get location");
    return;
  }

  char mqttPush[1024];
  sprintf(mqttPush,
          "AT+MQTTPUB=\"%s\",\"%s,%s,%s\",0,0,0",
          MQTT_FEED, speed.c_str(), latLong.c_str(), elevation);
  String msg2 = sendData(mqttPush, 3000, DEBUG);
  if (msg2.indexOf("OK") >= 0)
  {
    Serial.println("A9G SENT message to the arduino MQTT broker");
  }

  delay(2000);
}

void SerialPassthrough()
{
  while (Serial.available() > 0)
  {
    A9G.write(Serial.read());
  }
  while (A9G.available() > 0)
  {
    Serial.write(A9G.read());
  }
}