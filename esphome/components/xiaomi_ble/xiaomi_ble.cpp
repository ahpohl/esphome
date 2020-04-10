#include "xiaomi_ble.h"
#include "esphome/core/log.h"

#include <string.h>
#include <mbedtls/ccm.h>

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_ble {

static const char *TAG = "xiaomi_ble";

bool parse_xiaomi_data_byte(uint8_t data_type, const uint8_t *data, uint8_t data_length, XiaomiParseResult &result)
{
  switch (data_type) {
    case 0x0D: {  // temperature+humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
      if (data_length != 4)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      const int16_t humidity = uint16_t(data[2]) | (uint16_t(data[3]) << 8);
      result.temperature = temperature / 10.0f;
      result.humidity = humidity / 10.0f;
      return true;
    }
    case 0x0A: {  // battery, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.battery_level = data[0];
      return true;
    }
    case 0x06: {  // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
      if (data_length != 2)
        return false;
      const int16_t humidity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.humidity = humidity / 10.0f;
      return true;
    }
    case 0x04: {  // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
      if (data_length != 2)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.temperature = temperature / 10.0f;
      return true;
    }
    case 0x09: {  // conductivity, 2 bytes, 16-bit unsigned integer (LE), 1 µS/cm
      if (data_length != 2)
        return false;
      const uint16_t conductivity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.conductivity = conductivity;
      return true;
    }
    case 0x07: {  // illuminance, 3 bytes, 24-bit unsigned integer (LE), 1 lx
      if (data_length != 3)
        return false;
      const uint32_t illuminance = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);
      result.illuminance = illuminance;
      return true;
    }
    case 0x08: {  // soil moisture, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.moisture = data[0];
      return true;
    }
    default:
      return false;
  }
}

// TODO: decrypt ADV message on the fly (in separate decrypt_xiaomi_payload() function)
// replace encrypted payload with plaintext
bool parse_xiaomi_service_data(XiaomiParseResult &result, const esp32_ble_tracker::ServiceData &service_data)
{
  if (!service_data.uuid.contains(0x95, 0xFE)) {
    ESP_LOGVV(TAG, "Xiaomi no service data UUID magic bytes");
    return false;
  }

  auto const raw = service_data.data;

  if (raw.size() < 14) {
    ESP_LOGVV(TAG, "Xiaomi service data too short!");
    return false;
  }

  bool is_lywsdcgq = (raw[1] & 0x20) == 0x20 && raw[2] == 0xAA && raw[3] == 0x01;
  bool is_hhccjcy01 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x98 && raw[3] == 0x00;
  bool is_lywsd02 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x5b && raw[3] == 0x04;
  bool is_cgg1 = ((raw[1] & 0x30) == 0x30 || (raw[1] & 0x20) == 0x20) && raw[2] == 0x47 && raw[3] == 0x03;
  bool is_lywsd03mmc = raw[2] == 0x5b && raw[3] == 0x05;

  if (!is_lywsdcgq && !is_hhccjcy01 && !is_lywsd02 && !is_cgg1 && !is_lywsd03mmc) {
    ESP_LOGVV(TAG, "Xiaomi no magic bytes");
    return false;
  }

  result.type = XiaomiParseResult::TYPE_HHCCJCY01;
  if (is_lywsdcgq) {
    result.type = XiaomiParseResult::TYPE_LYWSDCGQ;
  } else if (is_lywsd02) {
    result.type = XiaomiParseResult::TYPE_LYWSD02;
  } else if (is_cgg1) {
    result.type = XiaomiParseResult::TYPE_CGG1;
  } else if (is_lywsd03mmc) {
    result.type = XiaomiParseResult::TYPE_LYWSD03MMC;
  }
  
  if (is_lywsd03mmc) {
    int ret = decrypt_xiaomi_payload(raw.data(), raw.size());
    if (ret == false) {
      ESP_LOGD(TAG, "Decrypt Xiaomi payload failed.");
    }
  }

  uint8_t raw_offset = is_lywsdcgq || is_cgg1 ? 11 : 12;

  // Data point specs
  // Byte 0: type
  // Byte 1: fixed 0x10
  // Byte 2: length
  // Byte 3..3+len-1: data point value

  const uint8_t *raw_data = &raw[raw_offset];
  uint8_t data_offset = 0;
  uint8_t data_length = raw.size() - raw_offset;
  bool success = false;

  while (true) {
    if (data_length < 4)
      // at least 4 bytes required
      // type, fixed 0x10, length, 1 byte value
      break;

    const uint8_t datapoint_type = raw_data[data_offset + 0];
    const uint8_t datapoint_length = raw_data[data_offset + 2];

    if (data_length < 3 + datapoint_length)
      // 3 fixed bytes plus value length
      break;

    const uint8_t *datapoint_data = &raw_data[data_offset + 3];

    if (parse_xiaomi_data_byte(datapoint_type, datapoint_data, datapoint_length, result))
      success = true;

    data_length -= data_offset + 3 + datapoint_length;
    data_offset += 3 + datapoint_length;
  }

  return success;
}

optional<XiaomiParseResult> parse_xiaomi(const esp32_ble_tracker::ESPBTDevice &device)
{
  XiaomiParseResult result;
  bool success = false;
  for (auto &service_data : device.get_service_datas()) {
    if (parse_xiaomi_service_data(result, service_data))
      success = true;
  }
  if (!success)
    return {};
  return result;
}

bool XiaomiListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device)
{
  auto res = parse_xiaomi(device);
  if (!res.has_value())
    return false;

  const char *name = "HHCCJCY01";
  if (res->type == XiaomiParseResult::TYPE_LYWSDCGQ) {
    name = "LYWSDCGQ";
  } else if (res->type == XiaomiParseResult::TYPE_LYWSD02) {
    name = "LYWSD02";
  } else if (res->type == XiaomiParseResult::TYPE_CGG1) {
    name = "CGG1";
  } else if (res->type == XiaomiParseResult::TYPE_LYWSD03MMC) {
    name = "LYWSD03MMC";
  }

  ESP_LOGD(TAG, "Got Xiaomi %s (%s):", name, device.address_str().c_str());

  if (res->temperature.has_value()) {
    ESP_LOGD(TAG, "  Temperature: %.1f°C", *res->temperature);
  }
  if (res->humidity.has_value()) {
    ESP_LOGD(TAG, "  Humidity: %.1f%%", *res->humidity);
  }
  if (res->battery_level.has_value()) {
    ESP_LOGD(TAG, "  Battery Level: %.0f%%", *res->battery_level);
  }
  if (res->conductivity.has_value()) {
    ESP_LOGD(TAG, "  Conductivity: %.0fµS/cm", *res->conductivity);
  }
  if (res->illuminance.has_value()) {
    ESP_LOGD(TAG, "  Illuminance: %.0flx", *res->illuminance);
  }
  if (res->moisture.has_value()) {
    ESP_LOGD(TAG, "  Moisture: %.0f%%", *res->moisture);
  }

  return true;
}

bool decrypt_xiaomi_payload(unsigned char const* t_raw, size_t t_length)
{
  if (t_length < 23) {
    ESP_LOGD(TAG, "Payload too short (%d)!", t_length);
    return false;
  }
  if (!(t_raw[0] & 0x08)) {
    ESP_LOGD(TAG, "Plaintext ADV payload");
    return false;
  }
  // TODO: get MAC from config, check MAC match (raw <-> config)
  // construct IV: MAC(6) + sensor_type(2) + packet_id(1) = 9 bytes
  // std::string iv = "raw[bytes 5-10] + raw[bytes 2-3] + raw[byte 4]";
  // get encryption_key from config

  // BLE ADV packet capture
  // Tag: 9F1F0F10 vs. 92982352
  AESVector_t constexpr vector = {
      .name        = "AES-128 CCM BLE ADV",
      .key         = {0xE9, 0xEF, 0xAA, 0x68, 0x73, 0xF9, 0xF9, 0xC8,
		      0x7A, 0x5E, 0x75, 0xA5, 0xF8, 0x14, 0x80, 0x1C},
      .plaintext   = {0x04, 0x10, 0x02, 0xD3, 0x00},
      .ciphertext  = {0xDA, 0x61, 0x66, 0x77, 0xD5},
      .authdata    = {0x11},
      .iv          = {0x78, 0x16, 0x4E, 0x38, 0xC1, 0xA4, 0x5B, 0x05,
		      0x3D, 0x2E, 0x00, 0x00},
      .tag         = {0x9F, 0x1F, 0x0F, 0x10},
      .authsize    = 1,
      .datasize    = 5,
      .tagsize     = 4,
      .ivsize      = 12
  };

  size_t const AES_KEY_SIZE = 128;
  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);

  int ret = 0;
  ret = mbedtls_ccm_setkey(&ctx,
    MBEDTLS_CIPHER_ID_AES,
    vector.key,
    AES_KEY_SIZE
  );
  if (ret) {
    ESP_LOGD(TAG, "AES-CCM setkey failed.");
    return false;
  }

  unsigned char plaintext[16] = {0};

  ret = mbedtls_ccm_auth_decrypt(&ctx,
    vector.datasize,
    vector.iv,
    vector.ivsize,
    vector.authdata,
    vector.datasize,
    vector.ciphertext,
    plaintext,
    vector.tag,
    vector.tagsize
  );

  if (ret) {
    if (ret == MBEDTLS_ERR_CCM_AUTH_FAILED) {
      ESP_LOGD(TAG, "Authenticated decryption failed.");
    } else if (ret == MBEDTLS_ERR_CCM_BAD_INPUT) {
      ESP_LOGD(TAG, "Bad input parameters to the function.");
    } else if (ret == MBEDTLS_ERR_CCM_HW_ACCEL_FAILED) {
      ESP_LOGD(TAG, "CCM hardware accelerator failed.");
    }
    return false;
  } else {
    ESP_LOGD(TAG, "Authenticated decryption successful.");
  }

  mbedtls_ccm_free(&ctx);

  // replace encrypted payload with plaintext

  return true;
}

}  // namespace xiaomi_ble
}  // namespace esphome

#endif
