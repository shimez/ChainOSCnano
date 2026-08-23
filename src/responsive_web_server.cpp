#include "responsive_web_server.h"

#include <Arduino.h>
#include <errno.h>
#include <lwip/sockets.h>

#include "config.h"

namespace {
constexpr uint32_t WEB_WRITE_STALL_TIMEOUT_MS = 3000;
constexpr uint32_t WEB_WRITE_SELECT_SLICE_US = 50000;
}

size_t ResponsiveWebServer::_currentClientWrite(const char* buffer,
                                                size_t length) {
  return writeWithStallTimeout(reinterpret_cast<const uint8_t*>(buffer), length);
}

size_t ResponsiveWebServer::_currentClientWrite_P(PGM_P buffer,
                                                  size_t length) {
  return writeWithStallTimeout(reinterpret_cast<const uint8_t*>(buffer), length);
}

size_t ResponsiveWebServer::writeWithStallTimeout(const uint8_t* buffer,
                                                  size_t length) {
  if (buffer == nullptr || length == 0) return 0;
  const int socketFd = _currentClient.fd();
  if (socketFd < 0) return 0;

  size_t sent = 0;
  uint32_t lastProgressMs = millis();
  while (sent < length && _currentClient.connected()) {
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(socketFd, &writeSet);
    timeval waitTime{0, WEB_WRITE_SELECT_SLICE_US};
    const int ready =
        lwip_select(socketFd + 1, nullptr, &writeSet, nullptr, &waitTime);
    if (ready > 0 && FD_ISSET(socketFd, &writeSet)) {
      const int result =
          lwip_send(socketFd, buffer + sent, length - sent, MSG_DONTWAIT);
      if (result > 0) {
        sent += static_cast<size_t>(result);
        lastProgressMs = millis();
        continue;
      }
      if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
    } else if (ready < 0 && errno != EINTR) {
      break;
    }
    if (millis() - lastProgressMs >= WEB_WRITE_STALL_TIMEOUT_MS) {
#if CHAINOSCNANO_WEB_PERF_DEBUG
      Serial.printf(
          "[ChainOSCnano][WEBPERF] phase=WRITE_STALL sent=%u total=%u timeout=%u\n",
          static_cast<unsigned>(sent), static_cast<unsigned>(length),
          static_cast<unsigned>(WEB_WRITE_STALL_TIMEOUT_MS));
#endif
      _currentClient.stop();
      break;
    }
    yield();
  }
  return sent;
}
