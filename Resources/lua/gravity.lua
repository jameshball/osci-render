local GRID_SIZE = 1.0
local DEPTH = 1.0
local GRID_LINES = 25
local WELL_RADIUS = 0.5
local ROTATION_SPEED = 0.4

local function rotateY(x, y, z, angle)
    return x * math.cos(angle) + z * math.sin(angle),
           y,
           -x * math.sin(angle) + z * math.cos(angle)
end

local function rotateX(x, y, z, angle)
    return x,
           y * math.cos(angle) - z * math.sin(angle),
           y * math.sin(angle) + z * math.cos(angle)
end

local function calculate_depth(x, z)
    local distance = math.sqrt(x * x + z * z)
    if distance > WELL_RADIUS then
        return 0
    else
        local t = 1 - (distance / WELL_RADIUS)
        return -DEPTH * t * t
    end
end

local total_segments = GRID_LINES * 2
local railroad_switch = total_segments * phase / (2 * math.pi)
local segment_index = math.floor(railroad_switch)
local segment_phase = railroad_switch % 1

local time = step/sample_rate * ROTATION_SPEED

local x, y, z = 0, 0, 0

if segment_index < GRID_LINES then
    local line_index = segment_index
    local z_pos = (line_index / (GRID_LINES - 1) - 0.5) * GRID_SIZE * 2
    local x_pos = (segment_phase - 0.5) * GRID_SIZE * 2
    
    x = x_pos
    z = z_pos
    y = calculate_depth(x_pos, z_pos)
else
    local line_index = segment_index - GRID_LINES
    local x_pos = (line_index / (GRID_LINES - 1) - 0.5) * GRID_SIZE * 2
    local z_pos = (segment_phase - 0.5) * GRID_SIZE * 2
    
    x = x_pos
    z = z_pos
    y = calculate_depth(x_pos, z_pos)
end

x, y, z = rotateX(x, y, z, -0.3)

return {x, y, z}