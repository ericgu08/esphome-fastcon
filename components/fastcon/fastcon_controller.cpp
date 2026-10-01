std::vector<uint8_t> FastconController::single_control(
    uint32_t light_id_,
    const std::vector<uint8_t> &light_data) {

  // Normal light command:
  // byte 0 = length/type
  // byte 1 = light ID
  // bytes 2.. = light data
  std::vector<uint8_t> result_data(12, 0);

  if (light_data.size() > 10) {
    ESP_LOGW(TAG,
             "Light data too large for normal single_control: %d bytes",
             (int) light_data.size());
    return {};
  }

  result_data[0] =
      2 | (((0x0FFFFFF & (light_data.size() + 1)) << 4));

  result_data[1] = light_id_;

  std::copy(
      light_data.begin(),
      light_data.end(),
      result_data.begin() + 2);

  const auto hex_vec = vector_to_hex_string(result_data);
  const std::string hex(hex_vec.begin(), hex_vec.end());

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


// -----------------------------------------------------------------------------
// BRmesh effect command
// -----------------------------------------------------------------------------
//
// Effect payload is already a complete 12-byte BRmesh application payload.
//
// Examples:
//
// Simple Color Fade:
//   48 03 19 42 00 00 00 00 00 00 00 00
//
// Full Color Fade:
//   98 03 28 47 05 03 06 01 04 02 00 00
//
// Full Color Flash:
//   88 03 66 C2 03 01 05 04 06 00 00 00
//
// The effect payload must NOT pass through the normal light_data
// length/type wrapper used by single_control().
//
std::vector<uint8_t> FastconController::effect_control(
    uint32_t light_id,
    const std::vector<uint8_t> &effect_data) {

  if (effect_data.size() != 12) {
    ESP_LOGW(
        TAG,
        "Invalid BRmesh effect payload size: %d (expected 12)",
        (int) effect_data.size());

    return {};
  }

  const auto hex_vec = vector_to_hex_string(effect_data);
  const std::string hex(hex_vec.begin(), hex_vec.end());

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
