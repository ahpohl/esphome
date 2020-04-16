#include "xiaomi_lywsd02.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_lywsd02 {

static const char *TAG = "xiaomi_lywsd02";

void XiaomiLYWSD02::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi LYWSD02");
  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
}

bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override {
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

  if (res->temperature.has_value() && this->temperature_ != nullptr)
    this->temperature_->publish_state(*res->temperature);
  if (res->humidity.has_value() && this->humidity_ != nullptr)
    this->humidity_->publish_state(*res->humidity);
  return true;
}

}  // namespace xiaomi_lywsd02
}  // namespace esphome

#endif
