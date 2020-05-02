#pragma once

#include "esphome/core/component.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_ble {

struct XiaomiParseResult {
  enum { TYPE_LYWSDCGQ, TYPE_HHCCJCY01, TYPE_LYWSD02, TYPE_CGG1, TYPE_LYWSD03MMC, TYPE_CGD1, TYPE_WX08ZM } type;
  optional<float> temperature;
  optional<float> humidity;
  optional<float> battery_level;
  optional<float> conductivity;
  optional<float> illuminance;
  optional<float> moisture;
  optional<float> tablet;
  optional<float> state;
  bool has_data;        // 0x40
  bool has_capability;  // 0x20
  bool has_encryption;  // 0x08
  bool is_duplicate;
  int raw_offset;
};

struct XiaomiAESVector {
  uint8_t key[16];
  uint8_t plaintext[16];
  uint8_t ciphertext[16];
  uint8_t authdata[16];
  uint8_t iv[16];
  uint8_t tag[16];
  size_t keysize;
  size_t authsize;
  size_t datasize;
  size_t tagsize;
  size_t ivsize;
};

bool parse_xiaomi_message(const std::vector<uint8_t> &message, XiaomiParseResult &result);
optional<XiaomiParseResult> parse_xiaomi_header(const esp32_ble_tracker::ServiceData &service_data);
bool decrypt_xiaomi_payload(std::vector<uint8_t> &raw, const uint8_t *bindkey);
bool report_xiaomi_results(const optional<XiaomiParseResult> &result, const std::string &address);

class XiaomiListener : public esp32_ble_tracker::ESPBTDeviceListener {
 public:
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
};

}  // namespace xiaomi_ble
}  // namespace esphome

#endif
