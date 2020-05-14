#include "xiaomi_mjyd2s.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_mjyd2s {

static const char *TAG = "xiaomi_mjyd2s";

void XiaomiMJYD2S::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi MJYD2S");
  LOG_BINARY_SENSOR("  ", "Motion", this);
}

bool XiaomiMJYD2S::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_) {
    ESP_LOGVV(TAG, "parse_device(): unknown MAC address.");
    return false;
  }
  ESP_LOGVV(TAG, "parse_device(): MAC address %s found.", device.address_str().c_str());

  bool success = false;
  for (auto &service_data : device.get_service_datas()) {
    auto res = xiaomi_ble::parse_xiaomi_header(service_data);
    if (!res.has_value()) {
      continue;
    }
    if (res->is_duplicate) {
      continue;
    }
    if (res->has_encryption && (!(xiaomi_ble::decrypt_xiaomi_payload(
                                   const_cast<std::vector<uint8_t> &>(service_data.data), this->bindkey_)))) {
      continue;
    }
    if (!(xiaomi_ble::parse_xiaomi_message(service_data.data, *res))) {
      continue;
    }
    if (!(xiaomi_ble::report_xiaomi_results(res, device.address_str()))) {
      continue;
    }
    if (res->has_motion.has_value()) {
      this->publish_state(*res->has_motion);
      this->set_timeout("motion_timeout", timeout_, [this]() { this->publish_state(false); });
    }
    success = true;
  }

  if (!success) {
    return false;
  }

  return true;
}

void XiaomiMJYD2S::set_bindkey(const std::string &bindkey) {
  memset(bindkey_, 0, 16);
  if (bindkey.size() != 32) {
    return;
  }
  char temp[3] = {0};
  for (int i = 0; i < 16; i++) {
    strncpy(temp, &(bindkey.c_str()[i * 2]), 2);
    bindkey_[i] = std::strtoul(temp, NULL, 16);
  }
}

}  // namespace xiaomi_mjyd2s
}  // namespace esphome

#endif
