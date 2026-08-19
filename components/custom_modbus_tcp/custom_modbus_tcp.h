#pragma once

#include "esphome/core/component.h"

#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace custom_modbus_tcp {

enum class ModbusReadError {
  OK = 0,
  NOT_CONNECTED,
  CONNECT_FAILED,
  SEND_FAILED,
  RECEIVE_FAILED,
  PROTOCOL_ERROR,
  EXCEPTION_RESPONSE,
  BAD_ARGUMENT,
};

struct ModbusReadResult {
  bool ok{false};
  std::vector<uint16_t> registers{};
  ModbusReadError error_code{ModbusReadError::OK};
  uint32_t latency_ms{0};
  uint32_t timestamp_ms{0};
};

// Result type for bit-oriented reads (discrete inputs, coils). Modbus packs
// these 8 bits per byte, LSB first, so they get their own result/parse path
// rather than reusing ModbusReadResult's 16-bit register layout.
struct ModbusBitReadResult {
  bool ok{false};
  std::vector<bool> bits{};
  ModbusReadError error_code{ModbusReadError::OK};
  uint32_t latency_ms{0};
  uint32_t timestamp_ms{0};
};

class CustomModbusTcp : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_unit_id(uint8_t unit_id) { this->unit_id_ = unit_id; }
  void set_connect_timeout_ms(uint32_t timeout_ms) { this->connect_timeout_ms_ = timeout_ms; }
  void set_response_timeout_ms(uint32_t timeout_ms) { this->response_timeout_ms_ = timeout_ms; }
  void set_retry_count(uint8_t retry_count) { this->retry_count_ = retry_count; }
  void set_retry_backoff_ms(uint32_t backoff_ms) { this->retry_backoff_ms_ = backoff_ms; }
  void set_keepalive_ms(uint32_t keepalive_ms) { this->keepalive_ms_ = keepalive_ms; }

  bool is_connected() const;
  ModbusReadError last_error() const { return this->last_error_; }

  bool read_holding_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t> &out_registers);
  bool read_input_registers(uint16_t start_address, uint16_t count, std::vector<uint16_t> &out_registers);
  ModbusReadResult read_holding_registers_result(uint16_t start_address, uint16_t count);
  ModbusReadResult read_input_registers_result(uint16_t start_address, uint16_t count);

  bool read_discrete_inputs(uint16_t start_address, uint16_t count, std::vector<bool> &out_bits);
  ModbusBitReadResult read_discrete_inputs_result(uint16_t start_address, uint16_t count);

 protected:
  bool ensure_connected_();
  void disconnect_();

  ModbusReadResult read_registers_(uint8_t function_code, uint16_t start_address, uint16_t count);
  ModbusBitReadResult read_bits_(uint8_t function_code, uint16_t start_address, uint16_t count);
  bool send_all_(const uint8_t *data, size_t len);
  bool recv_all_(uint8_t *data, size_t len);

  std::string host_;
  uint16_t port_{502};
  uint8_t unit_id_{1};
  uint32_t connect_timeout_ms_{1000};
  uint32_t response_timeout_ms_{300};
  uint8_t retry_count_{3};
  uint32_t retry_backoff_ms_{150};
  uint32_t keepalive_ms_{30000};

  int socket_fd_{-1};
  uint16_t transaction_id_{1};
  uint32_t last_io_ms_{0};
  uint32_t last_connect_attempt_ms_{0};
  ModbusReadError last_error_{ModbusReadError::OK};

  uint32_t read_ok_count_{0};
  uint32_t read_fail_count_{0};
  uint32_t reconnect_count_{0};
};

}  // namespace custom_modbus_tcp
}  // namespace esphome