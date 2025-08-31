import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Configuration constants
CONF_HOST = "host"
CONF_PORT = "port"
CONF_UNIT_ID = "unit_id"

# Namespace
modbus_tcp_ns = cg.esphome_ns.namespace("modbus_tcp")
ModbusTCPManager = modbus_tcp_ns.class_("ModbusTCPManager", cg.Component)

# Dependencies
DEPENDENCIES = ["network"]
CODEOWNERS = ["@Gucioo"]

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ModbusTCPManager),
    cv.Required(CONF_HOST): cv.string,
    cv.Optional(CONF_PORT, default=502): cv.port,
    cv.Optional(CONF_UNIT_ID, default=1): cv.int_range(min=1, max=255),
}).extend(cv.COMPONENT_SCHEMA)

# Action schemas for writing
ModbusTCPWriteAction = modbus_tcp_ns.class_("ModbusTCPWriteAction", cg.Action)
ModbusTCPWriteMultipleAction = modbus_tcp_ns.class_("ModbusTCPWriteMultipleAction", cg.Action)

CONF_REGISTER = "register"
CONF_VALUE = "value"
CONF_REGISTERS = "registers"
CONF_VALUES = "values"

# Write single register action
MODBUS_TCP_WRITE_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(ModbusTCPManager),
    cv.Required(CONF_REGISTER): cv.templatable(cv.positive_int),
    cv.Required(CONF_VALUE): cv.templatable(cv.int_range(min=-32768, max=32767)),
})

# Write multiple registers action  
MODBUS_TCP_WRITE_MULTIPLE_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(ModbusTCPManager),
    cv.Required(CONF_REGISTER): cv.templatable(cv.positive_int),
    cv.Required(CONF_VALUES): cv.All(cv.ensure_list(cv.templatable(cv.int_range(min=-32768, max=32767))), cv.Length(min=1, max=123)),
})

async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_HOST],
        config[CONF_PORT],
        config[CONF_UNIT_ID]
    )
    
    await cg.register_component(var, config)

@cg.register_action("modbus_tcp.write_register", ModbusTCPWriteAction, MODBUS_TCP_WRITE_SCHEMA)
async def modbus_tcp_write_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    
    register_template = await cg.templatable(config[CONF_REGISTER], args, cg.uint16)
    cg.add(var.set_register(register_template))
    
    value_template = await cg.templatable(config[CONF_VALUE], args, cg.int16)
    cg.add(var.set_value(value_template))
    
    return var

@cg.register_action("modbus_tcp.write_multiple_registers", ModbusTCPWriteMultipleAction, MODBUS_TCP_WRITE_MULTIPLE_SCHEMA)
async def modbus_tcp_write_multiple_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    
    register_template = await cg.templatable(config[CONF_REGISTER], args, cg.uint16)
    cg.add(var.set_register(register_template))
    
    values = []
    for value_config in config[CONF_VALUES]:
        value_template = await cg.templatable(value_config, args, cg.int16)
        values.append(value_template)
    cg.add(var.set_values(values))
    
    return var
