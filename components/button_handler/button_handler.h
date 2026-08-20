#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "../custom_modbus_tcp/custom_modbus_tcp.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace esphome {
namespace button_handler {

using custom_modbus_tcp::CustomModbusTcp;

class SingleTrigger : public Trigger<std::string, std::string> {};
class DoubleTrigger : public Trigger<std::string, std::string> {};
class LongPressTrigger : public Trigger<std::string, std::string> {};
class LongReleaseTrigger : public Trigger<std::string, std::string> {};
class ComboTrigger : public Trigger<std::string, std::string> {};
class EventTrigger : public Trigger<std::string, std::string, std::string> {};

enum class ButtonEventType {
  SINGLE,
  DOUBLE,
  LONG_PRESS,
  DOUBLE_LONG_PRESS,
  LONG_RELEASE,
  COMBO,
  UNKNOWN,
};

struct InputState {
  bool value{false};
  int state{0};
  uint32_t flank_time{0};
  bool valid_state{false};
  bool pressed{false};
  bool released{false};
  bool long_detected{false};
};

class ButtonHandlerButton {
 public:
  void set_button_id(const std::string &button_id) { this->button_id_ = button_id; }
  void set_addresses(const std::vector<uint16_t> &addresses) { this->addresses_ = addresses; }
  void add_label(uint16_t address, const std::string &label) { this->labels_[address] = label; }

  const std::string &button_id() const { return this->button_id_; }
  const std::vector<uint16_t> &addresses() const { return this->addresses_; }

  SingleTrigger *get_single_trigger() { return &this->single_trigger_; }
  DoubleTrigger *get_double_trigger() { return &this->double_trigger_; }
  LongPressTrigger *get_long_press_trigger() { return &this->long_press_trigger_; }
  DoubleLongPressTrigger *get_double_long_press_trigger() { return &this->double_long_press_trigger_; }
  LongReleaseTrigger *get_long_release_trigger() { return &this->long_release_trigger_; }
  ComboTrigger *get_combo_trigger() { return &this->combo_trigger_; }
  EventTrigger *get_event_trigger() { return &this->event_trigger_; }

  std::string resolve_combo_label(const std::vector<uint16_t> &pressed_addresses) const;

  std::unordered_map<uint16_t, InputState> valid_states{};
  bool timing_active{false};
  uint32_t timing_start_ms{0};

  void fire_event(ButtonEventType event_type, const std::string &button_id, const std::string &label);

 protected:
  std::string button_id_;
  std::vector<uint16_t> addresses_;
  std::map<uint16_t, std::string> labels_;

  SingleTrigger single_trigger_;
  DoubleTrigger double_trigger_;
  LongPressTrigger long_press_trigger_;
  DoubleLongPressTrigger double_long_press_trigger_;
  LongReleaseTrigger long_release_trigger_;
  ComboTrigger combo_trigger_;
  EventTrigger event_trigger_;
};

class ButtonHandler : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_modbus_tcp(CustomModbusTcp *modbus_tcp) { this->modbus_tcp_ = modbus_tcp; }
  void set_register_type(const std::string &register_type) { this->register_type_ = register_type; }
  void set_register_start(uint16_t register_start) { this->register_start_ = register_start; }
  void set_register_count(uint16_t register_count) { this->register_count_ = register_count; }
  void set_poll_interval_ms(uint32_t poll_interval_ms) { this->poll_interval_ms_ = poll_interval_ms; }

  void set_debounce_ms(uint32_t debounce_ms) { this->debounce_ms_ = debounce_ms; }
  void set_long_press_ms(uint32_t long_press_ms) { this->long_press_ms_ = long_press_ms; }
  void set_collect_window_ms(uint32_t collect_window_ms) { this->collect_window_ms_ = collect_window_ms; }
  void set_invalid_cooldown_ms(uint32_t invalid_cooldown_ms) { this->invalid_cooldown_ms_ = invalid_cooldown_ms; }

  void add_button(ButtonHandlerButton *button) { this->buttons_.push_back(button); }
  EventTrigger *get_event_trigger() { return &this->event_trigger_; }

 protected:
  bool should_poll_(uint32_t now_ms) const;
  bool read_word_registers_(std::vector<uint16_t> &registers);
  bool read_discrete_(std::vector<bool> &bits);
  void update_input_states_(const std::vector<bool> &io_list, uint32_t now_ms);
  void process_button_groups_(uint32_t now_ms);

  void fire_event_(const std::string &button_id, ButtonEventType event_type, const std::string &label);

  ButtonEventType map_state_to_event_(int state) const;

  CustomModbusTcp *modbus_tcp_{nullptr};
  std::string register_type_{"holding"};
  uint16_t register_start_{0};
  uint16_t register_count_{7};
  uint32_t poll_interval_ms_{10};

  uint32_t debounce_ms_{20};
  uint32_t long_press_ms_{500};
  uint32_t collect_window_ms_{100};
  uint32_t invalid_cooldown_ms_{100};

  uint32_t last_poll_ms_{0};
  uint32_t last_invalid_ms_{0};

  std::vector<ButtonHandlerButton *> buttons_;
  std::unordered_map<uint16_t, InputState> io_states_;

  EventTrigger event_trigger_;
};

}  // namespace button_handler
}  // namespace esphome