#include "Lamp.h"

Lamp::Lamp(unsigned char index, unsigned char *address, const char *topic, const char *topicSet)
{
    this->index = index;
    this->address = address;
    this->topic = topic;
    this->topicSet = topicSet;

    this->red = 255;
    this->green = 255;
    this->blue = 255;
    this->white = 255;
    this->brightness = 255;

    this->realRed = 0;
    this->realGreen = 0;
    this->realBlue = 0;
    this->realWhite = 0;

    this->stateOn = false;
    this->realStateOn = false;

    this->startFade = false;
    this->lastLoop = 0;
    this->transitionTime = 0;
    this->inFade = false;
    this->loopCount = 0;

    this->flash = false;
    this->startFlash = false;
    this->flashLength = 0;
    this->flashStartTime = 0;
    this->flashRed = red;
    this->flashGreen = green;
    this->flashBlue = blue;
    this->flashWhite = white;
    this->flashBrightness = brightness;
}