#include "xiaomi_ble.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include <vector>
#include <algorithm>
#include <string.h>
#include <mbedtls/ccm.h>
#include <mbedtls/error.h>

#ifdef ARDUINO_ARCH_ESP32

namespace esphome {
namespace xiaomi_ble {

static const char *TAG = "xiaomi_ble";

bool parse_xiaomi_message(std::vector<uint8_t> &raw, XiaomiParseResult &result) {

  // Data point specs
  // Byte 0: type
  // Byte 1: fixed 0x10
  // Byte 2: length
  // Byte 3..3+len-1: data point value

  uint8_t *raw_data = raw.data() + result.raw_offset;

  // ESP_LOGD(TAG, "parse_xiaomi_data_byte(): type %02x, data %s", raw_data[0],
  //         hexencode(raw_data + 3, raw_data[2]).c_str());

  uint8_t data_type = raw_data[0];
  uint8_t data_length = raw_data[2]);
  uint8_t data[7] = {0};

  if ((data_length < 1) || (data_length > 4)) {
     ESP_LOGVV(TAG, "parse_xiaomi_data_byte(): payload has wrong size (%d)!", data_length);
     return false;
  }

  memcpy(data, raw_data, data_length);
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

  return false;
}

optional<XiaomiParseResult> parse_xiaomi_header(const esp32_ble_tracker::ESPBTDevice &device) {
  XiaomiParseResult result;

  esp32_ble_tracker::ServiceData service_data = device.get_service_data();
  ESP_LOGVV(TAG, "parse_xiaomi_header(): %s", hexencode(service_data.data.data(),
                                                                   service_data.data.size()).c_str());

  if (!service_data.uuid.contains(0x95, 0xFE)) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): no service data UUID magic bytes");
    return {};
  }

  auto raw = service_data.data;

  if (raw.size() < 14) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): service data too short (%d)!", raw.size());
    return {};
  }

  bool is_lywsdcgq = (raw[1] & 0x20) == 0x20 && raw[2] == 0xAA && raw[3] == 0x01;
  bool is_hhccjcy01 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x98 && raw[3] == 0x00;
  bool is_lywsd02 = (raw[1] & 0x20) == 0x20 && raw[2] == 0x5b && raw[3] == 0x04;
  bool is_cgg1 = ((raw[1] & 0x30) == 0x30 || (raw[1] & 0x20) == 0x20) && raw[2] == 0x47 && raw[3] == 0x03;
  bool is_lywsd03mmc = (raw[1] & 0x58) == 0x58 && raw[2] == 0x5b && raw[3] == 0x05;

  if (!is_lywsdcgq && !is_hhccjcy01 && !is_lywsd02 && !is_cgg1 && !is_lywsd03mmc) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): no magic bytes");
    return {};
  }

  result.raw_offset = is_lywsdcgq || is_cgg1 || is_lywsd03mmc ? 11 : 12;

  result.has_data = (raw[0] & 0x40) ? true : false;
  result.has_capability = (raw[0] & 0x20) ? true : false;
  result.has_encryption = (raw[0] & 0x08) ? true : false;

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

  return result;
}

bool XiaomiListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  auto res = parse_xiaomi_header(device);
  if (!res.has_value()) {
    return false;
  }

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

bool decrypt_xiaomi_payload(std::vector<uint8_t> &t_raw, const uint8_t *t_bindkey) {
  if (!(t_raw[0] & 0x40)) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): not a data frame!");
    ESP_LOGVV(TAG, "Packet : %s", hexencode(t_raw.data(), t_raw.size()).c_str());
    return false;
  }
  if ((t_raw[0] & 0x20) && !(t_raw[0] & 0x08)) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): data packet is not encrypted!");
    ESP_LOGVV(TAG, "Packet : %s", hexencode(t_raw.data(), t_raw.size()).c_str());
    return false;
  }
  if ((t_raw.size() < 22) || (t_raw.size() > 23)) {
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): data packet has wrong size (%d)!", t_raw.size());
    ESP_LOGVV(TAG, "Packet : %s", hexencode(t_raw.data(), t_raw.size()).c_str());
    return false;
  }

  AESVector_t vector = {.name = "LYWSD03MMC AES-128 CCM",
                        .key = {0},
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
  if (t_raw.size() == 23) {
    vector.datasize = 5;  // temperature or humidity
    offset = 1;
  }

  const uint8_t *v = t_raw.data();
  memcpy(vector.key, t_bindkey, vector.keysize);
  memcpy(vector.ciphertext, v + 11, vector.datasize);
  memcpy(vector.tag, v + 18 + offset, vector.tagsize);
  memcpy(vector.iv, v + 5, 6);                // MAC address reversed
  memcpy(vector.iv + 6, v + 2, 3);            // sensor type (2) + packet id (1)
  memcpy(vector.iv + 9, v + 15 + offset, 3);  // payload counter

  uint8_t mac_address[6] = {0};
  memcpy(mac_address, v + 10, 1);
  memcpy(mac_address + 1, v + 9, 1);
  memcpy(mac_address + 2, v + 8, 1);
  memcpy(mac_address + 3, v + 7, 1);
  memcpy(mac_address + 4, v + 6, 1);
  memcpy(mac_address + 5, v + 5, 1);

  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);

  int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, vector.key, vector.keysize * 8);
  if (ret) {
    char err[100] = {0};
    mbedtls_strerror(ret, err, 99);
    ESP_LOGD(TAG, "%s.", err);
    mbedtls_ccm_free(&ctx);
    return false;
  }

  ret = mbedtls_ccm_auth_decrypt(&ctx, vector.datasize, vector.iv, vector.ivsize, vector.authdata, vector.authsize,
                                 vector.ciphertext, vector.plaintext, vector.tag, vector.tagsize);
  if (ret) {
    ESP_LOGVV(TAG, "Name : %s", vector.name);
    ESP_LOGVV(TAG, "MAC address : %s", hexencode(mac_address, 6).c_str());
    ESP_LOGVV(TAG, "Packet : %s", hexencode(t_raw.data(), t_raw.size()).c_str());
    ESP_LOGVV(TAG, "Key : %s", hexencode(vector.key, vector.keysize).c_str());
    ESP_LOGVV(TAG, "Iv : %s", hexencode(vector.iv, vector.ivsize).c_str());
    ESP_LOGVV(TAG, "Cipher : %s", hexencode(vector.ciphertext, vector.datasize).c_str());
    ESP_LOGVV(TAG, "Tag : %s", hexencode(vector.tag, vector.tagsize).c_str());
    ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): authenticated decryption failed.");
    mbedtls_ccm_free(&ctx);
    return false;
  }

  // replace encrypted payload with plaintext
  uint8_t *p = vector.plaintext;
  for (std::vector<uint8_t>::iterator it = t_raw.begin() + 11; it != t_raw.begin() + 11 + vector.datasize; ++it) {
    *it = *(p++);
  }

  // clear encrypted flag
  t_raw[0] &= ~0x08;

  ESP_LOGVV(TAG, "decrypt_xiaomi_payload(): authenticated decryption passed.");
  ESP_LOGVV(TAG, "Plaintext : %s", hexencode(t_raw.data()+11, vector.datasize).c_str());

  mbedtls_ccm_free(&ctx);
  return true;
}

}  // namespace xiaomi_ble
}  // namespace esphome

#endif
