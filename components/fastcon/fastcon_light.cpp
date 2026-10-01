#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"

#include "fastcon_controller.h"
#include "fastcon_light.h"

#ifndef FASTCON_VERSION
#define FASTCON_VERSION "0.3.3-dev"
#endif

namespace esphome {
namespace fastcon {

static const char *const TAG = "fastcon.light";

light::LightTraits FastconLight::get_traits() {
  light::LightTraits t;

  if (this->color_interlock_) {
    if (this->supports_cwww_) {
      t.set_supported_color_modes({
          light::ColorMode::RGB,
          light::ColorMode::COLD_WARM_WHITE
      });
    } else {
      t.set_supported_color_modes({
          light::ColorMode::RGB,
          light::ColorMode::WHITE
      });
    }
  } else {
    if (this->supports_cwww_) {
      t.set_supported_color_modes({
          light::ColorMode::RGB_COLD_WARM_WHITE
      });
    } else {
      t.set_supported_color_modes({
          light::ColorMode::RGB_WHITE
      });
    }
  }

  if (this->supports_cwww_) {
    t.set_min_mireds(153.0f);
    t.set_max_mireds(500.0f);
  }

  return t;
}


void FastconLight::write_state(light::LightState *state) {
  if (this->controller_ == nullptr) {
    ESP_LOGW(TAG, "No controller bound; dropping command");
    return;
  }

  std::vector<uint8_t> light_bytes;
  auto &values = state->current_values;

  bool is_white_only =
      values.get_color_mode() == light::ColorMode::WHITE;

  if (is_white_only) {
    ESP_LOGD(
        TAG,
        "Sending white-only command for light %u",
        (unsigned) this->light_id_);

    light_bytes =
        this->controller_->get_white_light_data(state);

  } else {
    ESP_LOGD(
        TAG,
        "Sending RGB/color command for light %u",
        (unsigned) this->light_id_);

    light_bytes =
        this->controller_->get_light_data(state);
  }

  std::vector<uint8_t> payload =
      this->controller_->single_control(
          this->light_id_,
          light_bytes);

  if (payload.empty()) {
    ESP_LOGW(TAG, "Failed to generate light command");
    return;
  }

  this->controller_->queueCommand(
      this->light_id_,
      payload);

  ESP_LOGD(
      TAG,
      "Queued state v%s: light_id=%u, payload_len=%d",
      FASTCON_VERSION,
      (unsigned) this->light_id_,
      (int) payload.size());
}


// -----------------------------------------------------------------------------
// Simple Color Fade
// -----------------------------------------------------------------------------
//
// Payload:
//
//   48 03 SS CC 00 00 00 00 00 00 00 00
//
// SS = speed
//
// CC:
//   41 = Blue
//   42 = Red
//   43 = Magenta
//   44 = Green
//   45 = Cyan
//   46 = Yellow
//   47 = White
//
void FastconLight::simple_color_fade(
    uint8_t speed,
    uint8_t color) {

  if (this->controller_ == nullptr) {
    ESP_LOGW(TAG, "No controller bound; dropping Simple Color Fade");
    return;
  }

  if (color < 1 || color > 7) {
    ESP_LOGW(
        TAG,
        "Invalid BRmesh color index: %u",
        (unsigned) color);
    return;
  }

  std::vector<uint8_t> effect_data = {
      0x48,
      0x03,
      speed,
      static_cast<uint8_t>(0x40 | color),
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
    ESP_LOGW(TAG, "Failed to generate Simple Color Fade");
    return;
  }

  this->controller_->queueCommand(
      this->light_id_,
      payload);

  ESP_LOGD(
      TAG,
      "Simple Color Fade: speed=%u color=%u",
      (unsigned) speed,
      (unsigned) color);
}


// -----------------------------------------------------------------------------
// Full Color Fade
// -----------------------------------------------------------------------------
//
// Payload:
//
//   98 03 SS CC S1 S2 S3 S4 S5 S6 00 00
//
// CC:
//   bit 7 = 0 -> Fade
//   bit 6 = 1
//   bits 5..0 = current color index
//
// SS = speed
//
// S1..S6 = remaining six color indexes
//
void FastconLight::full_color_fade(
    uint8_t speed,
    uint8_t current_color,
    const std::array<uint8_t, 6> &sequence) {

  if (this->controller_ == nullptr) {
    ESP_LOGW(TAG, "No controller bound; dropping Full Color Fade");
    return;
  }

  if (current_color < 1 || current_color > 7) {
    ESP_LOGW(
        TAG,
        "Invalid current color index: %u",
        (unsigned) current_color);
    return;
  }

  std::vector<uint8_t> effect_data = {
      0x98,
      0x03,
      speed,
      static_cast<uint8_t>(0x40 | current_color),
      sequence[0],
      sequence[1],
      sequence[2],
      sequence[3],
      sequence[4],
      sequence[5],
      0x00,
      0x00
  };

  std::vector<uint8_t> payload =
      this->controller_->effect_control(
          this->light_id_,
          effect_data);

  if (payload.empty()) {
    ESP_LOGW(TAG, "Failed to generate Full Color Fade");
    return;
  }

  this->controller_->queueCommand(
      this->light_id_,
      payload);

  ESP_LOGD(
      TAG,
      "Full Color Fade: speed=%u current_color=%u",
      (unsigned) speed,
      (unsigned) current_color);
}


// -----------------------------------------------------------------------------
// Full Color Flash
// -----------------------------------------------------------------------------
//
// User speed:
//   1..100
//
// Protocol speed:
//
//   SS = 0xCA - (2 * speed)
//
// Examples:
//
//   speed 1   -> C8
//   speed 20  -> A2
//   speed 40  -> 7A
//   speed 50  -> 66
//   speed 100 -> 02
//
// Payload:
//
//   88 03 SS CC S1 S2 S3 S4 S5 S6 00 00
//
void FastconLight::full_color_flash(
    uint8_t speed,
    uint8_t current_color,
    const std::array<uint8_t, 6> &sequence) {

  if (this->controller_ == nullptr) {
    ESP_LOGW(TAG, "No controller bound; dropping Full Color Flash");
    return;
  }

  if (speed < 1 || speed > 100) {
    ESP_LOGW(
        TAG,
        "Invalid Flash speed: %u (expected 1..100)",
        (unsigned) speed);
    return;
  }

  if (current_color < 1 || current_color > 7) {
    ESP_LOGW(
        TAG,
        "Invalid current color index: %u",
        (unsigned) current_color);
    return;
  }

  const uint8_t protocol_speed =
      static_cast<uint8_t>(0xCA - (2 * speed));

  std::vector<uint8_t> effect_data = {
      0x88,
      0x03,
      protocol_speed,
      static_cast<uint8_t>(0xC0 | current_color),
      sequence[0],
      sequence[1],
      sequence[2],
      sequence[3],
      sequence[4],
      sequence[5],
      0x00,
      0x00
  };

  std::vector<uint8_t> payload =
      this->controller_->effect_control(
          this->light_id_,
          effect_data);

  if (payload.empty()) {
    ESP_LOGW(TAG, "Failed to generate Full Color Flash");
    return;
  }

  this->controller_->queueCommand(
      this->light_id_,
      payload);

  ESP_LOGD(
      TAG,
      "Full Color Flash: speed=%u protocol_speed=0x%02X current_color=%u",
      (unsigned) speed,
      (unsigned) protocol_speed,
      (unsigned) current_color);
}

}  // namespace fastcon
}  // namespace esphome
