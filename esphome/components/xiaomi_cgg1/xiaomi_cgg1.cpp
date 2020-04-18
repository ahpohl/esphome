#include "xiaomi_cgg1.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_cgg1 {

static const char *TAG = "xiaomi_cgg1";

void XiaomiCGG1::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi CGG1");
  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_);
}

bool XiaomiCGG1::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_)
    return false;

  auto res = xiaomi_ble::parse_xiaomi_header(device);
  if (res->has_capability) {
    ESP_LOGVV(TAG, "parse_device(): service data has capability flag.");
    return false;
  }

  if (!res.has_value()) {
    ESP_LOGVV(TAG, "parse_device(): no service data received.");
    return false;
  }

  esp32_ble_tracker::ServiceData service_data = device.get_service_data();
  if (res->has_encryption) {
    ESP_LOGVV(TAG, "parse_device(): decryption is currently not supported on this device.");
    return false;
  }

  if (!(xiaomi_ble::parse_xiaomi_message(service_data.data, *res))) {
    ESP_LOGVV(TAG, "parse_device(): message contains no results.");
    return false;
  }

  xiaomi_ble::report_xiaomi_results(res, device.address_str());

  if (res->temperature.has_value() && this->temperature_ != nullptr)
    this->temperature_->publish_state(*res->temperature);
  if (res->humidity.has_value() && this->humidity_ != nullptr)
    this->humidity_->publish_state(*res->humidity);
  if (res->battery_level.has_value() && this->battery_level_ != nullptr)
    this->battery_level_->publish_state(*res->battery_level);
  return true;
}

}  // namespace xiaomi_cgg1
}  // namespace esphome

#endif
