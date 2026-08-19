import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT

AUTO_LOAD = []
DEPENDENCIES = []
MULTI_CONF = True

CONF_HOST = "host"
CONF_UNIT_ID = "unit_id"
CONF_CONNECT_TIMEOUT = "connect_timeout"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_RETRY_COUNT = "retry_count"
CONF_RETRY_BACKOFF = "retry_backoff"
CONF_KEEPALIVE = "keepalive"

custom_modbus_tcp_ns = cg.esphome_ns.namespace("custom_modbus_tcp")
CustomModbusTcp = custom_modbus_tcp_ns.class_("CustomModbusTcp", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CustomModbusTcp),
        cv.Required(CONF_HOST): cv.string_strict,
        cv.Optional(CONF_PORT, default=502): cv.port,
        cv.Optional(CONF_UNIT_ID, default=1): cv.int_range(min=1, max=247),
        cv.Optional(CONF_CONNECT_TIMEOUT, default="1s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_RESPONSE_TIMEOUT, default="300ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_RETRY_COUNT, default=3): cv.int_range(min=0, max=10),
        cv.Optional(CONF_RETRY_BACKOFF, default="150ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_KEEPALIVE, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_unit_id(config[CONF_UNIT_ID]))
    cg.add(var.set_connect_timeout_ms(config[CONF_CONNECT_TIMEOUT].total_milliseconds))
    cg.add(var.set_response_timeout_ms(config[CONF_RESPONSE_TIMEOUT].total_milliseconds))
    cg.add(var.set_retry_count(config[CONF_RETRY_COUNT]))
    cg.add(var.set_retry_backoff_ms(config[CONF_RETRY_BACKOFF].total_milliseconds))
    cg.add(var.set_keepalive_ms(config[CONF_KEEPALIVE].total_milliseconds))
