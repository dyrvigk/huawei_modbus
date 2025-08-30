import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
)

# Configuration constants
CONF_HOST = "host"
CONF_PORT = "port"
CONF_FUNCTIONCODE = "functioncode"
CONF_REGISTER_ADDRESS = "register_address"
CONF_SCALE = "scale"

# Namespace
modbus_tcp_ns = cg.esphome_ns.namespace("modbus_tcp")
ModbusTCP16Sensor = modbus_tcp_ns.class_("ModbusTCP16Sensor", cg.PollingComponent, sensor.Sensor)

# Dependencies
DEPENDENCIES = ["network"]

# Configuration schema
CONFIG_SCHEMA = sensor.sensor_schema(ModbusTCP16Sensor).extend({
    cv.Required(CONF_HOST): cv.string,
    cv.Optional(CONF_PORT, default=502): cv.port,
    cv.Optional(CONF_FUNCTIONCODE, default=3): cv.int_range(min=1, max=4),
    cv.Required(CONF_REGISTER_ADDRESS): cv.positive_int,
    cv.Optional(CONF_SCALE, default=0.1): cv.float_,
    cv.Optional(CONF_UPDATE_INTERVAL, default="30s"): cv.update_interval,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_HOST],
        config[CONF_PORT],
        config[CONF_FUNCTIONCODE],
        config[CONF_REGISTER_ADDRESS],
        config[CONF_SCALE],
        config[CONF_UPDATE_INTERVAL].total_milliseconds,
    )
    
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
