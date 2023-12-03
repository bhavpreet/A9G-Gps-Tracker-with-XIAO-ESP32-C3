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
#define DEFAULT_TIMEOUT 5000

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

void MakeCall();
void RedialCall();
void HangupCall();
void ReceiveCall();
void ReadLocation();
void MQTTPush();
void resetA9G();
void SerialPassthrough();
String sendData(String command, const int timeout, boolean debug);
String sendData(String command);
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

  sleep(5);
  A9G.begin(115201); // baudrate for GSM shield

  printUsage();

  // Initialize GSM module with AT command
  while (true)
  {
    Serial.println("Initializing A9G");
    resp = sendData("AT");
    if (resp.indexOf("OK") >= 0)
    {
      Serial.println("A9G is ready");
      break;
    }
    Serial.println("A9G is not ready");
    sleep(1);
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

  if (Serial.available() > 0)
    switch (Serial.read())
    {
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

void MakeCall()
{
  A9G.println("ATD+91<your-number>;"); // ATDxxxxxxxxxx; -- watch out here for semicolon at the end, replace your number here!!
  Serial.println("Calling  ");         // print response over serial port
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

String sendData(String command)
{
  return sendData(command, DEFAULT_TIMEOUT, DEBUG);
}

// Sends a command and waits for the response to complete.
// The response either ends with OK or +CME ERROR: <err>
String sendData(String command, const int timeout, boolean debug)
{
  String response = "";
  A9G.println(command); // Send command
  long int time = millis();

  // End of response checks for either OK or +CMS ERROR: <err>
  bool endOfResponse = false;
  while (!endOfResponse && (time + timeout) > millis())
  {
    while (A9G.available())
    {
      char c = A9G.read();
      response += c;
      if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0)
      {
        endOfResponse = true;
      }
    }
    if ((time + timeout) < millis())
    {
      Serial.println("A9G could not get response");
      return "";
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
}

String getBatteryPercentage(int delay, bool debug)
{
  String msg = sendData("AT+CBC?", delay, debug);
  if (msg.indexOf("OK") >= 0)
  {
    // Message looks like +CBC: 0, <pct>, extract <pct> part
    String ret = msg.substring(msg.indexOf("+CBC:"));
    // extract <pct> part, take till CRLF
    String batteryPercentage = ret.substring(ret.indexOf(",") + 1, ret.indexOf("\r\n"));
    batteryPercentage.trim();
    if (debug)
      Serial.println("Precentage = '" + batteryPercentage + "'");
    return batteryPercentage;
  }
  return "1"; // who knows what!
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
  String resp;
  // Disconnect first
  sendData("AT+CGACT=0,1", 1000, DEBUG);
  sendData("AT+CGATT=1", 1000, DEBUG);
  resp = sendData("AT+CGDCONT=1,\"IP\",\"WWW\"", 1000, DEBUG);
  if (resp.indexOf("OK") >= 0)
  {
    Serial.println("A9G GPRS is enabled");
  }
  else
  {
    Serial.println("A9G GPRS is not enabled enabling GPRS");
    // Call setup again??
    resetA9G();
    setup();
  }
  resp = sendData("AT+CGACT=1,1", 1000, DEBUG);
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
  sendData("AT+GPSLP=2");
  sendData("AT+SLEEP=1");
}

void MQTTConnect()
{
  sendData("AT+MQTTDISCONN", 1000, DEBUG);
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