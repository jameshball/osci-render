local LOOPS = 30
local CAP_WIDTH = 0.35
local STEM_WIDTH = 0.05
local HEIGHT = 2.0
local OFFSET_Y = -1.0
local SHROOMS = 3 -- can be 1-4

local MOVE_AMOUNT = 0.4
local MOVE_FREQ = 0.3
local SPREAD = 0.2

local railroad_switch = SHROOMS * phase / (2 * math.pi)
local drawing_phase = (phase * SHROOMS) % (2 * math.pi)

local x = CAP_WIDTH * math.cos(LOOPS * drawing_phase)
local z = CAP_WIDTH * math.sin(LOOPS * drawing_phase)

local sawtooth = (drawing_phase % (2 * math.pi)) / (2 * math.pi)
local y = HEIGHT * sawtooth + OFFSET_Y

local sine_mod = math.sin(2 * math.pi * sawtooth)

if sawtooth < 0.75 then
    sine_mod = STEM_WIDTH / CAP_WIDTH
end

x = x * sine_mod
z = z * sine_mod

local base_time = step/sample_rate * MOVE_FREQ
local current_mushroom = math.floor(railroad_switch)
local phase_offset = current_mushroom / SHROOMS

local wiggle_x = math.cos(2 * math.pi * (sawtooth + base_time + phase_offset)) * sawtooth
local wiggle_z = math.sin(2 * math.pi * (sawtooth + base_time + phase_offset)) * sawtooth
local outer_drift_x = SPREAD * sawtooth * wiggle_x
local outer_drift_z = SPREAD * sawtooth * wiggle_z
x = x + (MOVE_AMOUNT * wiggle_x + outer_drift_x)
z = z + (MOVE_AMOUNT * wiggle_z + outer_drift_z)

-- normalize for default HEIGHT and OFFSET_Y
local max_xz = math.abs(MOVE_AMOUNT) + math.abs(SPREAD)
local scale = 1 / math.sqrt(1 + max_xz * max_xz)

return {x * scale, y * scale, z * scale}
