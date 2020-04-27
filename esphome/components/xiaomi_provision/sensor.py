import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, esp32_ble_tracker
from esphome.const import CONF_MAC_ADDRESS, CONF_ID

DEPENDENCIES = ['esp32_ble_tracker']
AUTO_LOAD = ['xiaomi_ble']

xiaomi_provision_ns = cg.esphome_ns.namespace('xiaomi_provision')
XiaomiProvision = xiaomi_provision_ns.class_('XiaomiProvision', esp32_ble_tracker.ESPBTDeviceListener,
                                             cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(XiaomiProvision),
    cv.Required(CONF_MAC_ADDRESS): cv.mac_address
}).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_address(config[CONF_MAC_ADDRESS].as_hex))
    cg.add_library("mbedtls", "cdf462088d")
