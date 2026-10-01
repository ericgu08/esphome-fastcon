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


// ============================================================================
// Command queue
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
      (unsigned long)millis(),
      (unsigned) light_id_,
      (unsigned) data.size());

  ESP_LOGV(
      TAG,
      "Command queued, queue size: %d",
      (int) queue_.size());
}


// ============================================================================
// Clear queue
// ============================================================================

void FastconController::clear_queue() {
  std::lock_guard<std::mutex> lock(queue_mutex_);

  std::queue<Command> empty;
  std::swap(queue_, empty);
}


// ============================================================================
// Setup
// ============================================================================

void FastconController::setup() {
  ESP_LOGCONFIG(
      TAG,
      "Setting up Fastcon BLE Controller...");

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement interval: %d-%d",
      this->adv_interval_min_,
      this->adv_interval_max_);

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement duration: %dms",
      this->adv_duration_);

  ESP_LOGCONFIG(
      TAG,
      "  Advertisement gap: %dms",
      this->adv_gap_);
}


// ============================================================================
// BLE advertising state machine
// ============================================================================

void FastconController::loop() {

  const uint32_t now = millis();

  switch (adv_state_) {

    // ------------------------------------------------------------------------
    // IDLE
    // ------------------------------------------------------------------------

    case AdvertiseState::IDLE: {

      std::lock_guard<std::mutex> lock(queue_mutex_);

      if (queue_.empty()) {
        return;
      }

      Command cmd = queue_.front();
      queue_.pop();

      ESP_LOGI(
          TAG,
          "DEQUEUE t=%lu ms queue=%u",
          (unsigned long)millis(),
          (unsigned) queue_.size());


      // ----------------------------------------------------------------------
      // Validate RF payload size
      //
      // A legacy BLE advertisement is limited to 31 bytes.
      //
      // Flags:
      //   3 bytes
      //
      // Manufacturer AD:
      //   1 byte  length
      //   1 byte  type
      //   2 bytes company ID
      //   N bytes RF payload
      //
      // Therefore:
      //
      //   3 + 1 + 1 + 2 + N <= 31
      //
      // Current BRmesh payload is exactly 24 bytes.
      // ----------------------------------------------------------------------

      if (cmd.data.size() + 7 > 31) {

        ESP_LOGW(
            TAG,
            "Advertisement payload too large: RF payload=%u bytes",
            (unsigned) cmd.data.size());

        return;
      }


      // ----------------------------------------------------------------------
      // BLE advertising parameters
      // ----------------------------------------------------------------------

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
          .adv_filter_policy =
              ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
      };


      // ----------------------------------------------------------------------
      // Build raw BLE advertisement
      // ----------------------------------------------------------------------

      uint8_t adv_data_raw[31] = {0};
      uint8_t adv_data_len = 0;


      // ----------------------------------------------------------------------
      // Flags AD
      //
      // Length = 2
      // Type   = Flags
      // Data   = 0x06
      // ----------------------------------------------------------------------

      adv_data_raw[adv_data_len++] = 2;

      adv_data_raw[adv_data_len++] =
          ESP_BLE_AD_TYPE_FLAG;

      adv_data_raw[adv_data_len++] =
          ESP_BLE_ADV_FLAG_BREDR_NOT_SPT |
          ESP_BLE_ADV_FLAG_GEN_DISC;


      // ----------------------------------------------------------------------
      // Manufacturer Specific Data
      //
      // IMPORTANT:
      //
      // The BLE AD length byte counts:
      //
      //   1 byte  = AD type
      //   2 bytes = manufacturer/company ID
      //   N bytes = manufacturer data
      //
      // Therefore:
      //
      //   length = N + 3
      //
      // For our BRmesh packet:
      //
      //   N = 24
      //   length = 27 = 0x1B
      //
      // The previous code used N + 2, which declared only
      // 23 RF bytes. This explains the 23-byte packets seen
      // by the sniffer.
      // ----------------------------------------------------------------------

      const uint8_t manufacturer_ad_length =
          static_cast<uint8_t>(cmd.data.size() + 3);

      adv_data_raw[adv_data_len++] =
          manufacturer_ad_length;

      adv_data_raw[adv_data_len++] =
          ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE;

      adv_data_raw[adv_data_len++] =
          MANUFACTURER_DATA_ID & 0xFF;

      adv_data_raw[adv_data_len++] =
          (MANUFACTURER_DATA_ID >> 8) & 0xFF;


      // ----------------------------------------------------------------------
      // BRmesh RF payload
      // ----------------------------------------------------------------------

      memcpy(
          &adv_data_raw[adv_data_len],
          cmd.data.data(),
          cmd.data.size());

      adv_data_len += cmd.data.size();


      // ----------------------------------------------------------------------
      // Diagnostic log
      // ----------------------------------------------------------------------

      ESP_LOGI(
          TAG,
          "ADV_CONFIG t=%lu ms size=%u manufacturer_len=%u rf=%u",
          (unsigned long)millis(),
          (unsigned) adv_data_len,
          (unsigned) manufacturer_ad_length,
          (unsigned) cmd.data.size());


      // ----------------------------------------------------------------------
      // Configure raw advertisement
      // ----------------------------------------------------------------------

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


      // ----------------------------------------------------------------------
      // Start advertising
      // ----------------------------------------------------------------------

      ESP_LOGI(
          TAG,
          "ADV_START t=%lu ms",
          (unsigned long)millis());

      err =
          esp_ble_gap_start_advertising(
              &adv_params);

      if (err != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Error starting advertisement "
            "(err=%d): %s",
            err,
            esp_err_to_name(err));

        return;
      }


      // Use the actual time at which start was requested,
      // rather than the timestamp captured at the beginning
      // of loop().
      adv_state_ = AdvertiseState::ADVERTISING;
      state_start_time_ = millis();

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
        state_start_time_ = millis();

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

        ESP_LOGV(
            TAG,
            "Gap period complete");
      }

      break;
    }
  }
}


// ============================================================================
// Helpers for channel resolution
// ============================================================================

static inline uint8_t to8(float v) {

  if (v < 0.0f) {
    v = 0.0f;
  }

  if (v > 1.0f) {
    v = 1.0f;
  }

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
// Normal light data
// ============================================================================

std::vector<uint8_t>
FastconController::get_light_data(
    light::LightState *state) {

  // Protocol:
  //
  // [0] 0x80 | brightness 0..127
  // [1] Blue
  // [2] Red
  // [3] Green
  // [4] Warm
  // [5] Cold
  //
  // OFF:
  //
  // 0x00

  auto &values = state->current_values;

  const bool is_on = values.is_on();

  if (!is_on) {
    return std::vector<uint8_t>({0x00});
  }


  // Compute final channel levels from current values.
  float r = 0;
  float g = 0;
  float b = 0;
  float cw = 0;
  float ww = 0;

  state->current_values_as_rgbww(
      &r,
      &g,
      &b,
      &cw,
      &ww,
      false);


  // --------------------------------------------------------------------------
  // White mode on RGB-only fixture
  // --------------------------------------------------------------------------

  const auto mode =
      values.get_color_mode();

  const bool supports_cwww =
      state->get_traits().get_min_mireds() > 0.0f;

  if ((mode == light::ColorMode::WHITE ||
       mode == light::ColorMode::COLD_WARM_WHITE) &&
      !supports_cwww) {

    float m = (ww > 0 ? ww : cw);

    r = m;
    g = m;
    b = m;

    cw = 0.0f;
    ww = 0.0f;
  }


  // --------------------------------------------------------------------------
  // Fallback for UNKNOWN / zero channels
  // --------------------------------------------------------------------------

  if (all_zero(r, g, b, cw, ww)) {

    if (supports_cwww) {
      ww = 1.0f;
    } else {
      r = 1.0f;
      g = 1.0f;
      b = 1.0f;
    }
  }


  // --------------------------------------------------------------------------
  // Compose payload
  // --------------------------------------------------------------------------

  const float blevel =
      std::min(
          values.get_brightness() * 127.0f,
          127.0f);

  std::vector<uint8_t> light_data = {

      static_cast<uint8_t>(
          0x80 |
          static_cast<uint8_t>(blevel)),

      to8(b),
      to8(r),
      to8(g),
      to8(ww),
      to8(cw)
  };

  return light_data;
}


// ============================================================================
// White light data
// ============================================================================

std::vector<uint8_t>
FastconController::get_white_light_data(
    light::LightState *state) {

  auto &values = state->current_values;

  const bool is_on = values.is_on();

  if (!is_on) {
    return std::vector<uint8_t>({0x00});
  }


  const float blevel =
      std::min(
          values.get_brightness() * 127.0f,
          127.0f);


  std::vector<uint8_t> light_data = {

      static_cast<uint8_t>(
          0x80 |
          static_cast<uint8_t>(blevel)),

      0,
      0,
      0,

      127,  // Warm
      127   // Cold
  };

  return light_data;
}


// ============================================================================
// Normal single light control
// ============================================================================

std::vector<uint8_t>
FastconController::single_control(
    uint32_t light_id_,
    const std::vector<uint8_t> &light_data) {

  // Normal light command:
  //
  // byte 0 = length/type
  // byte 1 = light ID
  // bytes 2.. = light data

  std::vector<uint8_t> result_data(12, 0);


  if (light_data.size() > 10) {

    ESP_LOGW(
        TAG,
        "Light data too large for normal "
        "single_control: %d bytes",
        (int) light_data.size());

    return {};
  }


  result_data[0] =
      2 |
      (((0x0FFFFFF &
         (light_data.size() + 1))
        << 4));


  result_data[1] =
      light_id_;


  std::copy(
      light_data.begin(),
      light_data.end(),
      result_data.begin() + 2);


  const auto hex_vec =
      vector_to_hex_string(result_data);

  const std::string hex(
      hex_vec.begin(),
      hex_vec.end());


  ESP_LOGD(
      TAG,
      "Inner Payload v%s (%zu bytes): %s",
      FASTCON_VERSION,
      result_data.size(),
      hex.c_str());


  return this->generate_command(
      5,
      light_id_,
      result_data,
      true);
}


// ============================================================================
// BRmesh effect command
// ============================================================================
//
// Effect payload is already a complete 12-byte BRmesh application payload.
//
// Simple Color Fade:
//
//   48 03 19 42 00 00 00 00 00 00 00 00
//
// Full Color Fade:
//
//   98 03 28 47 05 03 06 01 04 02 00 00
//
// Full Color Flash:
//
//   98 03 66 C2 03 01 05 04 06 00 00 00
//
// The effect payload must NOT pass through the normal
// light_data length/type wrapper.
//

std::vector<uint8_t>
FastconController::effect_control(
    uint32_t light_id,
    const std::vector<uint8_t> &effect_data) {

  if (effect_data.size() != 12) {

    ESP_LOGW(
        TAG,
        "Invalid BRmesh effect payload size: %d "
        "(expected 12)",
        (int) effect_data.size());

    return {};
  }


std::vector<uint8_t> log_data = effect_data;

const auto hex_vec =
    vector_to_hex_string(log_data);

  const std::string hex(
      hex_vec.begin(),
      hex_vec.end());


  ESP_LOGD(
      TAG,
      "BRmesh Effect Payload (%zu bytes): %s",
      effect_data.size(),
      hex.c_str());


  return this->generate_command(
      5,
      light_id,
      effect_data,
      true);
}


// ============================================================================
// BRmesh command generation
// ============================================================================

std::vector<uint8_t>
FastconController::generate_command(
    uint8_t n,
    uint32_t light_id_,
    const std::vector<uint8_t> &data,
    bool forward) {

  static uint8_t sequence = 0;


  ESP_LOGD(
      TAG,
      "generate_command: n=%u light_id=%u "
      "data_size=%zu forward=%s",
      (unsigned) n,
      (unsigned) light_id_,
      data.size(),
      forward ? "YES" : "NO");


  // --------------------------------------------------------------------------
  // Create command body
  //
  // 4-byte BRmesh header
  // + application payload
  // --------------------------------------------------------------------------

  std::vector<uint8_t> body(
      data.size() + 4);


  const uint8_t i2 =
      static_cast<uint8_t>(
          (light_id_ >> 8) & 0x0F);


  // --------------------------------------------------------------------------
  // Header
  // --------------------------------------------------------------------------

  body[0] =
      i2 |
      ((n & 0x07) << 4) |
      (forward ? 0x80 : 0);


  body[1] = sequence;

  sequence++;

  if (sequence >= 255) {
    sequence = 1;
  }


  body[2] =
      this->mesh_key_[3];


  ESP_LOGD(
      TAG,
      "BRmesh header: 0x%02X "
      "sequence=%u light_id=%u",
      body[0],
      (unsigned) body[1],
      (unsigned) light_id_);


  // --------------------------------------------------------------------------
  // Copy application data
  // --------------------------------------------------------------------------

  std::copy(
      data.begin(),
      data.end(),
      body.begin() + 4);


  // --------------------------------------------------------------------------
  // Command checksum
  // --------------------------------------------------------------------------

  uint8_t checksum = 0;

  for (size_t i = 0; i < body.size(); i++) {

    if (i != 3) {
      checksum =
          checksum + body[i];
    }
  }

  body[3] = checksum;


  // --------------------------------------------------------------------------
  // Encrypt 4-byte header
  // --------------------------------------------------------------------------

  for (size_t i = 0; i < 4; i++) {

    body[i] =
        DEFAULT_ENCRYPT_KEY[i & 3] ^
        body[i];
  }


  // --------------------------------------------------------------------------
  // Encrypt application data
  // --------------------------------------------------------------------------

  for (size_t i = 0; i < data.size(); i++) {

    body[4 + i] =
        this->mesh_key_[i & 3] ^
        body[4 + i];
  }


  // --------------------------------------------------------------------------
  // RF protocol formatting
  // --------------------------------------------------------------------------

  std::vector<uint8_t> addr = {
      DEFAULT_BLE_FASTCON_ADDRESS.begin(),
      DEFAULT_BLE_FASTCON_ADDRESS.end()
  };


  return prepare_payload(
      addr,
      body);
}


// ============================================================================
// Simple Color Fade
// ============================================================================

void SimpleColorFadeAction::play() {

  if (this->controller_ == nullptr) {

    ESP_LOGW(
        TAG,
        "No controller bound; dropping "
        "Simple Color Fade");

    return;
  }


std::vector<uint8_t> effect_data = {
    0x48,
    static_cast<uint8_t>(this->light_id_ & 0xFF),
    this->speed_,
    static_cast<uint8_t>(0x40 | this->color_),
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
};


  std::vector<uint8_t> payload =
      this->controller_->effect_control(
          this->light_id_,
          effect_data);


  if (payload.empty()) {

    ESP_LOGW(
        TAG,
        "Failed to generate "
        "Simple Color Fade");

    return;
  }


  this->controller_->queueCommand(
      this->light_id_,
      payload);


  ESP_LOGD(
      TAG,
      "Simple Color Fade: "
      "light=%u speed=%u color=%u",
      (unsigned) this->light_id_,
      (unsigned) this->speed_,
      (unsigned) this->color_);
}


// ============================================================================
// Full Color Fade
// ============================================================================

void FullColorFadeAction::play() {

  if (this->controller_ == nullptr) {

    ESP_LOGW(
        TAG,
        "No controller bound; dropping "
        "Full Color Fade");

    return;
  }


  if (this->speed_ < 1 ||
      this->speed_ > 100) {

    ESP_LOGW(
        TAG,
        "Invalid Full Color Fade speed: %u",
        (unsigned) this->speed_);

    return;
  }


  if (this->current_color_ < 1 ||
      this->current_color_ > 7) {

    ESP_LOGW(
        TAG,
        "Invalid Full Color Fade "
        "current color: %u",
        (unsigned) this->current_color_);

    return;
  }


std::vector<uint8_t> effect_data = {
    0x98,
    static_cast<uint8_t>(this->light_id_ & 0xFF),
    this->speed_,
    static_cast<uint8_t>(0x40 | this->current_color_),
    this->sequence_[0],
    this->sequence_[1],
    this->sequence_[2],
    this->sequence_[3],
    this->sequence_[4],
    this->sequence_[5],
    0x00,
    0x00
};


  std::vector<uint8_t> payload =
      this->controller_->effect_control(
          this->light_id_,
          effect_data);


  if (payload.empty()) {

    ESP_LOGW(
        TAG,
        "Failed to generate "
        "Full Color Fade");

    return;
  }


  this->controller_->queueCommand(
      this->light_id_,
      payload);


  ESP_LOGD(
      TAG,
      "Full Color Fade: "
      "light=%u speed=%u current_color=%u",
      (unsigned) this->light_id_,
      (unsigned) this->speed_,
      (unsigned) this->current_color_);
}


// ============================================================================
// Full Color Flash
// ============================================================================

void FullColorFlashAction::play() {

  if (this->controller_ == nullptr) {

    ESP_LOGW(
        TAG,
        "No controller bound; dropping "
        "Full Color Flash");

    return;
  }


  if (this->speed_ < 1 ||
      this->speed_ > 100) {

    ESP_LOGW(
        TAG,
        "Invalid Full Color Flash speed: %u",
        (unsigned) this->speed_);

    return;
  }


  if (this->current_color_ < 1 ||
      this->current_color_ > 7) {

    ESP_LOGW(
        TAG,
        "Invalid Full Color Flash "
        "current color: %u",
        (unsigned) this->current_color_);

    return;
  }


  // --------------------------------------------------------------------------
  // BRmesh Flash speed encoding
  //
  // User speed -> protocol byte
  //
  // speed 1   -> C8
  // speed 20  -> A2
  // speed 40  -> 7A
  // speed 50  -> 66
  // speed 100 -> 02
  //
  // Formula:
  //
  // protocol_speed = 0xCA - (2 * speed)
  // --------------------------------------------------------------------------

  const uint8_t protocol_speed =
      static_cast<uint8_t>(
          0xCA - (2 * this->speed_));


  std::vector<uint8_t> effect_data = {

      0x98,

      static_cast<uint8_t>(
          this->light_id_ & 0xFF),

      protocol_speed,

      static_cast<uint8_t>(
          0xC0 | this->current_color_),

      this->sequence_[0],
      this->sequence_[1],
      this->sequence_[2],
      this->sequence_[3],
      this->sequence_[4],
      this->sequence_[5],

      0x00,
      0x00
  };


  std::vector<uint8_t> payload =
      this->controller_->effect_control(
          this->light_id_,
          effect_data);


  if (payload.empty()) {

    ESP_LOGW(
        TAG,
        "Failed to generate "
        "Full Color Flash");

    return;
  }


  this->controller_->queueCommand(
      this->light_id_,
      payload);


  ESP_LOGD(
      TAG,
      "Full Color Flash: "
      "light=%u speed=%u current_color=%u "
      "protocol_speed=0x%02X",
      (unsigned) this->light_id_,
      (unsigned) this->speed_,
      (unsigned) this->current_color_,
      (unsigned) protocol_speed);
}


}  // namespace fastcon
}  // namespace esphome
