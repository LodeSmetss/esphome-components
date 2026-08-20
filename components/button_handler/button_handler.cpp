#include "button_handler.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace esphome {
namespace button_handler {

static const char *const TAG = "button_handler";

static std::string join_labels(const std::vector<std::string> &parts) {
  std::ostringstream oss;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i > 0) {
      oss << " ";
    }
    oss << parts[i];
  }
  return oss.str();
}

static std::string event_type_to_string(ButtonEventType event_type) {
  switch (event_type) {
    case ButtonEventType::SINGLE:
      return "single";
    case ButtonEventType::DOUBLE:
      return "double";
    case ButtonEventType::LONG_PRESS:
      return "long_press";
    case ButtonEventType::DOUBLE_LONG_PRESS:
      return "double_long_press";
    case ButtonEventType::LONG_RELEASE:
      return "long_release";
    case ButtonEventType::COMBO:
      return "combo";
    case ButtonEventType::UNKNOWN:
    default:
      return "unknown";
  }
}

std::string ButtonHandlerButton::resolve_combo_label(const std::vector<uint16_t> &pressed_addresses) const {
  if (pressed_addresses.empty()) {
    return "";
  }

  std::vector<std::string> labels;
  labels.reserve(pressed_addresses.size());
  for (const auto addr : pressed_addresses) {
    const auto it = this->labels_.find(addr);
    if (it != this->labels_.end()) {
      labels.push_back(it->second);
    }
  }

  if (!labels.empty()) {
    return join_labels(labels);
  }

  std::vector<uint16_t> sorted = pressed_addresses;
  std::sort(sorted.begin(), sorted.end());
  std::ostringstream oss;
  for (size_t i = 0; i < sorted.size(); i++) {
    if (i > 0) {
      oss << ",";
    }
    oss << sorted[i];
  }
  return oss.str();
}

void ButtonHandlerButton::fire_event(ButtonEventType event_type, const std::string &button_id,
                                      const std::string &label) {
  switch (event_type) {
    case ButtonEventType::SINGLE:
      this->single_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::DOUBLE:
      this->double_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::LONG_PRESS:
      this->long_press_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::DOUBLE_LONG_PRESS:
      this->double_long_press_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::LONG_RELEASE:
      this->long_release_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::COMBO:
      this->combo_trigger_.trigger(button_id, label);
      break;
    case ButtonEventType::UNKNOWN:
    default:
      break;
  }

  this->event_trigger_.trigger(button_id, event_type_to_string(event_type), label);
}

void ButtonHandler::fire_event_(const std::string &button_id,
                                ButtonEventType event_type,
                                const std::string &label) {
  this->event_trigger_.trigger(
      button_id,
      event_type_to_string(event_type),
      label);
}

void ButtonHandler::setup() {
  ESP_LOGI(TAG, "Setting up ButtonHandler");
  this->last_poll_ms_ = millis();
}

void ButtonHandler::dump_config() {
  ESP_LOGCONFIG(TAG, "ButtonHandler:");
  ESP_LOGCONFIG(TAG, "  Register type: %s", this->register_type_.c_str());
  ESP_LOGCONFIG(TAG, "  Register start: %u", this->register_start_);
  ESP_LOGCONFIG(TAG, "  Register count: %u", this->register_count_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", this->poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Timings (debounce/long/collect/cooldown): %u/%u/%u/%u ms", this->debounce_ms_,
                this->long_press_ms_, this->collect_window_ms_, this->invalid_cooldown_ms_);
  ESP_LOGCONFIG(TAG, "  Button groups: %u", static_cast<unsigned>(this->buttons_.size()));
}

bool ButtonHandler::should_poll_(uint32_t now_ms) const { return (now_ms - this->last_poll_ms_) >= this->poll_interval_ms_; }

bool ButtonHandler::read_word_registers_(std::vector<uint16_t> &registers) {
  if (this->modbus_tcp_ == nullptr) {
    return false;
  }

  if (this->register_type_ == "input") {
    return this->modbus_tcp_->read_input_registers(
        this->register_start_,
        this->register_count_,
        registers);
  }

  return this->modbus_tcp_->read_holding_registers(
      this->register_start_,
      this->register_count_,
      registers);
}

bool ButtonHandler::read_discrete_(std::vector<bool> &bits) {
  if (this->modbus_tcp_ == nullptr) {
    return false;
  }

  return this->modbus_tcp_->read_discrete_inputs(
      this->register_start_,
      this->register_count_,
      bits);
}

void ButtonHandler::update_input_states_(const std::vector<bool> &io_list, uint32_t now_ms) {
  for (size_t i = 0; i < io_list.size(); i++) {
    const uint16_t address = static_cast<uint16_t>(i + 1);
    auto &st = this->io_states_[address];

    st.released = false;
    st.pressed = false;

    const bool new_value = io_list[i];
    if (new_value != st.value) {
      if ((now_ms - st.flank_time) < this->debounce_ms_) {
        continue;
      }

      st.released = !new_value;
      st.pressed = new_value;
      st.value = new_value;
      st.state += 1;
      st.flank_time = now_ms;
      if ((st.state % 2) == 1) {
        st.valid_state = false;
      }
    }

    if ((st.state % 2) == 0 && st.valid_state) {
      st.state = 0;
      st.valid_state = false;
    }

    if ((now_ms - st.flank_time) > this->long_press_ms_) {
      if (!st.long_detected && st.state != 0) {
        st.valid_state = true;
        if ((st.state % 2) != 0) {
          st.long_detected = true;
        }
      } else if (st.state != 0) {
        st.valid_state = false;
      }
    }

    if (st.long_detected && (st.state % 2) == 0) {
      st.valid_state = true;
      st.long_detected = false;
      st.state = 0;
    }
  }
}

ButtonEventType ButtonHandler::map_state_to_event_(int state) const {
  switch (state) {
    case 0:
      return ButtonEventType::LONG_RELEASE;
    case 1:
      return ButtonEventType::LONG_PRESS;
    case 2:
      return ButtonEventType::SINGLE;
    case 3:
      return ButtonEventType::DOUBLE_LONG_PRESS;
    case 4:
      return ButtonEventType::DOUBLE;
    default:
      return ButtonEventType::COMBO;
  }
}

void ButtonHandler::process_button_groups_(uint32_t now_ms) {
  for (auto *button : this->buttons_) {
    if (button == nullptr) {
      continue;
    }

    std::unordered_map<uint16_t, InputState> valid_states;
    for (const auto address : button->addresses()) {
      const auto st_it = this->io_states_.find(address);
      if (st_it == this->io_states_.end()) {
        continue;
      }
      if (st_it->second.valid_state) {
        valid_states[address] = st_it->second;
      }
    }

    if (valid_states.empty() && button->valid_states.empty()) {
      continue;
    }

    if (!valid_states.empty()) {
      for (const auto &kv : valid_states) {
        button->valid_states[kv.first] = kv.second;
      }

      if (!button->timing_active) {
        button->timing_active = true;
        button->timing_start_ms = now_ms;
      }
    }

    if (!button->timing_active) {
      continue;
    }

    if ((now_ms - button->timing_start_ms) < this->collect_window_ms_) {
      continue;
    }

    std::map<int, std::vector<uint16_t>> state_to_addresses;
    for (const auto &kv : button->valid_states) {
      state_to_addresses[kv.second.state].push_back(kv.first);
    }

    button->valid_states.clear();
    button->timing_active = false;

    for (const auto &kv : state_to_addresses) {
      const int state = kv.first;
      auto addresses = kv.second;
      std::sort(addresses.begin(), addresses.end());

      auto event_type = this->map_state_to_event_(state);
      const auto combo = button->resolve_combo_label(addresses);

      ESP_LOGI(TAG, "Button '%s' event state=%d combo=%s", button->button_id().c_str(), state, combo.c_str());
      button->fire_event(event_type, button->button_id(), combo);
      
      //global trigger
      this->fire_event_(button->button_id(), event_type, combo);
    }
  }
}

void ButtonHandler::loop() {
  const uint32_t now = millis();
  if (!this->should_poll_(now)) {
    return;
  }
  this->last_poll_ms_ = now;

  if (this->modbus_tcp_ == nullptr) {
    ESP_LOGW(TAG, "No modbus_tcp_id configured");
    return;
  }

  std::vector<bool> io_list;
  bool ok;

  if (this->register_type_ == "discrete") {
    // Discrete inputs already come back as one bool per address; no unpacking needed.
    ok = this->read_discrete_(io_list);
  } else {
    // Holding/input registers pack 16 digital I/O points per 16-bit word.
    std::vector<uint16_t> registers;
    ok = this->read_word_registers_(registers);
    if (ok) {
      io_list.reserve(registers.size() * 16);
      for (const auto reg : registers) {
        for (uint8_t bit = 0; bit < 16; bit++) {
          io_list.push_back(((reg >> bit) & 0x1U) != 0U);
        }
      }
    }
  }

  if (!ok) {
    const bool in_cooldown = (now - this->last_invalid_ms_) < this->invalid_cooldown_ms_;
    if (!in_cooldown) {
      ESP_LOGW(TAG, "Register read failed");
      this->last_invalid_ms_ = now;
    }
    return;
  }

  this->update_input_states_(io_list, now);
  this->process_button_groups_(now);
}

}  // namespace button_handler
}  // namespace esphome