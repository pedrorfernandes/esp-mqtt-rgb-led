#include <arduino.h>

class Lamp
{
public:
  unsigned char index;
  unsigned char *address;
  const char *topic;
  const char *topicSet;

  // Maintained state for reporting to HA
  byte red;
  byte green;
  byte blue;
  byte white;
  byte brightness;

  // Real values to write to the LEDs (ex. including brightness and state)
  byte realRed;
  byte realGreen;
  byte realBlue;
  byte realWhite;
  bool stateOn;
  bool realStateOn;

  // Values for fade/transitions
  bool startFade;
  unsigned long lastLoop;
  int transitionTime;
  bool inFade;
  int loopCount;
  float stepR;
  float stepG;
  float stepB;
  float stepW;
  int redVal;
  int grnVal;
  int bluVal;
  int whtVal;
  int redValFadeStart;
  int grnValFadeStart;
  int bluValFadeStart;
  int whtValFadeStart;
  
  // Globals for flash
  bool flash;
  bool startFlash;
  int flashLength;
  unsigned long flashStartTime;
  byte flashRed;
  byte flashGreen;
  byte flashBlue;
  byte flashWhite;
  byte flashBrightness;

  // Globals for colorfade
  bool colorfade;
  int currentColor;

  Lamp(unsigned char index, unsigned char *address, const char *topic, const char *topicSet);
};