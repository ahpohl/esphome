#include "xiaomi_lywsd03mmc.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_lywsd03mmc {

static const char *TAG = "xiaomi_lywsd03mmc";

void XiaomiLYWSD03MMC::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi LYWSD03MMC");
  ESP_LOGCONFIG(TAG, "  Bindkey: %s", hexencode(this->bindkey_, 16).c_str());
  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_);
}

bool XiaomiLYWSD03MMC::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_) {
    // ESP_LOGD(TAG, "XiaomiLYWSD03MMC::parse_device() wrong MAC address.");
    return false;
  }
  for (auto &service_data : device.get_service_datas()) {
    if (!(xiaomi_ble::decrypt_xiaomi_payload(const_cast<std::vector<uint8_t> &>(service_data.data), this->bindkey_))) {
      // ESP_LOGD(TAG, "xiaomi_ble::decrypt_xiaomi_payload() failed.");
      return false;
    }
    // ESP_LOGD(TAG, "Packet : %s", hexencode(service_data.data.data(), service_data.data.size()).c_str());
  }

  auto res = xiaomi_ble::parse_xiaomi(device);
  if (!res.has_value()) {
    // ESP_LOGD(TAG, "xiaomi_ble::parse_xiaomi() no result.");
    return false;
  }

  if (res->temperature.has_value() && this->temperature_ != nullptr)
    this->temperature_->publish_state(*res->temperature);
  if (res->humidity.has_value() && this->humidity_ != nullptr)
    this->humidity_->publish_state(*res->humidity);
  if (res->battery_level.has_value() && this->battery_level_ != nullptr)
    this->battery_level_->publish_state(*res->battery_level);
  return true;
}

void XiaomiLYWSD03MMC::set_bindkey(std::string const& t_bindkey) {
  if (t_bindkey.size() != 16) {
      return;
  }
  memcpy(bindkey_, t_bindkey.c_str(), t_bindkey.size());
}

}  // namespace xiaomi_lywsd03mmc
}  // namespace esphome

#endif
