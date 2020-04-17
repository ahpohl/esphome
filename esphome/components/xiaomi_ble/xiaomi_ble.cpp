#include "xiaomi_ble.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef ARDUINO_ARCH_ESP32

#include <vector>
#include "mbedtls/ccm.h"

namespace esphome {
namespace xiaomi_ble {

static const char *TAG = "xiaomi_ble";

bool parse_xiaomi_data_byte(uint8_t data_type, const uint8_t *data, uint8_t data_length, XiaomiParseResult &result) {
  if ((data_length < 1) || (data_length > 4)) {
    ESP_LOGVV(TAG, "parse_xiaomi_data_byte(): payload has wrong size (%d)!", data_length);
    return false;
  }
  switch (data_type) {
    case 0x0D: {  // temperature+humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
      if (data_length != 4)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      const int16_t humidity = uint16_t(data[2]) | (uint16_t(data[3]) << 8);
      result.temperature = temperature / 10.0f;
      result.humidity = humidity / 10.0f;
      break;
    }
    case 0x0A: {  // battery, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.battery_level = data[0];
      break;
    }
    case 0x06: {  // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
      if (data_length != 2)
        return false;
      const int16_t humidity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.humidity = humidity / 10.0f;
      break;
    }
    case 0x04: {  // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
      if (data_length != 2)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.temperature = temperature / 10.0f;
      break;
    }
    case 0x09: {  // conductivity, 2 bytes, 16-bit unsigned integer (LE), 1 µS/cm
      if (data_length != 2)
        return false;
      const uint16_t conductivity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.conductivity = conductivity;
      break;
    }
    case 0x07: {  // illuminance, 3 bytes, 24-bit unsigned integer (LE), 1 lx
      if (data_length != 3)
        return false;
      const uint32_t illuminance = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);
      result.illuminance = illuminance;
      break;
    }
    case 0x08: {  // soil moisture, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.moisture = data[0];
      break;
    }
    default:
      return false;
  }

  return true;
}

bool parse_xiaomi_service_data(XiaomiParseResult &result, const esp32_ble_tracker::ServiceData &service_data) {
  if (!service_data.uuid.contains(0x95, 0xFE)) {
    ESP_LOGVV(TAG, "parse_xiaomi_service_data(): no service data UUID magic bytes");
    return false;
  }

  auto raw = service_data.data;

  if (raw.size() < 14) {
    ESP_LOGVV(TAG, "parse_xiaomi_service_data(): service data too short (%d)!", raw.size());
    return false;
  }

  bool is_lywsdcgq = (raw[1] & 0x20) == 0x20 && raw[2] == 0xAA && raw[3] == 0x01;
  bool is_hhccjcy01 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x98 && raw[3] == 0x00;
  bool is_lywsd02 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x5b && raw[3] == 0x04;
  bool is_cgg1 = ((raw[1] & 0x30) == 0x30 || (raw[1] & 0x20) == 0x20) && raw[2] == 0x47 && raw[3] == 0x03;
  bool is_lywsd03mmc = (raw[1] & 0x58) == 0x58 && raw[2] == 0x5b && raw[3] == 0x05;

  if (!is_lywsdcgq && !is_hhccjcy01 && !is_lywsd02 && !is_cgg1 && !is_lywsd03mmc) {
    ESP_LOGVV(TAG, "parse_xiaomi_service_data(): no magic bytes");
    return false;
  }

  static uint8_t last_frame_count = 0;
  if (is_lywsd03mmc && (raw[0] & 0x20) && (last_frame_count == raw[4])) {
    ESP_LOGVV(TAG, "parse_xiaomi_service_data(): duplicate data packet received (%d).", last_frame_count);
    last_frame_count = raw[4];
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

  if (is_lywsd03mmc && (raw[0] & 0x08)) {
    ESP_LOGVV(TAG, "parse_xiaomi_service_data(): encrypted packet received.");
    return false;
  }

  uint8_t raw_offset = is_lywsdcgq || is_cgg1 || is_lywsd03mmc ? 11 : 12;

  // Data point specs
  // Byte 0: type
  // Byte 1: fixed 0x10
  // Byte 2: length
  // Byte 3..3+len-1: data point value

  uint8_t *raw_data = raw.data() + raw_offset;

  if (parse_xiaomi_data_byte(raw_data[0], raw_data + 3, raw_data[2], result)) {
    return true;
  }

  return false;
}

bool decrypt_xiaomi_payload(std::vector<uint8_t> &raw, const uint8_t *bindkey) {
  if (!(raw[0] & 0x08)) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): data packet is not encrypted!");
    ESP_LOGVV(TAG, "  Packet : %s", hexencode(raw.data(), raw.size()).c_str());
    return false;
  }
  if ((raw.size() < 22) || (raw.size() > 23)) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): data packet has wrong size (%d)!", raw.size());
    ESP_LOGVV(TAG, "  Packet : %s", hexencode(raw.data(), raw.size()).c_str());
    return false;
  }

  XiaomiAESVector vector{.key = {0},
                         .plaintext = {0},
                         .ciphertext = {0},
                         .authdata = {0x11},
                         .iv = {0},
                         .tag = {0},
                         .keysize = 16,
                         .authsize = 1,
                         .datasize = 4,  // battery
                         .tagsize = 4,
                         .ivsize = 12};

  int offset = 0;
  if (raw.size() == 23) {
    vector.datasize = 5;  // temperature or humidity
    offset = 1;
  }

  const uint8_t *v = raw.data();
  memcpy(vector.key, bindkey, vector.keysize);
  memcpy(vector.ciphertext, v + 11, vector.datasize);
  memcpy(vector.tag, v + 18 + offset, vector.tagsize);
  memcpy(vector.iv, v + 5, 6);                // MAC address reversed
  memcpy(vector.iv + 6, v + 2, 3);            // sensor type (2) + packet id (1)
  memcpy(vector.iv + 9, v + 15 + offset, 3);  // payload counter

  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);

  int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, vector.key, vector.keysize * 8);
  if (ret) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): mbedtls_ccm_setkey() failed.");
    mbedtls_ccm_free(&ctx);
    return false;
  }

  ret = mbedtls_ccm_auth_decrypt(&ctx, vector.datasize, vector.iv, vector.ivsize, vector.authdata, vector.authsize,
                                 vector.ciphertext, vector.plaintext, vector.tag, vector.tagsize);
  if (ret) {
    uint8_t mac_address[6] = {0};
    memcpy(mac_address, v + 10, 1);
    memcpy(mac_address + 1, v + 9, 1);
    memcpy(mac_address + 2, v + 8, 1);
    memcpy(mac_address + 3, v + 7, 1);
    memcpy(mac_address + 4, v + 6, 1);
    memcpy(mac_address + 5, v + 5, 1);

    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): authenticated decryption failed.");
    ESP_LOGVV(TAG, "  MAC address : %s", hexencode(mac_address, 6).c_str());
    ESP_LOGVV(TAG, "  Packet : %s", hexencode(raw.data(), raw.size()).c_str());
    ESP_LOGVV(TAG, "  Key : %s", hexencode(vector.key, vector.keysize).c_str());
    ESP_LOGVV(TAG, "  Iv : %s", hexencode(vector.iv, vector.ivsize).c_str());
    ESP_LOGVV(TAG, "  Cipher : %s", hexencode(vector.ciphertext, vector.datasize).c_str());
    ESP_LOGVV(TAG, "  Tag : %s", hexencode(vector.tag, vector.tagsize).c_str());
    mbedtls_ccm_free(&ctx);
    return false;
  }

  // replace encrypted payload with plaintext
  uint8_t *p = vector.plaintext;
  for (std::vector<uint8_t>::iterator it = raw.begin() + 11; it != raw.begin() + 11 + vector.datasize; ++it) {
    *it = *(p++);
  }

  // clear encrypted flag
  raw[0] &= ~0x08;

  ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): authenticated decryption passed.");
  ESP_LOGVV(TAG, "  Plaintext : %s", hexencode(raw.data() + 11, vector.datasize).c_str());

  mbedtls_ccm_free(&ctx);
  return true;
}

optional<XiaomiParseResult> parse_xiaomi(const esp32_ble_tracker::ESPBTDevice &device) {
  XiaomiParseResult result;
  bool success = false;
  for (auto &service_data : device.get_service_datas()) {
    if (parse_xiaomi_service_data(result, service_data))
      success = true;
  }
  if (!success) {
    return {};
  }

  const char *name = "HHCCJCY01";
  if (result.type == XiaomiParseResult::TYPE_LYWSDCGQ) {
    name = "LYWSDCGQ";
  } else if (result.type == XiaomiParseResult::TYPE_LYWSD02) {
    name = "LYWSD02";
  } else if (result.type == XiaomiParseResult::TYPE_CGG1) {
    name = "CGG1";
  } else if (result.type == XiaomiParseResult::TYPE_LYWSD03MMC) {
    name = "LYWSD03MMC";
  }

  ESP_LOGD(TAG, "Got Xiaomi %s (%s):", name, device.address_str().c_str());

  if (result.temperature.has_value()) {
    ESP_LOGD(TAG, "  Temperature: %.1f°C", result.temperature.value());
  }
  if (result.humidity.has_value()) {
    ESP_LOGD(TAG, "  Humidity: %.1f%%", result.humidity.value());
  }
  if (result.battery_level.has_value()) {
    ESP_LOGD(TAG, "  Battery Level: %.0f%%", result.battery_level.value());
  }
  if (result.conductivity.has_value()) {
    ESP_LOGD(TAG, "  Conductivity: %.0fµS/cm", result.conductivity.value());
  }
  if (result.illuminance.has_value()) {
    ESP_LOGD(TAG, "  Illuminance: %.0flx", result.illuminance.value());
  }
  if (result.moisture.has_value()) {
    ESP_LOGD(TAG, "  Moisture: %.0f%%", result.moisture.value());
  }

  return result;
}

bool XiaomiListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  auto res = parse_xiaomi(device);

  if (!res.has_value()) {
    return false;
  }

  return true;
}

}  // namespace xiaomi_ble
}  // namespace esphome

#endif
