/*
 * ESP8266 MQTT Lights for Home Assistant.
 *
 * Created DIY lights for Home Assistant using MQTT and JSON.
 * This project supports single-color, RGB, and RGBW lights.
 *
 * Copy the included `config-sample.h` file to `config.h` and update
 * accordingly for your setup.
 *
 * See https://github.com/corbanmailloux/esp-mqtt-rgb-led for more information.
 */

#include "src/LivingColors/CC2500.h"
#include "src/LivingColors/ColourConversion.h"
#include "src/LivingColors/LivingColors.h"
#include "src/Lamp.h"

// Set configuration options for LED type, pins, WiFi, and MQTT in the following file:
#include "config.h"

// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>

// http://pubsubclient.knolleary.net/
#include <PubSubClient.h>

#ifdef ESP8266
#define lcMOSI 13 // D7 SPI master data out pin 11
#define lcMISO 12 // D6 SPI master data in pin  12
#define lcSCK 14  // D5 SPI clock pin           13
#define lcCS 15   // D2 SPI slave select pin    10
#else
#define lcMOSI 11 // SPI master data out pin
#define lcMISO 12 // SPI master data in pin
#define lcSCK 13  // SPI clock pin
#define lcCS 10   // SPI slave select pin
#endif

const bool rgb = (CONFIG_STRIP == RGB) || (CONFIG_STRIP == RGBW);
const bool includeWhite = (CONFIG_STRIP == BRIGHTNESS) || (CONFIG_STRIP == RGBW);

const int BUFFER_SIZE = JSON_OBJECT_SIZE(20);
const int COLOR_SET_DELAY_MICRO_SECONDS = 2000;

// {red, grn, blu, wht}
const byte COLORS[][4] = {
    {255, 0, 0, 0},
    {0, 255, 0, 0},
    {0, 0, 255, 0},
    {255, 80, 0, 0},
    {163, 0, 255, 0},
    {0, 255, 255, 0},
    {255, 255, 0, 0}};
const int NUM_COLORS = 7;

WiFiClient espClient;
PubSubClient client(espClient);
LivingColors livcol(lcCS, lcSCK, lcMOSI, lcMISO);
Lamp lamps[] = {
    Lamp(0, (unsigned char[]){0xE7, 0x52, 0xD3, 0x52, 0x99, 0x28, 0x8F, 0xB4, 0x11}, "home/rgb1", "home/rgb1/set"),
    Lamp(1, (unsigned char[]){0x43, 0xB7, 0xBA, 0x14, 0x99, 0x28, 0x8F, 0xB4, 0x11}, "home/rgb2", "home/rgb2/set"),
};
const int NUMBER_OF_LAMPS = sizeof(lamps) / sizeof(Lamp);

void setup()
{
  if (CONFIG_DEBUG)
  {
    Serial.begin(115200);
    Serial.println("serial init");
  }

  livcol.init();
  livcol.clearLamps();

  for (const Lamp &lamp : lamps)
  {
    livcol.addLamp(lamp.address);
  }

  livcol.turnLampOnRGB(1, 0, 255, 255);
  setup_wifi();
  client.setServer(CONFIG_MQTT_HOST, CONFIG_MQTT_PORT);
  client.setCallback(callback);
}

void setup_wifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(CONFIG_WIFI_SSID);

  WiFi.mode(WIFI_STA); // Disable the built-in WiFi access point.
  WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

Lamp &getLamp(char *topicSet)
{
  for (Lamp &lamp : lamps)
  {
    if (strcmp(lamp.topicSet, topicSet) == 0)
    {
      return lamp;
    }
  }
}

/*
  SAMPLE PAYLOAD (BRIGHTNESS):
    {
      "brightness": 120,
      "flash": 2,
      "transition": 5,
      "state": "ON"
    }

  SAMPLE PAYLOAD (RGBW):
    {
      "brightness": 120,
      "color": {
        "r": 255,
        "g": 100,
        "b": 100
      },
      "white_value": 255,
      "flash": 2,
      "transition": 5,
      "state": "ON",
      "effect": "colorfade_fast"
    }
  */
void callback(char *topicSet, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topicSet);
  Serial.print("]\n");
  Lamp &lamp = getLamp(topicSet);

  char message[length + 1];
  for (int i = 0; i < length; i++)
  {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!processJson(lamp, message))
  {
    return;
  }

  if (lamp.stateOn)
  {
    // Update lights
    lamp.realRed = map(lamp.red, 0, 255, 0, lamp.brightness);
    lamp.realGreen = map(lamp.green, 0, 255, 0, lamp.brightness);
    lamp.realBlue = map(lamp.blue, 0, 255, 0, lamp.brightness);
    lamp.realWhite = map(lamp.white, 0, 255, 0, lamp.brightness);
  }
  else
  {
    lamp.realRed = 0;
    lamp.realGreen = 0;
    lamp.realBlue = 0;
    lamp.realWhite = 0;
  }

  lamp.startFade = true;
  lamp.inFade = false; // Kill the current fade

  sendState(lamp);
}

bool processJson(Lamp &lamp, char *message)
{
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject &root = jsonBuffer.parseObject(message);

  if (!root.success())
  {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state"))
  {
    if (strcmp(root["state"], CONFIG_MQTT_PAYLOAD_ON) == 0)
    {
      lamp.stateOn = true;
    }
    else if (strcmp(root["state"], CONFIG_MQTT_PAYLOAD_OFF) == 0)
    {
      lamp.stateOn = false;
    }
  }

  // If "flash" is included, treat RGB and brightness differently
  if (root.containsKey("flash") ||
      (root.containsKey("effect") && strcmp(root["effect"], "flash") == 0))
  {

    if (root.containsKey("flash"))
    {
      lamp.flashLength = (int)root["flash"] * 1000;
    }
    else
    {
      lamp.flashLength = CONFIG_DEFAULT_FLASH_LENGTH * 1000;
    }

    if (root.containsKey("brightness"))
    {
      lamp.flashBrightness = root["brightness"];
    }
    else
    {
      lamp.flashBrightness = lamp.brightness;
    }

    if (rgb && root.containsKey("color"))
    {
      lamp.flashRed = root["color"]["r"];
      lamp.flashGreen = root["color"]["g"];
      lamp.flashBlue = root["color"]["b"];
    }
    else
    {
      lamp.flashRed = lamp.red;
      lamp.flashGreen = lamp.green;
      lamp.flashBlue = lamp.blue;
    }

    if (includeWhite && root.containsKey("white_value"))
    {
      lamp.flashWhite = root["white_value"];
    }
    else
    {
      lamp.flashWhite = lamp.white;
    }

    lamp.flashRed = map(lamp.flashRed, 0, 255, 0, lamp.flashBrightness);
    lamp.flashGreen = map(lamp.flashGreen, 0, 255, 0, lamp.flashBrightness);
    lamp.flashBlue = map(lamp.flashBlue, 0, 255, 0, lamp.flashBrightness);
    lamp.flashWhite = map(lamp.flashWhite, 0, 255, 0, lamp.flashBrightness);

    lamp.flash = true;
    lamp.startFlash = true;
  }
  else if (rgb && root.containsKey("effect") &&
           (strcmp(root["effect"], "colorfade_slow") == 0 || strcmp(root["effect"], "colorfade_fast") == 0))
  {
    lamp.flash = false;
    lamp.colorfade = true;
    lamp.currentColor = 0;
    if (strcmp(root["effect"], "colorfade_slow") == 0)
    {
      lamp.transitionTime = CONFIG_COLORFADE_TIME_SLOW;
    }
    else
    {
      lamp.transitionTime = CONFIG_COLORFADE_TIME_FAST;
    }
  }
  else if (lamp.colorfade && !root.containsKey("color") && root.containsKey("brightness"))
  {
    // Adjust brightness during colorfade
    // (will be applied when fading to the next color)
    lamp.brightness = root["brightness"];
  }
  else
  { // No effect
    lamp.flash = false;
    lamp.colorfade = false;

    if (rgb && root.containsKey("color"))
    {
      lamp.red = root["color"]["r"];
      lamp.green = root["color"]["g"];
      lamp.blue = root["color"]["b"];
    }

    if (includeWhite && root.containsKey("white_value"))
    {
      lamp.white = root["white_value"];
    }

    if (root.containsKey("brightness"))
    {
      lamp.brightness = root["brightness"];
    }

    if (root.containsKey("transition"))
    {
      lamp.transitionTime = root["transition"];
    }
    else
    {
      lamp.transitionTime = CONFIG_DEFAULT_TRANSITION_TIME;
    }
  }

  return true;
}

void sendState(Lamp &lamp)
{
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject &root = jsonBuffer.createObject();

  root["state"] = (lamp.stateOn) ? CONFIG_MQTT_PAYLOAD_ON : CONFIG_MQTT_PAYLOAD_OFF;
  if (rgb)
  {
    JsonObject &color = root.createNestedObject("color");
    color["r"] = lamp.red;
    color["g"] = lamp.green;
    color["b"] = lamp.blue;
  }

  root["brightness"] = lamp.brightness;

  if (includeWhite)
  {
    root["white_value"] = lamp.white;
  }

  if (rgb && lamp.colorfade)
  {
    if (lamp.transitionTime == CONFIG_COLORFADE_TIME_SLOW)
    {
      root["effect"] = "colorfade_slow";
    }
    else
    {
      root["effect"] = "colorfade_fast";
    }
  }
  else
  {
    root["effect"] = "null";
  }

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(lamp.topic, buffer, true);
  Serial.println(buffer);
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_USER, CONFIG_MQTT_PASS))
    {
      Serial.println("connected");
      for (Lamp &lamp : lamps)
      {
        client.subscribe(lamp.topicSet);
      }
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setColor(Lamp &lamp, int inR, int inG, int inB, int inW)
{
  if (inR == 0 && inG == 0 && inB == 0 && lamp.realStateOn)
  {
    delayMicroseconds(COLOR_SET_DELAY_MICRO_SECONDS * 5);
    Serial.println("LAMP OFF");
    livcol.turnLampOff(lamp.index);
    lamp.realStateOn = false;
  }

  if ((inR > 0 || inG > 0 || inB > 0) && lamp.realStateOn)
  {
    Serial.print("R:");
    Serial.print(inR);
    Serial.print(" G:");
    Serial.print(inG);
    Serial.print(" B:");
    Serial.print(inB);
    Serial.print(" ");
    livcol.setLampColourRGB(lamp.index, inR, inG, inB);
  }

  if ((inR > 0 || inG > 0 || inB > 0) && !lamp.realStateOn)
  {
    Serial.print("LAMP ON r:");
    Serial.print(inR);
    Serial.print(" g: ");
    Serial.print(inG);
    Serial.print(" b: ");
    Serial.println(inB);
    livcol.turnLampOnRGB(lamp.index, inR, inG, inB);
    lamp.realStateOn = true;
  }

  delayMicroseconds(COLOR_SET_DELAY_MICRO_SECONDS);
}

void loop()
{
  if (!client.connected())
  {
    reconnect();
  }

  client.loop();

  for (Lamp &lamp : lamps)
  {
    lampLoop(lamp);
  }
}

void lampLoop(Lamp &lamp)
{
  const int STEPS = (lamp.transitionTime / (COLOR_SET_DELAY_MICRO_SECONDS / 1000 * NUMBER_OF_LAMPS));
  
  if (lamp.flash)
  {
    if (lamp.startFlash)
    {
      lamp.startFlash = false;
      lamp.flashStartTime = millis();
    }

    if ((millis() - lamp.flashStartTime) <= lamp.flashLength)
    {
      if ((millis() - lamp.flashStartTime) % 1000 <= 500)
      {
        setColor(lamp, lamp.flashRed, lamp.flashGreen, lamp.flashBlue, lamp.flashWhite);
      }
      else
      {
        setColor(lamp, 0, 0, 0, 0);
        // If you'd prefer the flashing to happen "on top of"
        // the current color, uncomment the next line.
        // setColor(realRed, realGreen, realBlue, realWhite);
      }
    }
    else
    {
      lamp.flash = false;
      setColor(lamp, lamp.realRed, lamp.realGreen, lamp.realBlue, lamp.realWhite);
    }
  }
  else if (rgb && lamp.colorfade && !lamp.inFade)
  {
    lamp.realRed = map(COLORS[lamp.currentColor][0], 0, 255, 0, lamp.brightness);
    lamp.realGreen = map(COLORS[lamp.currentColor][1], 0, 255, 0, lamp.brightness);
    lamp.realBlue = map(COLORS[lamp.currentColor][2], 0, 255, 0, lamp.brightness);
    lamp.realWhite = map(COLORS[lamp.currentColor][3], 0, 255, 0, lamp.brightness);
    lamp.currentColor = (lamp.currentColor + 1) % NUM_COLORS;
    lamp.startFade = true;
  }

  if (lamp.startFade)
  {
    // If we don't want to fade, skip it.
    if (lamp.transitionTime == 0)
    {
      setColor(lamp, lamp.realRed, lamp.realGreen, lamp.realBlue, lamp.realWhite);

      lamp.redVal = lamp.realRed;
      lamp.grnVal = lamp.realGreen;
      lamp.bluVal = lamp.realBlue;
      lamp.whtVal = lamp.realWhite;

      lamp.startFade = false;
    }
    else
    {
      lamp.loopCount = 0;
      lamp.stepR = calculateStep(lamp.redVal, lamp.realRed, STEPS);
      lamp.stepG = calculateStep(lamp.grnVal, lamp.realGreen, STEPS);
      lamp.stepB = calculateStep(lamp.bluVal, lamp.realBlue, STEPS);
      lamp.stepW = calculateStep(lamp.whtVal, lamp.realWhite, STEPS);
      lamp.redValFadeStart = lamp.redVal;
      lamp.grnValFadeStart = lamp.grnVal;
      lamp.bluValFadeStart = lamp.bluVal;
      lamp.whtValFadeStart = lamp.whtVal;
      lamp.inFade = true;
      lamp.lastLoop = millis();
    }
  }

  if (lamp.inFade)
  {
    lamp.startFade = false;
    //unsigned long now = millis();

    if (lamp.loopCount <= STEPS)
    {
      //lamp.lastLoop = now;

      lamp.redVal = calculateVal(lamp.stepR, lamp.redValFadeStart, lamp.loopCount);
      lamp.grnVal = calculateVal(lamp.stepG, lamp.grnValFadeStart, lamp.loopCount);
      lamp.bluVal = calculateVal(lamp.stepB, lamp.bluValFadeStart, lamp.loopCount);
      lamp.whtVal = calculateVal(lamp.stepW, lamp.whtValFadeStart, lamp.loopCount);
      setColor(lamp, lamp.redVal, lamp.grnVal, lamp.bluVal, lamp.whtVal);
      lamp.loopCount++;
    }
    else
    {
      setColor(lamp, lamp.realRed, lamp.realGreen, lamp.realBlue, lamp.realWhite);
      lamp.redVal = lamp.realRed;
      lamp.grnVal = lamp.realGreen;
      lamp.bluVal = lamp.realBlue;
      lamp.whtVal = lamp.realWhite;
      Serial.print("Loop complete: r: ");
      Serial.print(lamp.redVal);
      Serial.print(", g: ");
      Serial.print(lamp.grnVal);
      Serial.print(", b: ");
      Serial.print(lamp.bluVal);
      Serial.print(" in ");
      Serial.print(millis() - lamp.lastLoop);
      Serial.println("ms");
      lamp.inFade = false;
    }
  }
}

double calculateStep(int prevValue, int endValue, int steps)
{
  return (float)(endValue - prevValue) / (float)steps;
}

int calculateVal(float step, int initialVal, int i)
{
  int newVal = initialVal + step * i;

  // Defensive driving: make sure val stays in the range 0-255
  if (newVal > 255)
  {
    newVal = 255;
  }
  else if (newVal < 0)
  {
    newVal = 0;
  }

  return newVal;
}
