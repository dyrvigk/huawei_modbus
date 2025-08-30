import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_EMPTY,
    STATE_CLASS_MEASUREMENT,
)

# Define constants
CONF_HOST = "host"
CONF_PORT = "port"
CONF_REGISTER_ADDRESS = "register_address"
CONF_BYTE_ORDER = "byte_order"
CONF_ACCURACY_DECIMALS = "accuracy_decimals"

# Import from the main component
modbus_tcp_ns = cg.esphome_ns.namespace("modbus_tcp")
ModbusTCPSensor = modbus_tcp_ns.class_("ModbusTCPSensor", cg.PollingComponent, sensor.Sensor)

# Dependencies
DEPENDENCIES = ["network"]

# Configuration schema
CONFIG_SCHEMA = sensor.sensor_schema(
    ModbusTCPSensor,
    device_class=DEVICE_CLASS_EMPTY,
    state_class=STATE_CLASS_MEASUREMENT,
).extend({
    cv.Required(CONF_HOST): cv.string,
    cv.Optional(CONF_PORT, default=502): cv.port,
    cv.Required(CONF_REGISTER_ADDRESS): cv.positive_int,
    cv.Optional(CONF_BYTE_ORDER, default="AB_CD"): cv.one_of("AB_CD", "CD_AB", "DC_BA", upper=True),
    cv.Optional(CONF_ACCURACY_DECIMALS, default=2): cv.int_range(min=0, max=6),
    cv.Optional(CONF_UPDATE_INTERVAL, default="60s"): cv.update_interval,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_HOST],
        config[CONF_PORT],
        config[CONF_REGISTER_ADDRESS],
        config[CONF_BYTE_ORDER],
        config[CONF_UPDATE_INTERVAL].total_milliseconds,
    )
    
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    if CONF_ACCURACY_DECIMALS in config:
        cg.add(var.set_accuracy_decimals(config[CONF_ACCURACY_DECIMALS]))
