import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, esp32_ble_tracker
from esphome.const import CONF_MAC_ADDRESS, CONF_MOTION, UNIT_EMPTY, ICON_MOTION_SENSOR, CONF_ID


DEPENDENCIES = ['esp32_ble_tracker']
AUTO_LOAD = ['xiaomi_ble']

xiaomi_mue4094rt_ns = cg.esphome_ns.namespace('xiaomi_mue4094rt')
XiaomiMUE4094RT = xiaomi_mue4094rt_ns.class_('XiaomiMUE4094RT',
                                             esp32_ble_tracker.ESPBTDeviceListener, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(XiaomiMUE4094RT),
    cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
    cv.Optional(CONF_MOTION): sensor.sensor_schema(UNIT_EMPTY, ICON_MOTION_SENSOR, 0),
}).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_address(config[CONF_MAC_ADDRESS].as_hex))

    if CONF_MOTION in config:
        sens = yield sensor.new_sensor(config[CONF_MOTION])
        cg.add(var.set_motion(sens))

