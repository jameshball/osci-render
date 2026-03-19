-- This is a simple example of how to use Lua
-- with osci-render.
--
-- Here, we make a simple spiral effect, that
-- can be controlled using the Lua Sliders and
-- can be played using MIDI.
--
-- Lua Slider values in osci-render can be
-- referenced using the name of the slider.
-- e.g. Lua Slider A is in a variable called
-- slider_a.

-- Globals persist across samples, so we only
-- need to initialise them once.
if not lfo then
    lfo = 0
    t = 0
    dir = 1
end

-- Increment lfo using the value of slider_b
-- to control how quickly it increases.
lfo = lfo + slider_b / sample_rate + 0.1 / sample_rate

-- Use slider_a along with lfo to define a
-- variable that controls the length of
-- the spiral
local spiral_length = (slider_a + 0.5) * 10 * math.sin(lfo)

-- This is the correct increment for t to use such
-- that we hear the right frequency.
local increment = 4 * math.pi * frequency / sample_rate

-- If we get to the end of the spiral, flip the
-- direction to go back.
if t > 2 * math.pi or t < 0 then
  dir = -dir
end

-- Increment t in the current direction
t = t + dir * increment

-- Return a point (left and right audio channel,
-- or x and y coordinate) that draws the spiral
-- at time, t.
return {
  0.1 * t * math.sin(spiral_length * t),
  0.1 * t * math.cos(spiral_length * t)
}
