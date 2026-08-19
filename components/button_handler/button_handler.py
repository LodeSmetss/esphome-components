import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID

DEPENDENCIES = ["custom_modbus_tcp"]
MULTI_CONF = True

CONF_MODBUS_TCP_ID = "modbus_tcp_id"
CONF_REGISTER_TYPE = "register_type"
CONF_REGISTER_START = "register_start"
CONF_REGISTER_COUNT = "register_count"
CONF_POLL_INTERVAL = "poll_interval"
CONF_TIMINGS = "timings"
CONF_BUTTONS = "buttons"
CONF_BUTTON_ID = "button_id"
CONF_ADDRESSES = "addresses"
CONF_LABELS = "labels"
CONF_DEBOUNCE = "debounce"
CONF_LONG_PRESS = "long_press"
CONF_COLLECT_WINDOW = "collect_window"
CONF_INVALID_COOLDOWN = "invalid_cooldown"
CONF_ON_SINGLE = "on_single"
CONF_ON_DOUBLE = "on_double"
CONF_ON_LONG_PRESS = "on_long_press"
CONF_ON_LONG_RELEASE = "on_long_release"
CONF_ON_COMBO = "on_combo"
CONF_ON_EVENT = "on_event"

button_handler_ns = cg.esphome_ns.namespace("button_handler")
custom_modbus_tcp_ns = cg.esphome_ns.namespace("custom_modbus_tcp")

ButtonHandler = button_handler_ns.class_("ButtonHandler", cg.Component)
ButtonHandlerButton = button_handler_ns.class_("ButtonHandlerButton")
CustomModbusTcp = custom_modbus_tcp_ns.class_("CustomModbusTcp")

SingleTrigger = button_handler_ns.class_("SingleTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
DoubleTrigger = button_handler_ns.class_("DoubleTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
LongPressTrigger = button_handler_ns.class_("LongPressTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
LongReleaseTrigger = button_handler_ns.class_("LongReleaseTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
ComboTrigger = button_handler_ns.class_("ComboTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
EventTrigger = button_handler_ns.class_("EventTrigger", automation.Trigger.template(cg.std_string, cg.std_string, cg.std_string))

TIMINGS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_DEBOUNCE, default="20ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_LONG_PRESS, default="500ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_COLLECT_WINDOW, default="100ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INVALID_COOLDOWN, default="100ms"): cv.positive_time_period_milliseconds,
    }
)

BUTTON_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ButtonHandlerButton),
        cv.Required(CONF_BUTTON_ID): cv.string_strict,
        cv.Required(CONF_ADDRESSES): cv.All(
            cv.ensure_list(cv.int_range(min=1, max=2048)),
            cv.Length(min=1, max=4),
        ),
        cv.Optional(CONF_LABELS, default={}): cv.Schema({
            cv.int_range(min=1, max=2048): cv.string_strict,
        }),
        cv.Optional(CONF_ON_SINGLE): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_DOUBLE): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_LONG_PRESS): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_LONG_RELEASE): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_COMBO): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_EVENT): automation.validate_automation(single=True),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ButtonHandler),
        cv.Required(CONF_MODBUS_TCP_ID): cv.use_id(CustomModbusTcp),
        cv.Optional(CONF_REGISTER_TYPE, default="holding"): cv.one_of("holding", "input", "discrete", lower=True),
        cv.Optional(CONF_REGISTER_START, default=0): cv.int_range(min=0, max=65535),
        cv.Optional(CONF_REGISTER_COUNT, default=7): cv.int_range(min=1, max=125),
        cv.Optional(CONF_POLL_INTERVAL, default="10ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TIMINGS, default={}): TIMINGS_SCHEMA,

        # Automation called for events from every button
        cv.Optional(CONF_ON_EVENT): automation.validate_automation(single=True),

        cv.Required(CONF_BUTTONS): cv.ensure_list(BUTTON_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    modbus = await cg.get_variable(config[CONF_MODBUS_TCP_ID])
    cg.add(var.set_modbus_tcp(modbus))

    cg.add(var.set_register_type(config[CONF_REGISTER_TYPE]))
    cg.add(var.set_register_start(config[CONF_REGISTER_START]))
    cg.add(var.set_register_count(config[CONF_REGISTER_COUNT]))
    cg.add(var.set_poll_interval_ms(config[CONF_POLL_INTERVAL].total_milliseconds))

    timings = config[CONF_TIMINGS]
    cg.add(var.set_debounce_ms(timings[CONF_DEBOUNCE].total_milliseconds))
    cg.add(var.set_long_press_ms(timings[CONF_LONG_PRESS].total_milliseconds))
    cg.add(var.set_collect_window_ms(timings[CONF_COLLECT_WINDOW].total_milliseconds))
    cg.add(var.set_invalid_cooldown_ms(timings[CONF_INVALID_COOLDOWN].total_milliseconds))

    # Global event handler: receives events from all configured buttons
    event_trigger_args = [
        (cg.std_string, "button_id"),
        (cg.std_string, "type"),
        (cg.std_string, "label"),
    ]

    if CONF_ON_EVENT in config:
        await automation.build_automation(
            var.get_event_trigger(),
            event_trigger_args,
            config[CONF_ON_EVENT],
        )

    for button_conf in config[CONF_BUTTONS]:
        btn = cg.new_Pvariable(button_conf[CONF_ID])
        cg.add(btn.set_button_id(button_conf[CONF_BUTTON_ID]))
        cg.add(btn.set_addresses(button_conf[CONF_ADDRESSES]))

        for addr, label in button_conf[CONF_LABELS].items():
            cg.add(btn.add_label(addr, label))

        cg.add(var.add_button(btn))

        trigger_args = [(cg.std_string, "button_id"), (cg.std_string, "label")]
        event_trigger_args = [(cg.std_string, "button_id"), (cg.std_string, "type"), (cg.std_string, "label")]

        if CONF_ON_SINGLE in button_conf:
            await automation.build_automation(btn.get_single_trigger(), trigger_args, button_conf[CONF_ON_SINGLE])
        if CONF_ON_DOUBLE in button_conf:
            await automation.build_automation(btn.get_double_trigger(), trigger_args, button_conf[CONF_ON_DOUBLE])
        if CONF_ON_LONG_PRESS in button_conf:
            await automation.build_automation(btn.get_long_press_trigger(), trigger_args, button_conf[CONF_ON_LONG_PRESS])
        if CONF_ON_LONG_RELEASE in button_conf:
            await automation.build_automation(btn.get_long_release_trigger(), trigger_args, button_conf[CONF_ON_LONG_RELEASE])
        if CONF_ON_COMBO in button_conf:
            await automation.build_automation(btn.get_combo_trigger(), trigger_args, button_conf[CONF_ON_COMBO])
        if CONF_ON_EVENT in button_conf:
            await automation.build_automation(btn.get_event_trigger(), event_trigger_args, button_conf[CONF_ON_EVENT])
