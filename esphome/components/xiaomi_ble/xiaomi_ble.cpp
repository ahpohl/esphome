#include "xiaomi_ble.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef ARDUINO_ARCH_ESP32

#include <vector>
#include "mbedtls/ccm.h"

namespace esphome {
namespace xiaomi_ble {

static const char *TAG = "xiaomi_ble";

bool parse_xiaomi_message(const std::vector<uint8_t> &message, XiaomiParseResult &result) {
  result.has_encryption = (message[0] & 0x08) ? true : false;  // update encryption status
  if (result.has_encryption) {
    ESP_LOGVV(TAG, "parse_xiaomi_message(): payload is encrypted, stop reading message.");
    return false;
  }

  // Data point specs
  // Byte 0: type
  // Byte 1: fixed 0x10
  // Byte 2: length
  // Byte 3..3+len-1: data point value

  const uint8_t *raw = message.data() + result.raw_offset;
  const uint8_t *data = raw + 3;
  const uint8_t data_length = raw[2];

  if ((data_length < 1) || (data_length > 4)) {
    ESP_LOGVV(TAG, "parse_xiaomi_message(): payload has wrong size (%d)!", data_length);
    return false;
  }

  switch (raw[0]) {
    case 0x03: {  // motion, 1 byte, 8-bit unsigned integer
      if (data_length != 1)
        return false;
      result.motion = data[0];
      break;
    }
    case 0x04: {  // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
      if (data_length != 2)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.temperature = temperature / 10.0f;
      break;
    }
    case 0x06: {  // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
      if (data_length != 2)
        return false;
      const int16_t humidity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.humidity = humidity / 10.0f;
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
    case 0x09: {  // conductivity, 2 bytes, 16-bit unsigned integer (LE), 1 µS/cm
      if (data_length != 2)
        return false;
      const uint16_t conductivity = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.conductivity = conductivity;
      break;
    }
    case 0x0A: {  // battery, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.battery_level = data[0];
      break;
    }
    case 0x0D: {  // temperature+humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
      if (data_length != 4)
        return false;
      const int16_t temperature = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      const int16_t humidity = uint16_t(data[2]) | (uint16_t(data[3]) << 8);
      result.temperature = temperature / 10.0f;
      result.humidity = humidity / 10.0f;
      break;
    }
    case 0x10: {  // formaldehyde, 2 bytes, 16-bit unsigned integer (LE), 0.01 mg / m3
      if (data_length != 2)
        return false;
      const uint16_t formaldehyde = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
      result.formaldehyde = formaldehyde / 100.0f;
      break;
    }
    case 0x12: {  // on/off state, 1 byte, 8-bit unsigned integer
      if (data_length != 1)
        return false;
      result.state = data[0];
      break;
    }
    case 0x13: {  // mosquito tablet, 1 byte, 8-bit unsigned integer, 1 %
      if (data_length != 1)
        return false;
      result.tablet = data[0];
      break;
    }
    default:
      return false;
  }

  return true;
}

optional<XiaomiParseResult> parse_xiaomi_header(const esp32_ble_tracker::ServiceData &service_data) {
  XiaomiParseResult result;
  if (!service_data.uuid.contains(0x95, 0xFE)) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): no service data UUID magic bytes.");
    return {};
  }

  auto raw = service_data.data;
  result.has_data = (raw[0] & 0x40) ? true : false;
  result.has_capability = (raw[0] & 0x20) ? true : false;
  result.has_encryption = (raw[0] & 0x08) ? true : false;

  if (!result.has_data) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): service data has no DATA flag.");
    return {};
  }

  static uint8_t last_frame_count = 0;
  if (last_frame_count == raw[4]) {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): duplicate data packet received (%d).", static_cast<int>(last_frame_count));
    result.is_duplicate = true;
    return {};
  }
  last_frame_count = raw[4];
  result.is_duplicate = false;
  result.raw_offset = result.has_capability ? 12 : 11;  // TODO: needs checking !!!

  if ((raw[2] == 0x98) && (raw[3] == 0x00)) {  // MiFlora
    result.type = XiaomiParseResult::TYPE_HHCCJCY01;
    result.name = "HHCCJCY01";
  } else if ((raw[2] == 0xaa) && (raw[3] == 0x01)) {  // round body, segment LCD
    result.type = XiaomiParseResult::TYPE_LYWSDCGQ;
    result.name = "LYWSDCGQ";
  } else if ((raw[2] == 0x5d) && (raw[3] == 0x01)) {  // FlowerPot, RoPot
    result.type = XiaomiParseResult::TYPE_HHCCPOT002;
    result.name = "HHCCPOT002";
  } else if ((raw[2] == 0xdf) && (raw[3] == 0x02)) {  // Xiaomi (Honeywell) formaldehyde sensor, OLED display
    result.type = XiaomiParseResult::TYPE_JQJCY01YM;
    result.name = "JQJCY01YM";
  } else if ((raw[2] == 0xdd) && (raw[3] == 0x03)) {  // Philips/Xiaomi BLE nightlight
    result.type = XiaomiParseResult::TYPE_MUE4094RT;
    result.name = "MUE4094RT";
    result.raw_offset -= 6;  // no MAC address
  } else if ((raw[2] == 0x47) && (raw[3] == 0x03)) {  // round body, e-ink display
    result.type = XiaomiParseResult::TYPE_CGG1;
    result.name = "CGG1";
  } else if ((raw[2] == 0xbc) && (raw[3] == 0x03)) {  // VegTrug Grow Care Garden
    result.type = XiaomiParseResult::TYPE_GCLS002;
    result.name = "GCLS002";
  } else if ((raw[2] == 0x5b) && (raw[3] == 0x04)) {  // rectangular body, e-ink display
    result.type = XiaomiParseResult::TYPE_LYWSD02;
    result.name = "LYWSD02";
  } else if ((raw[2] == 0x0a) && (raw[3] == 0x04)) {  // Mosquito Repellent Smart Version
    result.type = XiaomiParseResult::TYPE_WX08ZM;
    result.name = "WX08ZM";
  } else if ((raw[2] == 0x76) && (raw[3] == 0x05)) {  // Cleargrass (Qingping) alarm clock, segment LCD
    result.type = XiaomiParseResult::TYPE_CGD1;
    result.name = "CGD1";
  } else if ((raw[2] == 0x5b) && (raw[3] == 0x05)) {  // small square body, segment LCD, encrypted
    result.type = XiaomiParseResult::TYPE_LYWSD03MMC;
    result.name = "LYWSD03MMC";
  } else {
    ESP_LOGVV(TAG, "parse_xiaomi_header(): unknown device, no magic bytes.");
    return {};
  }

  return result;
}

bool decrypt_xiaomi_payload(std::vector<uint8_t> &raw, const uint8_t *bindkey) {
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
    ESP_LOGVV(TAG, "       Packet : %s", hexencode(raw.data(), raw.size()).c_str());
    ESP_LOGVV(TAG, "          Key : %s", hexencode(vector.key, vector.keysize).c_str());
    ESP_LOGVV(TAG, "           Iv : %s", hexencode(vector.iv, vector.ivsize).c_str());
    ESP_LOGVV(TAG, "       Cipher : %s", hexencode(vector.ciphertext, vector.datasize).c_str());
    ESP_LOGVV(TAG, "          Tag : %s", hexencode(vector.tag, vector.tagsize).c_str());
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
  ESP_LOGVV(TAG, "  Plaintext : %s, Packet : %d", hexencode(raw.data() + 11, vector.datasize).c_str(),
            static_cast<int>(raw[4]));

  mbedtls_ccm_free(&ctx);
  return true;
}

bool report_xiaomi_results(const optional<XiaomiParseResult> &result, const std::string &address) {
  if (!result.has_value()) {
    ESP_LOGVV(TAG, "report_xiaomi_results(): no results available.");
    return false;
  }

  ESP_LOGD(TAG, "Got Xiaomi %s (%s):", result->name.c_str(), address.c_str());

  if (result->temperature.has_value()) {
    ESP_LOGD(TAG, "  Temperature: %.1f°C", *result->temperature);
  }
  if (result->humidity.has_value()) {
    ESP_LOGD(TAG, "  Humidity: %.1f%%", *result->humidity);
  }
  if (result->battery_level.has_value()) {
    ESP_LOGD(TAG, "  Battery Level: %.0f%%", *result->battery_level);
  }
  if (result->conductivity.has_value()) {
    ESP_LOGD(TAG, "  Conductivity: %.0fµS/cm", *result->conductivity);
  }
  if (result->illuminance.has_value()) {
    ESP_LOGD(TAG, "  Illuminance: %.0flx", *result->illuminance);
  }
  if (result->moisture.has_value()) {
    ESP_LOGD(TAG, "  Moisture: %.0f%%", *result->moisture);
  }
  if (result->tablet.has_value()) {
    ESP_LOGD(TAG, "  Mosquito tablet: %.0f%%", *result->tablet);
  }
  if (result->state.has_value()) {
    ESP_LOGD(TAG, "  On/off state: %.0f", *result->state);
  }
  if (result->motion.has_value()) {
    ESP_LOGD(TAG, "  Motion: %.0f", *result->motion);
  }

  return true;
}

bool XiaomiListener::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  // Previously the message was parsed twice per packet, once by XiaomiListener::parse_device()
  // and then again by the respective device class's parse_device() function. Parsing the header
  // here and then for each device seems to be unneccessary and complicates the duplicate packet filtering.
  // Hence I disabled the call to parse_xiaomi_header() here and the message parsing is done entirely
  // in the respecive device instance. The XiaomiListener class is defined in __init__.py and I was not
  // able to remove it entirely.

  return false;  // with true it's not showing device scans
}

}  // namespace xiaomi_ble
}  // namespace esphome

#endif
