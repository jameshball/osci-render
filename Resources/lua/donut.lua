local NUM_LATITUDE_LINES = 16
local NUM_LONGITUDE_LINES = 24
local MAJOR_RADIUS = 0.6
local MINOR_RADIUS = 0.2
local ROTATION_SPEED = 0.2

local function rotateX(x, y, z, angle)
    return x,
           y * math.cos(angle) - z * math.sin(angle),
           y * math.sin(angle) + z * math.cos(angle)
end

local function rotateY(x, y, z, angle)
    return x * math.cos(angle) + z * math.sin(angle),
           y,
           -x * math.sin(angle) + z * math.cos(angle)
end

-- u: angle around the tube (0 to 2π)
-- v: angle around the center (0 to 2π)
local function torus_point(u, v)
    local x = (MAJOR_RADIUS + MINOR_RADIUS * math.cos(u)) * math.cos(v)
    local y = (MAJOR_RADIUS + MINOR_RADIUS * math.cos(u)) * math.sin(v)
    local z = MINOR_RADIUS * math.sin(u)
    return x, y, z
end

local total_lines = NUM_LATITUDE_LINES + NUM_LONGITUDE_LINES
local railroad_switch = total_lines * phase / (2 * math.pi)
local drawing_phase = (phase * total_lines) % (2 * math.pi)

local time = step/sample_rate * ROTATION_SPEED

if railroad_switch < NUM_LATITUDE_LINES then
    local current_line = math.floor(railroad_switch)
    local u = (current_line / NUM_LATITUDE_LINES) * 2 * math.pi
    local v = drawing_phase
    
    local x, y, z = torus_point(u, v)
    
    x, y, z = rotateX(x, y, z, 1)
    
    return {x, y, z}

else
    local current_line = math.floor(railroad_switch - NUM_LATITUDE_LINES)
    local v = (current_line / NUM_LONGITUDE_LINES) * 2 * math.pi
    local u = drawing_phase
    
    local x, y, z = torus_point(u, v)
    
    x, y, z = rotateX(x, y, z, 1)
    
    return {x, y, z}
end