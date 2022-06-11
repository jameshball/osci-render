--
-- .lua files can be used to make your own custom audio sources!
-- Lua documentation: https://www.lua.org/docs.html
--
-- All variables are saved between calls to this script.
--
-- Below is a simple example of an audio effect that makes a
-- nice visual on an oscilloscope and shows off some of the
-- functionality available.
--
-- The variable 'step' used below is incremented with every
-- call to this script, starting at 1.
--

-- sets 'theta' to 0 initially, or the previous value of
-- 'theta' the last time this script ran
theta = theta or 0

-- updates 'theta' using 'step'
theta = theta + math.sqrt(step) / 1000000000

-- 'slider_a', 'slider_b', ..., 'slider_e' are controlled by
-- the respective sliders in the .lua file settings
left_scale = 0.3 * slider_a
right_scale = 0.3 * slider_b

-- Returns audio samples that will be played back
return {
  -- left audio channel
  left_scale * math.tan(theta * step),
  -- right audio channel
  right_scale * math.tan(theta * step + math.pi / 2)
}