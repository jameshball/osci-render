local GRAPH_SIZE = 0.8
local NUM_SEGMENTS = 20
local AXES_REPEATS = 10
local AXES_SEGMENTS = 4 * AXES_REPEATS

local total_segments = NUM_SEGMENTS + AXES_SEGMENTS
local railroad_switch = total_segments * phase / (2 * math.pi)
local current_segment = math.floor(railroad_switch)
local t = railroad_switch % 1

local x, y = 0, 0

if current_segment < AXES_SEGMENTS then
    local axis_pair = math.floor(current_segment / 2) % 2
    
    if axis_pair == 0 then
        x = t * 2 - 1
        y = 0
    else
        x = 0
        y = t * 2 - 1
    end
else
    local curve_t = (current_segment - AXES_SEGMENTS + t) / NUM_SEGMENTS
    x = curve_t * 2 - 1
    -- Graph equation
    y = (math.exp(curve_t * 4 - 2) - math.exp(-2)) / (math.exp(2) - math.exp(-2))
    
    x = x * GRAPH_SIZE
    y = y * GRAPH_SIZE
end

return {x, y}