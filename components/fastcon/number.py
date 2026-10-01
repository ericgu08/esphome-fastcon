"""Fastcon effect speed number platform."""

import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import number
from esphome.const import CONF_INITIAL_STATE

from .fastcon_controller import FastconController

CONF_CONTROLLER_ID = "controller_id"
CONF_LIGHT_ID = "light_id"

fastcon_ns = cg.esphome_ns.namespace("fastcon")

FastconSpeedNumber = fastcon_ns.class_(
    "FastconSpeedNumber",
    number.Number,
)


CONFIG_SCHEMA = number.number_schema(FastconSpeedNumber).extend(
    {
        cv.Required(CONF_CONTROLLER_ID): cv.use_id(FastconController),

        cv.Required(CONF_LIGHT_ID):
            cv.int_range(min=1, max=255),

        cv.Optional(
            CONF_INITIAL_STATE,
            default=40.0,
        ):
            cv.float_range(min=1.0, max=100.0),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    controller = await cg.get_variable(config[CONF_CONTROLLER_ID])

    var = await number.new_number(
        config,
        min_value=1.0,
        max_value=100.0,
        step=1.0,
    )

    cg.add(var.set_controller(controller))
    cg.add(var.set_light_id(config[CONF_LIGHT_ID]))

    await cg.register_component(var, config)
