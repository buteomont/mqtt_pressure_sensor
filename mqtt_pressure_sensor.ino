/*
 * ESP32 firmware to measure the pressure of the water system and send it to an MQTT 
 * broker.
 * Configuration is done via serial connection.  Enter:
 *  broker:<broker name or address>
 *  port:<port number>   (defaults to 1883)
 *  topic:<topic string> (something like buteomont/water/pressure/psi)
 *  user:<mqtt user>
 *  pass:<mqtt password>
 *  ssid:<wifi ssid>
 *  wifipass:<wifi password>
 *  
 */
 
#include <PubSubClient.h> 
#include <WiFi.h>
#include <EEPROM.h>
#include "mqtt_pressure_sensor.h"

// To connect with SSL/TLS:
// 1) Change WiFiClient to WiFiSSLClient.
// 2) Change port value from 1883 to 8883.
// 3) Change broker value to a server with a known SSL/TLS root certificate 
//    flashed in the WiFi module.

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

const char broker[] = "test.mosquitto.org";
int        port     = 1883;
const char topic[]  = "buteomont/water/pressure/psi";
const char configureTopic[]  = "buteomont/water/pressure/configure/#"; //used to configure this device

const long interval = 1000;
unsigned long previousMillis = 0;

int count = 0;

int sensorPin = A0;     // the input pin for the sensor
char sensorValue[7]=""; // place to store the value coming from the sensor in string form

typedef struct 
  {
  unsigned int validConfig=0; 
  char ssid[SSID_SIZE] = "";
  char wifiPassword[PASSWORD_SIZE] = "";
  char mqttBrokerAddress[ADDRESS_SIZE]=""; //default
  int mqttBrokerPort=1883;
  char mqttUsername[USERNAME_SIZE]="";
  char mqttPassword[PASSWORD_SIZE]="";
  char mqttTopic[MQTT_TOPIC_SIZE]="";
  int mqttPostInterval=1000;
  } conf;

conf settings; //all settings in one struct makes it easier to store in EEPROM
boolean settingsAreValid=false;

String commandString = "";     // a String to hold incoming commands from serial
bool commandComplete = false;  // goes true when enter is pressed

void setup() 
  {
  //***************Initialize serial and wait for port to open:
  Serial.begin(115200);
  Serial.setTimeout(10000);
  Serial.println();
  delay(500);
  Serial.println("Starting up...");
  while (!Serial) 
    {
    ; // wait for serial port to connect. Needed for native USB port only
    }
    
  EEPROM.begin(sizeof(settings)); //fire up the eeprom section of flash
  Serial.print("Settings object size=");
  Serial.println(sizeof(settings));
  
  commandString.reserve(200); // reserve 200 bytes for the command string

  loadSettings(); //set the values from eeprom

  if (settingsAreValid)
    {
    // ********************* attempt to connect to Wifi network:
    Serial.print("Attempting to connect to WPA SSID \"");
    Serial.print(settings.ssid);
    Serial.println("\"");
    while (WiFi.begin(settings.ssid, settings.wifiPassword) != WL_CONNECTED) 
      {
      // failed, retry
      Serial.print(".");
      
      // Check for input in case it needs to be changed to work
      if (Serial.available())
        {
        serialEvent();
        String cmd=getConfigCommand();
        if (cmd.length()>0)
          {
          processCommand(cmd);
          }
        }
      else
        delay(5000);
      }
  
    Serial.println("You're connected to the network");
    Serial.println();

    // ********************* Initialize the MQTT connection
    mqttClient.setServer(settings.mqttBrokerAddress, settings.mqttBrokerPort);
    delay(2000);  //give wifi a chance to warm up
    reconnect();     
    }
    
  Serial.println("\nConfiguration is done via serial connection.  You can enter:\n");
  showSettings();
  }

void showSettings()
  {
  Serial.print("broker:<MQTT broker host name or address> (");
  Serial.print(settings.mqttBrokerAddress);
  Serial.println(")");
  Serial.print("port:<port number>   (");
  Serial.print(settings.mqttBrokerPort);
  Serial.println(")");
  Serial.print("topic:<topic string> (");
  Serial.print(settings.mqttTopic);
  Serial.println(")");  
  Serial.print("user:<mqtt user> (");
  Serial.print(settings.mqttUsername);
  Serial.println(")");
  Serial.print("pass:<mqtt password> (");
  Serial.print(settings.mqttPassword);
  Serial.println(")");
  Serial.print("ssid:<wifi ssid> (");
  Serial.print(settings.ssid);
  Serial.println(")");
  Serial.print("wifipass:<wifi password> (");
  Serial.print(settings.wifiPassword);
  Serial.println(")");
  Serial.print("interval:<post interval in seconds>   (");
  Serial.print(settings.mqttPostInterval/1000);
  Serial.println(")");
  Serial.println("\n*** Use \"reset:yes\" to reset all settings ***\n");
  }

/*
 * Reconnect to the MQTT broker
 */
void reconnect() 
  {
  // Loop until we're reconnected
  while (!mqttClient.connected()) 
    {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(),settings.mqttUsername,settings.mqttPassword))
      {
      Serial.println("connected to MQTT broker.");
      }
    else 
      {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      Serial.println("Will try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      }
    }
  }


/*
 * Read and return the pressure value from the sensor
 */
char* getPressure() 
  {
  int value = analogRead(sensorPin); 
  sprintf(sensorValue,"%d",value);
  return sensorValue;
  }

/*
 * Check for configuration input via the serial port.  Return a null string 
 * if no input is available or return the complete line otherwise.
 */
String getConfigCommand()
  {
  if (commandComplete) 
    {
    Serial.println(commandString);
    String newCommand=commandString;
    // clear the string:
    commandString = "";
    commandComplete = false;
    return newCommand;
    }
  else return "";
  }

void processCommand(String cmd)
  {
  const char *str=cmd.c_str();
  char *val=NULL;
  char *nme=strtok((char *)str,":");
  if (nme!=NULL)
    val=strtok(NULL,":");

  //Get rid of the carriage return
  if (val!=NULL && strlen(val)>0 && val[strlen(val)-1]==13)
    val[strlen(val)-1]=0; 

  if (nme==NULL || val==NULL || strlen(nme)==0 || strlen(val)==0)
    {
    showSettings();
    
    return;  
    }
  else if (strcmp(nme,"broker")==0)
    {
    strcpy(settings.mqttBrokerAddress,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"port")==0)
    {
    settings.mqttBrokerPort=atoi(val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"topic")==0)
    {
    strcpy(settings.mqttTopic,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"user")==0)
    {
    strcpy(settings.mqttUsername,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"pass")==0)
    {
    strcpy(settings.mqttPassword,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"ssid")==0)
    {
    strcpy(settings.ssid,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"wifipass")==0)
    {
    strcpy(settings.wifiPassword,val);
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if (strcmp(nme,"interval")==0)
    {
    settings.mqttPostInterval=atoi(val)*1000;
    saveSettings();
    if (settingsAreValid)
      ESP.restart();
    }
  else if ((strcmp(nme,"reset")==0) && (strcmp(val,"yes")==0)) //reset all eeprom settings
    {
    Serial.println("\n*********************** Resetting EEPROM Values ************************");
    initializeSettings();
    saveSettings();
    ESP.restart();
    }
  else
    showSettings();
  return;
  }

void initializeSettings()
  {
  settings.validConfig=0; 
  strcpy(settings.ssid,"");
  strcpy(settings.wifiPassword,"");
  strcpy(settings.mqttBrokerAddress,""); //default
  settings.mqttBrokerPort=1883;
  strcpy(settings.mqttUsername,"");
  strcpy(settings.mqttPassword,"");
  strcpy(settings.mqttTopic,"");
  settings.mqttPostInterval=1000;
  }

void loop() 
  {
  // serialEvent is not interrupt driven on the ESP32 for some reason. Do it here.
  if (Serial.available())
    serialEvent();
    
  // call loop() regularly to allow the library to send MQTT keep alives which
  // avoids being disconnected by the broker
  mqttClient.loop();



  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= settings.mqttPostInterval && mqttClient.connected()) 
    {
    // save the last time a message was sent
    previousMillis = currentMillis;

    //publish the current pressure
    char* pressure=getPressure();
    mqttClient.publish(settings.mqttTopic,pressure);
    Serial.print(count++);
    Serial.print("\t");
    Serial.print(settings.mqttTopic);
    Serial.print("\t");
    Serial.println(pressure);
    }

  String cmd=getConfigCommand();
  if (cmd.length()>0)
    {
    processCommand(cmd);
    }
  }


/*
*  Initialize the settings from eeprom and determine if they are valid
*/
void loadSettings()
  {
  EEPROM.get(0,settings);
  if (settings.validConfig==VALID_SETTINGS_FLAG)    //skip loading stuff if it's never been written
    {
    settingsAreValid=true;
    Serial.println("Loaded configuration values from EEPROM");
//    showSettings();
    }
  else
    {
    Serial.println("Skipping load from EEPROM, device not configured.");    
    settingsAreValid=false;
    }
  }

/*
 * Save the settings to EEPROM. Set the valid flag if everything is filled in.
 */
boolean saveSettings()
  {
  if (strlen(settings.ssid)>0 &&
    strlen(settings.wifiPassword)>0 &&
    strlen(settings.mqttBrokerAddress)>0 &&
    settings.mqttBrokerPort!=0 &&
//    strlen(settings.mqttUsername)>0 &&
//    strlen(settings.mqttPassword)>0 &&
    strlen(settings.mqttTopic)>0 &&
    settings.mqttPostInterval!=0)
    {
    Serial.println("Settings deemed valid");
    settings.validConfig=VALID_SETTINGS_FLAG;
    settingsAreValid=true;
    }
  else
    {
    Serial.println("Settings deemed incomplete");
    settings.validConfig=0;
    settingsAreValid=false;
    }
    
  EEPROM.put(0,settings);
  return EEPROM.commit();
  }

/*
  SerialEvent occurs whenever a new data comes in the hardware serial RX. This
  routine is run between each time loop() runs, so using delay inside loop can
  delay response. Multiple bytes of data may be available.
*/
void serialEvent() {
  while (Serial.available()) 
    {
    // get the new byte:
    char inChar = (char)Serial.read();

    // if the incoming character is a newline, set a flag so the main loop can
    // do something about it:
    if (inChar == '\n') 
      {
      commandComplete = true;
      }
    else
      {
      // add it to the inputString:
      commandString += inChar;
      }
    }
  }
