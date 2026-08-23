#pragma once

#include <Arduino.h>
#include <WebServer.h>

String keyWebStyles();
String keyWebScript();
String keyWebConnectedHtml(bool japanese);
String keyWebSavedHtml(bool japanese);
bool keyWebSave(WebServer& server, String& error);
bool keyWebDelete(WebServer& server, String& error);
