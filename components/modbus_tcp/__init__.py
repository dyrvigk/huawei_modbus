import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the namespace and component class
modbus_tcp_ns = cg.esphome_ns.namespace("modbus_tcp")
ModbusTCPComponent = modbus_tcp_ns.class_("ModbusTCPComponent", cg.Component)

# Dependencies - requires network for TCP connectivity
DEPENDENCIES = ["network"]
CODEOWNERS = ["@Gucioo"]

# Configuration schema for the main component
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ModbusTCPComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
