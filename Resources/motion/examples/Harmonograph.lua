-- A harmonograph: pendulums swing a pen in X and in Y, each swing a
-- little smaller than the last. Each frame draws the whole curve once,
-- as `phase` runs from 0 to 2 pi.
--
-- Sliders change it while it plays (key them, or route a modulator):
--   A detunes one pendulum against the other: the figure's shape
--   B is how quickly the swinging dies away
--   C turns the whole figure

local swings = 20                       -- swings drawn each frame
local along = phase / (2 * math.pi)     -- 0 to 1 along this frame's curve
local s = along * swings * 2 * math.pi

local detune = 1 + slider_a * 0.015
local fade = math.exp(-along * (0.5 + slider_b * 3))

local x = (math.sin(2 * s + 0.4) + math.sin(3 * detune * s)) * 0.5 * fade
local y = (math.sin(3 * s) + math.sin(2 * detune * s + 1.2)) * 0.5 * fade

local turn = slider_c * 2 * math.pi
return {
  x * math.cos(turn) - y * math.sin(turn),
  x * math.sin(turn) + y * math.cos(turn)
}
