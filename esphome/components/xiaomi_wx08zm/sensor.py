import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, esp32_ble_tracker
from esphome.const import CONF_BATTERY_LEVEL, CONF_MAC_ADDRESS, CONF_TABLET, CONF_STATE, \
    UNIT_PERCENT, UNIT_EMPTY, ICON_BUG, ICON_LIGHTBULB, ICON_BATTERY, CONF_ID
    

DEPENDENCIES = ['esp32_ble_tracker']
AUTO_LOAD = ['xiaomi_ble']

xiaomi_wx08zm_ns = cg.esphome_ns.namespace('xiaomi_wx08zm')
XiaomiWX08ZM = xiaomi_wx08zm_ns.class_('XiaomiWX08ZM', esp32_ble_tracker.ESPBTDeviceListener,
                                       cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(XiaomiWX08ZM),
    cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
    cv.Optional(CONF_TABLET): sensor.sensor_schema(UNIT_PERCENT, ICON_BUG, 0),
    cv.Optional(CONF_STATE): sensor.sensor_schema(UNIT_EMPTY, ICON_LIGHTBULB, 0),
    cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(UNIT_PERCENT, ICON_BATTERY, 0),
}).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_address(config[CONF_MAC_ADDRESS].as_hex))

    if CONF_TABLET in config:
        sens = yield sensor.new_sensor(config[CONF_TABLET])
        cg.add(var.set_tablet(sens))
    if CONF_STATE in config:
        sens = yield sensor.new_sensor(config[CONF_STATE])
        cg.add(var.set_state(sens))
    if CONF_BATTERY_LEVEL in config:
        sens = yield sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level(sens))
