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
    ESP_LOGVV(TAG, "XiaomiLYWSD03MMC::parse_device(): unknown MAC address.");
    return false;
  }

  auto res = xiaomi_ble::parse_xiaomi_header(device);
  if (res->has_capability) {
    ESP_LOGVV(TAG, "XiaomiLYWSD03MMC::parse_device(): service data has capability.");
    return false;
  }

  if (!res.has_value()) {
    ESP_LOGVV(TAG, "XiaomiLYWSD03MMC::parse_device(): no service data received.");
    return false;
  }

  esp32_ble_tracker::ServiceData service_data = device.get_service_data();
  if (res->has_encryption) {
      xiaomi_ble::decrypt_xiaomi_payload(const_cast<std::vector<uint8_t> &>(service_data.data), this->bindkey_);
  }

  if (!(xiaomi_ble::parse_xiaomi_message(service_data.data, *res))) {
    ESP_LOGVV(TAG, "XiaomiLYWSD03MMC::parse_device(): message contains no results.");
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

void XiaomiLYWSD03MMC::set_bindkey(const std::string &t_bindkey) {
  if (t_bindkey.size() != 16) {
    return;
  }
  memcpy(bindkey_, t_bindkey.c_str(), t_bindkey.size());
}

}  // namespace xiaomi_lywsd03mmc
}  // namespace esphome

#endif
