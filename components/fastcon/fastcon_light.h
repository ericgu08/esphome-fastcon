#pragma once

#include <array>
#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"

namespace esphome {
namespace fastcon {

class FastconController;

class FastconLight : public Component, public light::LightOutput {
 public:
  FastconLight() = default;

  explicit FastconLight(int light_id) {
    this->light_id_ = static_cast<uint8_t>(light_id);
  }

  void set_controller(FastconController *c) {
    controller_ = c;
  }

  void set_light_id(uint8_t id) {
    light_id_ = id;
  }

  void set_supports_cwww(bool v) {
    supports_cwww_ = v;
  }

  void set_color_interlock(bool v) {
    color_interlock_ = v;
  }

  // Intervalle minimal (ms) entre deux envois d'état pour cette lampe.
  void set_min_send_interval(uint16_t ms) {
    min_send_interval_ms_ = ms;
  }

  // --------------------------------------------------------------------------
  // BRmesh effects
  // --------------------------------------------------------------------------

  void simple_color_fade(uint8_t speed, uint8_t color);

  void full_color_fade(
      uint8_t speed,
      uint8_t current_color,
      const std::array<uint8_t, 6> &sequence);

  void full_color_flash(
      uint8_t speed,
      uint8_t current_color,
      const std::array<uint8_t, 6> &sequence);

  // Component interface
  void loop() override;

  // LightOutput interface
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  // Génère la commande à partir de l'état courant et la met en file.
  void send_state_(light::LightState *state);

  FastconController *controller_{nullptr};

  uint8_t light_id_{0};

  bool supports_cwww_{false};
  bool color_interlock_{false};

  // Envoi différé : limite le débit de write_state tout en garantissant
  // que la dernière valeur est toujours envoyée.
  uint32_t last_send_ms_{0};
  uint16_t min_send_interval_ms_{100};
  light::LightState *pending_state_{nullptr};
};

}  // namespace fastcon
}  // namespace esphome
