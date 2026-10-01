"""Light platform for Fastcon BLE lights."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.automation as automation

from esphome.components import light
from esphome.const import CONF_COLOR_INTERLOCK, CONF_LIGHT_ID, CONF_OUTPUT_ID

from .fastcon_controller import FastconController


CONF_SUPPORTS_CWWW = "supports_cwww"
CONF_CONTROLLER_ID = "controller_id"

CONF_SPEED = "speed"
CONF_COLOR = "color"

fastcon_ns = cg.esphome_ns.namespace("fastcon")

FastconLight = fastcon_ns.class_(
    "FastconLight",
    light.LightOutput,
    cg.Component,
)

SimpleColorFadeAction = fastcon_ns.class_(
    "SimpleColorFadeAction",
    automation.Action,
)


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA
    .extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID):
                cv.declare_id(FastconLight),

            cv.Required(CONF_LIGHT_ID):
                cv.int_range(min=1, max=255),

            cv.Optional(
                CONF_CONTROLLER_ID,
                default="fastcon_controller",
            ):
                cv.use_id(FastconController),

            cv.Optional(
                CONF_SUPPORTS_CWWW,
                default=False,
            ):
                cv.boolean,

            cv.Optional(
                CONF_COLOR_INTERLOCK,
                default=False,
            ):
                cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_OUTPUT_ID],
        config[CONF_LIGHT_ID],
    )

    await cg.register_component(var, config)
    await light.register_light(var, config)

    if config.get(CONF_COLOR_INTERLOCK):
        cg.add(var.set_color_interlock(True))

    controller = await cg.get_variable(
        config[CONF_CONTROLLER_ID]
    )

    cg.add(var.set_controller(controller))

    if config.get(CONF_SUPPORTS_CWWW):
        cg.add(var.set_supports_cwww(True))


# -----------------------------------------------------------------------------
# BRmesh Simple Color Fade
# -----------------------------------------------------------------------------

BRMESH_COLORS = {
    "blue": 0x01,
    "red": 0x02,
    "magenta": 0x03,
    "green": 0x04,
    "cyan": 0x05,
    "yellow": 0x06,
    "white": 0x07,
}


@automation.register_action(
    "fastcon.simple_color_fade",
    SimpleColorFadeAction,
    cv.Schema(
        {
            cv.Required(CONF_CONTROLLER_ID):
                cv.use_id(FastconController),

            cv.Required(CONF_LIGHT_ID):
                cv.int_range(min=1, max=255),

            cv.Required(CONF_SPEED):
                cv.int_range(min=1, max=100),

            cv.Required(CONF_COLOR):
                cv.enum(BRMESH_COLORS, upper=False),
        }
    ),
)
async def simple_color_fade_to_code(
    config,
    action_id,
    template_arg,
    args,
):
    controller = await cg.get_variable(
        config[CONF_CONTROLLER_ID]
    )

    var = cg.new_Pvariable(
        action_id,
        controller,
    )

    cg.add(
        var.set_light_id(
            config[CONF_LIGHT_ID]
        )
    )

    cg.add(
        var.set_speed(
            config[CONF_SPEED]
        )
    )

    cg.add(
        var.set_color(
            config[CONF_COLOR]
        )
    )

    return var
