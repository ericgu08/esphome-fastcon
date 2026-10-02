#include <algorithm>
#include <cstring>

#include "esphome/core/component_iterator.h"
#include "esphome/core/log.h"
#include "esphome/components/light/color_mode.h"
#include "esphome/components/light/light_state.h"

#include "fastcon_controller.h"
#include "protocol.h"

#ifndef FASTCON_VERSION
#define FASTCON_VERSION "0.3.2-dev"
#endif

namespace esphome {
namespace fastcon {

static const char *const TAG = "fastcon.controller";

static uint8_t calculate_effect_color_count(
    const std::array<uint8_t, 6> &sequence) {

  // BRmesh reserves two states:
  //
  //   current_color -> sequence[0]
  //
  // Therefore the minimum is always 2 states.
  //
  // For a one-color effect:
  //
  //   current_color -> OFF
  //
  // sequence[0] == 0 is therefore meaningful and must NOT
  // be treated as an unused slot.

  uint8_t color_count = 2;

  for (size_t i = 1; i < sequence.size(); i++) {
    if (sequence[i] != 0) {
      color_count++;
    }
  }

  return color_count;
}


static uint8_t calculate_effect_header(
    const std::array<uint8_t, 6> &sequence) {

  const uint8_t color_count =
      calculate_effect_color_count(sequence);

  // Confirmed BRmesh values:
  //
  // 2 states -> 0x48
  // 3 states -> 0x58
  // 4 states -> 0x68
  // 5 states -> 0x78
  // 6 states -> 0x88
  // 7 states -> 0x98
  //
  // The protocol reserves 0x48 as the minimum.

  return static_cast<uint8_t>(
      0x48 + ((color_count - 2) * 0x10));
}

// ============================================================================
// Queue
// ============================================================================

void FastconController::queueCommand(
    uint32_t light_id_,
    const std::vector<uint8_t> &data) {

  std::lock_guard<std::mutex> lock(queue_mutex_);

  if (queue_.size() >= max_queue_size_) {
    ESP_LOGW(
        TAG,
        "Command queue full (size=%d), dropping command for light %d",
        (int) queue_.size(),
        (int) light_id_);

    return;
  }

  Command cmd;
  cmd.data = data;
  cmd.timestamp = millis();
  cmd.retries = 0;

  queue_.push(cmd);

  ESP_LOGI(
      TAG,
      "QUEUE t=%lu ms light=%u size=%u",
      (unsigned long) millis(),
      (unsigned) light_id_,
      (unsigned) data.size());

  ESP_LOGV(
      TAG,
      "Command queued, queue size: %d",
      (int) queue_.size());
}


void FastconController::clear_queue() {
  std::lock_guard<std::mutex> lock(queue_mutex_);

  std::queue<Command> empty;
  std::swap(queue_, empty);
}


// ============================================================================
// Effect speed
// ============================================================================

void FastconController::set_effect_speed(
    uint32_t light_id,
    uint8_t speed) {

  if (light_id > 255) {
    ESP_LOGW(
        TAG,
        "Invalid light ID %lu",
        (unsigned long) light_id);

    return;
  }

  if (speed < 1)
    speed = 1;

  if (speed > 100)
    speed = 100;

  effect_speeds_[light_id] = speed;

  ESP_LOGI(
      TAG,
      "Effect speed: light=%lu speed=%u",
      (unsigned long) light_id,
      (unsigned) speed);
}
void FastconController::set_initial_effect_speed(
    uint32_t light_id,
    uint8_t speed) {

  if (light_id > 255) {
    ESP_LOGW(
        TAG,
        "Invalid initial light ID %lu",
        (unsigned long) light_id);
    return;
  }

  if (speed < 1)
    speed = 1;

  if (speed > 100)
    speed = 100;

  initial_effect_speeds_[light_id] = speed;
  initial_effect_speed_set_[light_id] = true;

  ESP_LOGI(
      TAG,
      "Initial effect speed: light=%lu speed=%u",
      (unsigned long) light_id,
      (unsigned) speed);
}
uint8_t FastconController::get_effect_speed(
    uint32_t light_id) const {

  if (light_id > 255)
    return 40;

  const uint8_t speed = effect_speeds_[light_id];

  if (speed < 1 || speed > 100)
    return 40;

  return speed;
}


// ============================================================================
// Setup
// ============================================================================

void FastconController::setup() {
  // Default speed for all lights.
  effect_speeds_.fill(40);
  // Apply YAML-defined initial speeds after the defaults.
  for (size_t light_id = 0;
       light_id < initial_effect_speed_set_.size();
       light_id++) {

    if (initial_effect_speed_set_[light_id]) {
      effect_speeds_[light_id] =
          initial_effect_speeds_[light_id];

      ESP_LOGCONFIG(
          TAG,
          "  Initial effect speed: light=%u speed=%u",
          (unsigned) light_id,
          (unsigned) effect_speeds_[light_id]);
    }
  }
  ESP_LOGCONFIG(
      TAG,
      "Setting up Fastcon BLE Controller...");

  ESP_LOGCONFIG(
      TAG,
      "  FastCon version: %s",
      FASTCON_VERSION);

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement interval: %d-%d",
      adv_interval_min_,
      adv_interval_max_);

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement duration: %dms",
      adv_duration_);

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement gap: %dms",
      adv_gap_);

  ESP_LOGCONFIG(
      TAG,
      "  Default effect speed: %u",
      (unsigned) get_effect_speed(1));
}


// ============================================================================
// BLE advertising loop
// ============================================================================

void FastconController::loop() {
  const uint32_t now = millis();

  switch (adv_state_) {

    // ------------------------------------------------------------------------
    // IDLE
    // ------------------------------------------------------------------------

    case AdvertiseState::IDLE: {
      std::lock_guard<std::mutex> lock(queue_mutex_);

      if (queue_.empty())
        return;

      Command cmd = queue_.front();
      queue_.pop();

      ESP_LOGI(
          TAG,
          "DEQUEUE t=%lu ms queue=%u",
          (unsigned long) millis(),
          (unsigned) queue_.size());

      esp_ble_adv_params_t adv_params = {
          .adv_int_min = adv_interval_min_,
          .adv_int_max = adv_interval_max_,
          .adv_type = ADV_TYPE_NONCONN_IND,
          .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
          .peer_addr = {
              0x00,
              0x00,
              0x00,
              0x00,
              0x00,
              0x00
          },
          .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
          .channel_map = ADV_CHNL_ALL,
          .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
      };

      uint8_t adv_data_raw[31] = {0};
      uint8_t adv_data_len = 0;

      // ----------------------------------------------------------------------
      // Flags
      // ----------------------------------------------------------------------

      adv_data_raw[adv_data_len++] = 2;
      adv_data_raw[adv_data_len++] = ESP_BLE_AD_TYPE_FLAG;
      adv_data_raw[adv_data_len++] =
          ESP_BLE_ADV_FLAG_BREDR_NOT_SPT |
          ESP_BLE_ADV_FLAG_GEN_DISC;

      // ----------------------------------------------------------------------
      // Manufacturer data
      //
      // AD structure:
      //
      //   1 byte  AD type
      //   2 bytes company ID
      //   N bytes RF payload
      //
      // Therefore AD length = RF size + 3.
      // ----------------------------------------------------------------------

      const uint8_t manufacturer_ad_length =
          static_cast<uint8_t>(cmd.data.size() + 3);

      adv_data_raw[adv_data_len++] = manufacturer_ad_length;

      adv_data_raw[adv_data_len++] =
          ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE;

      adv_data_raw[adv_data_len++] =
          MANUFACTURER_DATA_ID & 0xFF;

      adv_data_raw[adv_data_len++] =
          (MANUFACTURER_DATA_ID >> 8) & 0xFF;

      memcpy(
          &adv_data_raw[adv_data_len],
          cmd.data.data(),
          cmd.data.size());

      adv_data_len += cmd.data.size();

      ESP_LOGI(
          TAG,
          "ADV_CONFIG t=%lu ms size=%u manufacturer_len=%u rf=%u",
          (unsigned long) millis(),
          (unsigned) adv_data_len,
          (unsigned) manufacturer_ad_length,
          (unsigned) cmd.data.size());

      esp_err_t err =
          esp_ble_gap_config_adv_data_raw(
              adv_data_raw,
              adv_data_len);

      if (err != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Error setting raw advertisement data "
            "(err=%d): %s",
            err,
            esp_err_to_name(err));

        return;
      }

      ESP_LOGI(
          TAG,
          "ADV_START t=%lu ms",
          (unsigned long) millis());

      err = esp_ble_gap_start_advertising(&adv_params);

      if (err != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Error starting advertisement "
            "(err=%d): %s",
            err,
            esp_err_to_name(err));

        return;
      }

      adv_state_ = AdvertiseState::ADVERTISING;
      state_start_time_ = now;

      ESP_LOGV(
          TAG,
          "Started advertising");

      break;
    }

    // ------------------------------------------------------------------------
    // ADVERTISING
    // ------------------------------------------------------------------------

    case AdvertiseState::ADVERTISING: {
      if (now - state_start_time_ >= adv_duration_) {

        esp_ble_gap_stop_advertising();

        adv_state_ = AdvertiseState::GAP;
        state_start_time_ = now;

        ESP_LOGV(
            TAG,
            "Stopped advertising, entering gap period");
      }

      break;
    }

    // ------------------------------------------------------------------------
    // GAP
    // ------------------------------------------------------------------------

    case AdvertiseState::GAP: {
      if (now - state_start_time_ >= adv_gap_) {

        adv_state_ = AdvertiseState::IDLE;
        state_start_time_ = now;

        ESP_LOGV(
            TAG,
            "Gap period complete");
      }

      break;
    }
  }
}


// ============================================================================
// Helpers
// ============================================================================

static inline uint8_t to8(float v) {
  if (v < 0.0f)
    v = 0.0f;

  if (v > 1.0f)
    v = 1.0f;

  return static_cast<uint8_t>(
      v * 255.0f + 0.5f);
}


static inline bool all_zero(
    float r,
    float g,
    float b,
    float cw,
    float ww) {

  return r == 0.0f &&
         g == 0.0f &&
         b == 0.0f &&
         cw == 0.0f &&
         ww == 0.0f;
}


// ============================================================================
// Light data
// ============================================================================

std::vector<uint8_t> FastconController::get_light_data(
    light::LightState *state) {

  if (!state->current_values.is_on())
    return {0x00};

  float red;
  float green;
  float blue;
  float cold_white;
  float warm_white;

  state->current_values_as_rgbww(
      &red,
      &green,
      &blue,
      &cold_white,
      &warm_white,
      true);

  // Fastcon light protocol is handled as RGB here.
  // If the RGB channels are all zero while the light is on,
  // use full white as the fallback.
  if (all_zero(
          red,
          green,
          blue,
          cold_white,
          warm_white)) {

    red = 1.0f;
    green = 1.0f;
    blue = 1.0f;
  }

  const uint8_t brightness =
      static_cast<uint8_t>(
          0x80 |
          std::min(
              static_cast<int>(
                  state->current_values.get_brightness() *
                  127.0f),
              127));

  return {
      brightness,
      to8(blue),
      to8(red),
      to8(green),
      to8(warm_white),
      to8(cold_white),
  };
}

std::vector<uint8_t> FastconController::get_white_light_data(
    light::LightState *state) {

  if (!state->current_values.is_on())
    return {0x00};

  const uint8_t brightness =
      static_cast<uint8_t>(
          0x80 |
          std::min(
              static_cast<int>(
                  state->current_values.get_brightness() *
                  127.0f),
              127));

  return {
      brightness,
      0,
      0,
      0,
      127,
      127,
  };
}


// ============================================================================
// Single light control
// ============================================================================

std::vector<uint8_t> FastconController::single_control(
    uint32_t addr,
    const std::vector<uint8_t> &light_data) {

  std::vector<uint8_t> result_data(12, 0);

  result_data[0] =
      2 |
      (((0x0FFFFFF & (light_data.size() + 1)) << 4));

  result_data[1] =
      static_cast<uint8_t>(addr & 0xFF);

  std::copy(
      light_data.begin(),
      light_data.end(),
      result_data.begin() + 2);

  return generate_command(
      5,
      addr,
      result_data,
      true);
}


// ============================================================================
// Effect control
// ============================================================================

std::vector<uint8_t> FastconController::effect_control(
    uint32_t light_id,
    const std::vector<uint8_t> &effect_data) {

  if (effect_data.size() != 12) {
    ESP_LOGW(
        TAG,
        "Invalid effect data size: %u",
        (unsigned) effect_data.size());

    return {};
  }

  // vector_to_hex_string() expects a non-const vector
  // in the current ESPHome utility implementation.
  std::vector<uint8_t> log_data = effect_data;

  const auto hex_vec =
      vector_to_hex_string(log_data);

  ESP_LOGD(
      TAG,
      "Effect light=%lu data=%s",
      (unsigned long) light_id,
      hex_vec.data());

  return generate_command(
      5,
      light_id,
      effect_data,
      true);
}


// ============================================================================
// Command generation
// ============================================================================

std::vector<uint8_t> FastconController::generate_command(
    uint8_t n,
    uint32_t light_id_,
    const std::vector<uint8_t> &data,
    bool forward) {

  static uint8_t sequence = 0;

  std::vector<uint8_t> body(
      data.size() + 4);

  const uint8_t i2 =
      static_cast<uint8_t>(
          (light_id_ >> 8) & 0x0F);

  body[0] =
      i2 |
      ((n & 0x07) << 4) |
      (forward ? 0x80 : 0);

  body[1] = sequence++;

  if (sequence >= 255)
    sequence = 1;

  body[2] = mesh_key_[3];

  std::copy(
      data.begin(),
      data.end(),
      body.begin() + 4);

  uint8_t checksum = 0;

  for (size_t i = 0; i < body.size(); i++) {
    if (i != 3)
      checksum += body[i];
  }

  body[3] = checksum;

  for (size_t i = 0; i < 4; i++) {
    body[i] =
        DEFAULT_ENCRYPT_KEY[i & 3] ^
        body[i];
  }

  for (size_t i = 0; i < data.size(); i++) {
    body[4 + i] =
        mesh_key_[i & 3] ^
        body[4 + i];
  }

  std::vector<uint8_t> addr = {
      DEFAULT_BLE_FASTCON_ADDRESS.begin(),
      DEFAULT_BLE_FASTCON_ADDRESS.end()
  };

  return prepare_payload(
      addr,
      body);
}


// ============================================================================
// Speed number
// ============================================================================

void FastconSpeedNumber::control(float value) {
  uint8_t speed =
      static_cast<uint8_t>(
          value + 0.5f);

  if (speed < 1)
    speed = 1;

  if (speed > 100)
    speed = 100;

  controller_->set_effect_speed(
      light_id_,
      speed);

  publish_state(speed);
}

// ============================================================================
// Simple Color Fade
// ============================================================================

void SimpleColorFadeAction::play() {
  uint8_t speed = speed_;

  if (speed == 0) {
    speed =
        controller_->get_effect_speed(
            light_id_);
  }

  std::vector<uint8_t> effect_data = {
      0x48,
      static_cast<uint8_t>(
          light_id_ & 0xFF),
      speed,
      static_cast<uint8_t>(
          0x40 | color_),
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
  };

controller_->queueCommand(
    light_id_,
    controller_->effect_control(
        light_id_,
        effect_data));

}
// ============================================================================
// Full Color Fade
// ============================================================================

void FullColorFadeAction::play() {
  uint8_t speed = speed_;

  if (speed == 0) {
    speed =
        controller_->get_effect_speed(
            light_id_);
  }

  const uint8_t effect_mode =
      calculate_effect_header(sequence_);

  std::vector<uint8_t> effect_data = {
      effect_mode,
      static_cast<uint8_t>(
          light_id_ & 0xFF),
      speed,
      static_cast<uint8_t>(
          0x40 | current_color_),
      sequence_[0],
      sequence_[1],
      sequence_[2],
      sequence_[3],
      sequence_[4],
      sequence_[5],
      0,
      0,
  };

controller_->queueCommand(
    light_id_,
    controller_->effect_control(
        light_id_,
        effect_data));
}


// ============================================================================
// Full Color Flash
// ============================================================================

void FullColorFlashAction::play() {
  uint8_t speed = speed_;

  if (speed == 0) {
    speed =
        controller_->get_effect_speed(
            light_id_);
  }

  // Confirmed BRmesh mapping:
  //
  // speed 1   -> C8
  // speed 20  -> A2
  // speed 40  -> 7A
  // speed 90  -> 16
  // speed 100 -> 02
  //
  const uint8_t protocol_speed =
      static_cast<uint8_t>(
          0xCA - (2 * speed));

  const uint8_t effect_mode =
      calculate_effect_header(sequence_);

  std::vector<uint8_t> effect_data = {
      effect_mode,
      static_cast<uint8_t>(
          light_id_ & 0xFF),
      protocol_speed,
      static_cast<uint8_t>(
          0xC0 | current_color_),
      sequence_[0],
      sequence_[1],
      sequence_[2],
      sequence_[3],
      sequence_[4],
      sequence_[5],
      0,
      0,
  };

controller_->queueCommand(
    light_id_,
    controller_->effect_control(
        light_id_,
        effect_data));
}

}  // namespace fastcon
}  // namespace esphome
