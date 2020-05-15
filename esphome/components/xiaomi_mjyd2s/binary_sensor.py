import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, binary_sensor, esp32_ble_tracker
from esphome.const import CONF_MAC_ADDRESS, CONF_TIMEOUT, CONF_ID, CONF_BINDKEY, \
    UNIT_PERCENT, ICON_BATTERY, CONF_BATTERY_LEVEL, CONF_ILLUMINANCE, UNIT_LUX, \
    ICON_BRIGHTNESS_5

DEPENDENCIES = ['esp32_ble_tracker']
AUTO_LOAD = ['xiaomi_ble']

xiaomi_mjyd2s_ns = cg.esphome_ns.namespace('xiaomi_mjyd2s')
XiaomiMJYD2S = xiaomi_mjyd2s_ns.class_('XiaomiMJYD2S', binary_sensor.BinarySensor,
                                             cg.Component, esp32_ble_tracker.ESPBTDeviceListener)

CONFIG_SCHEMA = cv.All(binary_sensor.BINARY_SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(XiaomiMJYD2S),
    cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
    cv.Required(CONF_BINDKEY): cv.bind_key,
    cv.Optional(CONF_TIMEOUT, default='5s'): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_ILLUMINANCE): sensor.sensor_schema(UNIT_LUX, ICON_BRIGHTNESS_5, 0),
    cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(UNIT_PERCENT, ICON_BATTERY, 0),
}).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA))


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield esp32_ble_tracker.register_ble_device(var, config)
    yield binary_sensor.register_binary_sensor(var, config)

    cg.add(var.set_address(config[CONF_MAC_ADDRESS].as_hex))
    cg.add(var.set_bindkey(config[CONF_BINDKEY]))
    cg.add(var.set_time(config[CONF_TIMEOUT]))
 
    if CONF_ILLUMINANCE in config:
        sens = yield sensor.new_sensor(config[CONF_ILLUMINANCE])
        cg.add(var.set_illuminance(sens))
    if CONF_BATTERY_LEVEL in config:
        sens = yield sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level(sens))

    cg.add_library("mbedtls", "cdf462088d")
