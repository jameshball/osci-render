local X_SIZE = 0.35
local STEM_WIDTH = 0.05
local Y_SIZE = 0.02
local HEIGHT = 2.0
local OFFSET_Y = -1.0
local LOOPS = 50
local SHROOMS = 3 -- can be 1-4

local MOVE_AMOUNT = 0.4
local MOVE_FREQ = 0.3
local SPREAD = 0.2

local railroad_switch = SHROOMS * phase / (2 * math.pi)
local drawing_phase = (phase * SHROOMS) % (2 * math.pi)

local x = X_SIZE * math.cos(LOOPS * drawing_phase)
local y = Y_SIZE * math.sin(LOOPS * drawing_phase)

local sawtooth = (drawing_phase % (2 * math.pi)) / (2 * math.pi)
y = y + (HEIGHT * sawtooth) + OFFSET_Y

local sine_mod = math.sin(2 * math.pi * sawtooth)

if sawtooth < 0.75 then
    sine_mod = 1
    x = x * (STEM_WIDTH / X_SIZE)
end

x = x * sine_mod

local base_time = step/sample_rate * MOVE_FREQ
local current_mushroom = math.floor(railroad_switch)
local phase_offset = current_mushroom / SHROOMS

local wiggle = math.sin(2 * math.pi * (sawtooth + base_time + phase_offset)) * sawtooth
local outer_drift = SPREAD * sawtooth * wiggle
x = x + (MOVE_AMOUNT * wiggle + outer_drift)

return {x, y}