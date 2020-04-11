#include "xiaomi_lywsd03mmc.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_lywsd03mmc {

static const char *TAG = "xiaomi_lywsd03mmc";

void XiaomiLYWSD03MMC::dump_config()
{
  ESP_LOGCONFIG(TAG, "Xiaomi LYWSD03MMC");
  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_);
}

bool XiaomiLYWSD03MMC::parse_device(const esp32_ble_tracker::ESPBTDevice &device)
{
  if (device.address_uint64() != this->address_)
    return false;

  auto res = xiaomi_ble::parse_xiaomi(device);
  if (!res.has_value())
    return false;

  if (res->temperature.has_value() && this->temperature_ != nullptr)
    this->temperature_->publish_state(*res->temperature);
  if (res->humidity.has_value() && this->humidity_ != nullptr)
    this->humidity_->publish_state(*res->humidity);
  if (res->battery_level.has_value() && this->battery_level_ != nullptr)
    this->battery_level_->publish_state(*res->battery_level);
  return true;
}

}  // namespace xiaomi_lywsd03mmc
}  // namespace esphome

#endif
