#pragma once

#include <array>
#include <queue>
#include <mutex>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/components/number/number.h"

#include "esphome/components/light/light_state.h"

namespace esphome {
namespace fastcon {

class FastconController : public Component {
 public:
  FastconController() = default;

  void setup() override;
  void loop() override;

  std::vector<uint8_t> get_light_data(light::LightState *state);
  std::vector<uint8_t> get_white_light_data(light::LightState *state);

  std::vector<uint8_t> single_control(
      uint32_t addr,
      const std::vector<uint8_t> &light_data);

  std::vector<uint8_t> effect_control(
      uint32_t light_id,
      const std::vector<uint8_t> &effect_data);

  void queueCommand(
      uint32_t light_id_,
      const std::vector<uint8_t> &data);

  void clear_queue();

  bool is_queue_empty() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.empty();
  }

  size_t get_queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
  }

  void set_max_queue_size(size_t size) {
    max_queue_size_ = size;
  }

  void set_mesh_key(std::array<uint8_t, 4> key) {
    mesh_key_ = key;
  }

  void set_adv_interval_min(uint16_t val) {
    adv_interval_min_ = val;
  }

  void set_adv_interval_max(uint16_t val) {
    adv_interval_max_ = val;

    if (adv_interval_max_ < adv_interval_min_) {
      adv_interval_max_ = adv_interval_min_;
    }
  }

  void set_adv_duration(uint16_t val) {
    adv_duration_ = val;
  }

  void set_adv_gap(uint16_t val) {
    adv_gap_ = val;
  }

  void set_effect_speed(uint32_t light_id, uint8_t speed);

  uint8_t get_effect_speed(uint32_t light_id) const;

 protected:
  struct Command {
    std::vector<uint8_t> data;
    uint32_t timestamp;
    uint8_t retries{0};

    static constexpr uint8_t MAX_RETRIES = 3;
  };

  std::queue<Command> queue_;
  mutable std::mutex queue_mutex_;

  size_t max_queue_size_{100};

  enum class AdvertiseState {
    IDLE,
    ADVERTISING,
    GAP
  };

  AdvertiseState adv_state_{AdvertiseState::IDLE};

  uint32_t state_start_time_{0};

  std::vector<uint8_t> generate_command(
      uint8_t n,
      uint32_t light_id_,
      const std::vector<uint8_t> &data,
      bool forward = true);

  std::array<uint8_t, 4> mesh_key_{};

  uint16_t adv_interval_min_{0x20};
  uint16_t adv_interval_max_{0x40};

  uint16_t adv_duration_{50};
  uint16_t adv_gap_{10};

  std::array<uint8_t, 256> effect_speeds_{};

  static const uint16_t MANUFACTURER_DATA_ID = 0xfff0;
};


// ============================================================================
// Speed number
// ============================================================================

class FastconSpeedNumber : public number::Number {
 public:
  void set_controller(FastconController *controller) {
    controller_ = controller;
  }

  void set_light_id(uint32_t light_id) {
    light_id_ = light_id;
  }

  void set_initial_state(float initial_state) {
    initial_state_ = initial_state;
  }

 protected:
  void setup() override;

  void control(float value) override;

  FastconController *controller_{nullptr};
  uint32_t light_id_{0};
  float initial_state_{40.0f};
};


// ============================================================================
// Simple Color Fade
// ============================================================================

class SimpleColorFadeAction : public esphome::Action<> {
 public:
  explicit SimpleColorFadeAction(FastconController *controller)
      : controller_(controller) {}

  void set_light_id(uint32_t light_id) {
    light_id_ = light_id;
  }

  void set_color(uint8_t color) {
    color_ = color;
  }

  void set_speed(uint8_t speed) {
    speed_ = speed;
  }

 protected:
  void play() override;

  FastconController *controller_;
  uint32_t light_id_{0};
  uint8_t color_{1};
  uint8_t speed_{0};
};


// ============================================================================
// Full Color Fade
// ============================================================================

class FullColorFadeAction : public esphome::Action<> {
 public:
  explicit FullColorFadeAction(FastconController *controller)
      : controller_(controller) {}

  void set_light_id(uint32_t light_id) {
    light_id_ = light_id;
  }

  void set_current_color(uint8_t color) {
    current_color_ = color;
  }

  void set_sequence_0(uint8_t value) {
    sequence_[0] = value;
  }

  void set_sequence_1(uint8_t value) {
    sequence_[1] = value;
  }

  void set_sequence_2(uint8_t value) {
    sequence_[2] = value;
  }

  void set_sequence_3(uint8_t value) {
    sequence_[3] = value;
  }

  void set_sequence_4(uint8_t value) {
    sequence_[4] = value;
  }

  void set_sequence_5(uint8_t value) {
    sequence_[5] = value;
  }

  void set_speed(uint8_t speed) {
    speed_ = speed;
  }

 protected:
  void play() override;

  FastconController *controller_;
  uint32_t light_id_{0};

  uint8_t current_color_{1};

  std::array<uint8_t, 6> sequence_{
      {0, 0, 0, 0, 0, 0}};

  uint8_t speed_{0};
};


// ============================================================================
// Full Color Flash
// ============================================================================

class FullColorFlashAction : public esphome::Action<> {
 public:
  explicit FullColorFlashAction(FastconController *controller)
      : controller_(controller) {}

  void set_light_id(uint32_t light_id) {
    light_id_ = light_id;
  }

  void set_current_color(uint8_t color) {
    current_color_ = color;
  }

  void set_sequence_0(uint8_t value) {
    sequence_[0] = value;
  }

  void set_sequence_1(uint8_t value) {
    sequence_[1] = value;
  }

  void set_sequence_2(uint8_t value) {
    sequence_[2] = value;
  }

  void set_sequence_3(uint8_t value) {
    sequence_[3] = value;
  }

  void set_sequence_4(uint8_t value) {
    sequence_[4] = value;
  }

  void set_sequence_5(uint8_t value) {
    sequence_[5] = value;
  }

  void set_speed(uint8_t speed) {
    speed_ = speed;
  }

 protected:
  void play() override;

  FastconController *controller_;

  uint32_t light_id_{0};

  uint8_t current_color_{1};

  std::array<uint8_t, 6> sequence_{
      {0, 0, 0, 0, 0, 0}};

  uint8_t speed_{0};
};

}  // namespace fastcon
}  // namespace esphome
