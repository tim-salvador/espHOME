"""stair_engine: direction-aware, sectioned stair lighting core for ESPHome.

Layout is declared bottom-to-top. Each section maps a contiguous pixel range onto
whole steps; a landing is a section whose "steps" are step-equivalents.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_NAME

CODEOWNERS = ["@tim-salvador"]
DEPENDENCIES = ["light"]

CONF_NUM_PIXELS = "num_pixels"
CONF_SECTIONS = "sections"
CONF_FIRST_PIXEL = "first_pixel"
CONF_PIXELS = "pixels"
CONF_PIXELS_PER_STEP = "pixels_per_step"
CONF_REVERSED = "reversed"

MAX_PIXELS = 2048
MAX_SECTIONS = 8

stair_engine_ns = cg.esphome_ns.namespace("stair_engine")
StairEngine = stair_engine_ns.class_("StairEngine", cg.Component)

SECTION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_FIRST_PIXEL): cv.int_range(min=0, max=MAX_PIXELS - 1),
        cv.Required(CONF_PIXELS): cv.int_range(min=1, max=MAX_PIXELS),
        cv.Optional(CONF_PIXELS_PER_STEP, default=1): cv.int_range(min=1, max=MAX_PIXELS),
        cv.Optional(CONF_REVERSED, default=False): cv.boolean,
    }
)


def _validate_layout(config):
    total = config[CONF_NUM_PIXELS]
    used = set()
    for sec in config[CONF_SECTIONS]:
        name = sec[CONF_NAME]
        first, count, pps = sec[CONF_FIRST_PIXEL], sec[CONF_PIXELS], sec[CONF_PIXELS_PER_STEP]
        if count % pps:
            raise cv.Invalid(
                f"Section '{name}': pixels ({count}) must be a multiple of pixels_per_step ({pps})"
            )
        if first + count > total:
            raise cv.Invalid(
                f"Section '{name}': pixels {first}-{first + count - 1} extend past num_pixels ({total})"
            )
        span = set(range(first, first + count))
        if overlap := used & span:
            raise cv.Invalid(f"Section '{name}' overlaps another section at pixel {min(overlap)}")
        used |= span
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StairEngine),
            cv.Required(CONF_NUM_PIXELS): cv.int_range(min=1, max=MAX_PIXELS),
            cv.Required(CONF_SECTIONS): cv.All(
                cv.ensure_list(SECTION_SCHEMA), cv.Length(min=1, max=MAX_SECTIONS)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_layout,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_num_pixels(config[CONF_NUM_PIXELS]))
    for sec in config[CONF_SECTIONS]:
        cg.add(
            var.add_section(
                sec[CONF_NAME],
                sec[CONF_FIRST_PIXEL],
                sec[CONF_PIXELS],
                sec[CONF_PIXELS_PER_STEP],
                sec[CONF_REVERSED],
            )
        )
