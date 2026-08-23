#pragma once

#include <WebServer.h>

class ResponsiveWebServer : public WebServer {
 public:
  explicit ResponsiveWebServer(int port = 80) : WebServer(port) {}

 protected:
  size_t _currentClientWrite(const char* buffer, size_t length) override;
  size_t _currentClientWrite_P(PGM_P buffer, size_t length) override;

 private:
  size_t writeWithStallTimeout(const uint8_t* buffer, size_t length);
};
