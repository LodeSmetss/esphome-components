#include "custom_modbus_tcp.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

#if defined(ESP32) || defined(ESP8266)
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace esphome {
namespace custom_modbus_tcp {

static const char *const TAG = "custom_modbus_tcp";

static void close_socket_fd(int &fd) {
  if (fd >= 0) {
#if defined(ESP32) || defined(ESP8266)
    ::close(fd);
#else
    ::close(fd);
#endif
    fd = -1;
  }
}

void CustomModbusTcp::setup() {
  ESP_LOGI(TAG, "Setting up Custom Modbus TCP client");
  this->ensure_connected_();
}

void CustomModbusTcp::loop() {
  if (this->socket_fd_ < 0) {
    return;
  }

  const uint32_t now = millis();
  if (this->keepalive_ms_ > 0 && (now - this->last_io_ms_) > this->keepalive_ms_) {
    // Keepalive is implemented as a reconnect boundary to avoid stale sockets.
    ESP_LOGW(TAG, "Keepalive timeout reached, reconnecting socket");
    this->disconnect_();
    this->ensure_connected_();
  }
}

void CustomModbusTcp::dump_config() {
  ESP_LOGCONFIG(TAG, "Custom Modbus TCP:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Unit ID: %u", this->unit_id_);
  ESP_LOGCONFIG(TAG, "  Connect timeout: %u ms", this->connect_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Response timeout: %u ms", this->response_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Retry count: %u", this->retry_count_);
  ESP_LOGCONFIG(TAG, "  Retry backoff: %u ms", this->retry_backoff_ms_);
  ESP_LOGCONFIG(TAG, "  Keepalive: %u ms", this->keepalive_ms_);
  ESP_LOGCONFIG(TAG, "  Connected: %s", this->is_connected() ? "yes" : "no");
}

bool CustomModbusTcp::is_connected() const { return this->socket_fd_ >= 0; }

bool CustomModbusTcp::ensure_connected_() {
  if (this->is_connected()) {
    return true;
  }

  if (this->host_.empty()) {
    this->last_error_ = ModbusReadError::CONNECT_FAILED;
    ESP_LOGE(TAG, "Host is empty, cannot connect");
    return false;
  }

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *result = nullptr;
  const std::string port_str = std::to_string(this->port_);
  const int gai = getaddrinfo(this->host_.c_str(), port_str.c_str(), &hints, &result);
  if (gai != 0 || result == nullptr) {
    this->last_error_ = ModbusReadError::CONNECT_FAILED;
    ESP_LOGW(TAG, "DNS/addr resolve failed for %s:%u", this->host_.c_str(), this->port_);
    return false;
  }

  int fd = -1;
  for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0) {
      continue;
    }

    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(this->connect_timeout_ms_ / 1000U);
    tv.tv_usec = static_cast<suseconds_t>((this->connect_timeout_ms_ % 1000U) * 1000U);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));

    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
      this->socket_fd_ = fd;
      this->last_io_ms_ = millis();
      this->last_error_ = ModbusReadError::OK;
      this->reconnect_count_++;
      freeaddrinfo(result);
      ESP_LOGI(TAG, "Connected to %s:%u", this->host_.c_str(), this->port_);
      return true;
    }

    close_socket_fd(fd);
  }

  freeaddrinfo(result);
  this->last_error_ = ModbusReadError::CONNECT_FAILED;
  ESP_LOGW(TAG, "Failed to connect to %s:%u", this->host_.c_str(), this->port_);
  return false;
}

void CustomModbusTcp::disconnect_() { close_socket_fd(this->socket_fd_); }

bool CustomModbusTcp::send_all_(const uint8_t *data, size_t len) {
  size_t sent_total = 0;
  while (sent_total < len) {
    const int sent = send(this->socket_fd_, reinterpret_cast<const char *>(data + sent_total), len - sent_total, 0);
    if (sent <= 0) {
      return false;
    }
    sent_total += static_cast<size_t>(sent);
  }
  return true;
}

bool CustomModbusTcp::recv_all_(uint8_t *data, size_t len) {
  size_t recv_total = 0;
  while (recv_total < len) {
    const int received = recv(this->socket_fd_, reinterpret_cast<char *>(data + recv_total), len - recv_total, 0);
    if (received <= 0) {
      return false;
    }
    recv_total += static_cast<size_t>(received);
  }
  return true;
}

ModbusReadResult CustomModbusTcp::read_registers_(uint8_t function_code, uint16_t start_address, uint16_t count) {
  ModbusReadResult result;
  result.timestamp_ms = millis();

  if (count == 0 || count > 125) {
    result.error_code = ModbusReadError::BAD_ARGUMENT;
    this->last_error_ = result.error_code;
    return result;
  }

  for (uint8_t attempt = 0; attempt <= this->retry_count_; attempt++) {
    const uint32_t start_ms = millis();

    if (!this->ensure_connected_()) {
      result.error_code = ModbusReadError::CONNECT_FAILED;
      this->last_error_ = result.error_code;
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    uint8_t request[12];
    const uint16_t tx_id = this->transaction_id_++;

    request[0] = static_cast<uint8_t>((tx_id >> 8) & 0xFF);
    request[1] = static_cast<uint8_t>(tx_id & 0xFF);
    request[2] = 0x00;
    request[3] = 0x00;
    request[4] = 0x00;
    request[5] = 0x06;
    request[6] = this->unit_id_;
    request[7] = function_code;
    request[8] = static_cast<uint8_t>((start_address >> 8) & 0xFF);
    request[9] = static_cast<uint8_t>(start_address & 0xFF);
    request[10] = static_cast<uint8_t>((count >> 8) & 0xFF);
    request[11] = static_cast<uint8_t>(count & 0xFF);

    if (!this->send_all_(request, sizeof(request))) {
      result.error_code = ModbusReadError::SEND_FAILED;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    uint8_t header[7];
    if (!this->recv_all_(header, sizeof(header))) {
      result.error_code = ModbusReadError::RECEIVE_FAILED;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    const uint16_t protocol_id = (static_cast<uint16_t>(header[2]) << 8) | header[3];
    const uint16_t length = (static_cast<uint16_t>(header[4]) << 8) | header[5];
    if (protocol_id != 0 || length < 2) {
      result.error_code = ModbusReadError::PROTOCOL_ERROR;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    std::vector<uint8_t> pdu(length - 1, 0);
    if (!this->recv_all_(pdu.data(), pdu.size())) {
      result.error_code = ModbusReadError::RECEIVE_FAILED;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    const uint8_t response_fc = pdu[0];
    if ((response_fc & 0x80U) != 0U) {
      result.error_code = ModbusReadError::EXCEPTION_RESPONSE;
      this->last_error_ = result.error_code;
      this->read_fail_count_++;
      return result;
    }

    if (response_fc != function_code || pdu.size() < 2) {
      result.error_code = ModbusReadError::PROTOCOL_ERROR;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    const uint8_t byte_count = pdu[1];
    if (byte_count != count * 2 || pdu.size() != static_cast<size_t>(2 + byte_count)) {
      result.error_code = ModbusReadError::PROTOCOL_ERROR;
      this->last_error_ = result.error_code;
      this->disconnect_();
      if (attempt < this->retry_count_) {
        delay(this->retry_backoff_ms_);
      }
      continue;
    }

    result.registers.resize(count);
    for (uint16_t i = 0; i < count; i++) {
      const size_t offset = 2 + (i * 2);
      result.registers[i] = (static_cast<uint16_t>(pdu[offset]) << 8) | static_cast<uint16_t>(pdu[offset + 1]);
    }

    this->last_io_ms_ = millis();
    result.ok = true;
    result.error_code = ModbusReadError::OK;
    result.latency_ms = millis() - start_ms;
    this->last_error_ = result.error_code;
    this->read_ok_count_++;
    return result;
  }

  this->read_fail_count_++;
  return result;
}

bool CustomModbusTcp::read_holding_registers(uint16_t start_address, uint16_t count,
                                             std::vector<uint16_t> &out_registers) {
  auto res = this->read_holding_registers_result(start_address, count);
  out_registers = std::move(res.registers);
  return res.ok;
}

bool CustomModbusTcp::read_input_registers(uint16_t start_address, uint16_t count,
                                           std::vector<uint16_t> &out_registers) {
  auto res = this->read_input_registers_result(start_address, count);
  out_registers = std::move(res.registers);
  return res.ok;
}

ModbusReadResult CustomModbusTcp::read_holding_registers_result(uint16_t start_address, uint16_t count) {
  return this->read_registers_(0x03, start_address, count);
}

ModbusReadResult CustomModbusTcp::read_input_registers_result(uint16_t start_address, uint16_t count) {
  return this->read_registers_(0x04, start_address, count);
}

}  // namespace custom_modbus_tcp
}  // namespace esphome
