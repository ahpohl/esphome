#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/xiaomi_ble/xiaomi_ble.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_lywsd02 {

class XiaomiLYWSD02 : public Component, public esp32_ble_tracker::ESPBTDeviceListener {
 public:
  void set_address(uint64_t address) { address_ = address; }

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

  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_temperature(sensor::Sensor *temperature) { temperature_ = temperature; }
  void set_humidity(sensor::Sensor *humidity) { humidity_ = humidity; }

 protected:
  uint64_t address_;
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};
};

}  // namespace xiaomi_lywsd02
}  // namespace esphome

#endif
