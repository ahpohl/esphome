#include "xiaomi_provision.h"
#include "esphome/core/log.h"

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_provision {

static const char *TAG = "xiaomi_provision";

void XiaomiProvision::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi Provision");
  ESP_LOGCONFIG(TAG, "  MAC Address:");
}

bool XiaomiProvision::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_) {
    ESP_LOGVV(TAG, "parse_device(): unknown MAC address.");
    return false;
  }
  ESP_LOGVV(TAG, "parse_device(): MAC address %s found.", device.address_str().c_str());

  return true;
}

bool generate_xiaomi_bindkey(void) {
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_entropy_context entropy;
  mbedtls_entropy_init(&entropy);
  char random_seed[] = "aes generate key";

  int ret =
      mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (uint8_t *) random_seed, strlen(random_seed));
  if (ret) {
    ESP_LOGVV(TAG, "mbedtls_ctr_drbg_init() returned -0x%04x\n", -ret);
    return false;
  }
  ret = mbedtls_ctr_drbg_random(&ctr_drbg, this->bindkey_, 16);
  if (ret) {
    ESP_LOGVV(TAG, "mbedtls_ctr_drbg_random() returned -0x%04x\n", -ret);
    return false;
  }

  return true;
}

}  // namespace xiaomi_provision
}  // namespace esphome

#endif
