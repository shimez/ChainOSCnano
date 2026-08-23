#pragma once

#include <Arduino.h>

void oscSetup();
const String& oscTargetHost();
uint16_t oscTargetPort();
bool oscSaveTarget(const String& host, uint16_t port);
void oscSendDualKey(uint8_t keyNumber, bool pressed);
void oscBeginChainPortUpdate(uint8_t portMask);
void oscRegisterChainKey(const uint8_t* uid, size_t uidLength,
                         uint8_t portMask);
void oscSendChainKey(const uint8_t* uid, size_t uidLength, bool pressed);
void oscRegisterChainEncoder(const uint8_t* uid, size_t uidLength,
                             uint8_t portMask);
void oscSendChainEncoderRotation(const uint8_t* uid, size_t uidLength,
                                 int16_t absoluteValue, int16_t delta);
bool oscSendChainEncoderClick(const uint8_t* uid, size_t uidLength,
                              bool pressed);
void oscRegisterChainAngle(const uint8_t* uid, size_t uidLength,
                           uint8_t portMask);
void oscSendChainAngle(const uint8_t* uid, size_t uidLength, int rawValue);
void oscRegisterChainTof(const uint8_t* uid, size_t uidLength,
                         uint8_t portMask);
void oscSendChainTof(const uint8_t* uid, size_t uidLength, int distanceMm);
void oscRegisterChainJoystick(const uint8_t* uid, size_t uidLength,
                              uint8_t portMask);
void oscSendChainJoystickAxes(const uint8_t* uid, size_t uidLength,
                              int8_t x, int8_t y, uint8_t portMask,
                              bool xChanged, bool yChanged);
bool oscSendChainJoystickClick(const uint8_t* uid, size_t uidLength,
                               bool pressed);
