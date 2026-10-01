"""Number platform for Fastcon BLE effect speed."""

import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
)

from .fastcon_controller import FastconController


CONF_CONTROLLER_ID = "controller_id"
CONF_LIGHT_ID = "light_id"


fastcon_ns = cg.esphome_ns.namespace("fastcon")

FastconSpeedNumber = fastcon_ns.class_(
    "FastconSpeedNumber",
    number.Number,
)


CONFIG_SCHEMA = number.NUMBER_SCHEMA.extend(
    {
        cv.GenerateID():
            cv.declare_id(FastconSpeedNumber),

        cv.Required(
            CONF_CONTROLLER_ID
        ):
            cv.use_id(FastconController),

        cv.Required(
            CONF_LIGHT_ID
        ):
            cv.int_range(
                min=1,
                max=255,
            ),

        cv.Optional(
            CONF_MIN_VALUE,
            default=1.0,
        ):
            cv.float_range(
                min=1.0,
                max=100.0,
            ),

        cv.Optional(
            CONF_MAX_VALUE,
            default=100.0,
        ):
            cv.float_range(
                min=1.0,
                max=100.0,
            ),

        cv.Optional(
            CONF_STEP,
            default=1.0,
        ):
            cv.float_range(
                min=0.1,
                max=100.0,
            ),
    }
)


async def to_code(config):
    controller = await cg.get_variable(
        config[CONF_CONTROLLER_ID]
    )

    var = cg.new_Pvariable(
        config[CONF_ID],
        controller,
        config[CONF_LIGHT_ID],
    )

    await number.register_number(
        var,
        config,
    )

    # Initial value.
    initial_value = config.get(
        "initial_state",
        40.0,
    )

    cg.add(
        controller.set_effect_speed(
            config[CONF_LIGHT_ID],
            cg.uint8_t(
                int(initial_value)
            ),
        )
    )
