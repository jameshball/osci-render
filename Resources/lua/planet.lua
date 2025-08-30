local RADIUS = 0.4
local NUM_LATITUDE_LINES = 12
local NUM_LONGITUDE_LINES = 12
local NUM_RINGS = 3
local RING_SPACING = 0.15
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

local total_shapes = NUM_LATITUDE_LINES + NUM_LONGITUDE_LINES + (NUM_RINGS * 3)
local railroad_switch = total_shapes * phase / (2 * math.pi)
local drawing_phase = (phase * total_shapes) % (2 * math.pi)

local time_based_rotation = step/sample_rate * ROTATION_SPEED

if railroad_switch < NUM_LATITUDE_LINES then
    local current_line = math.floor(railroad_switch)
    local latitude = (current_line / (NUM_LATITUDE_LINES - 1) - 0.5) * math.pi
    
    local y = RADIUS * math.sin(latitude)
    local circle_radius = RADIUS * math.cos(latitude)
    local x = circle_radius * math.cos(drawing_phase)
    local z = circle_radius * math.sin(drawing_phase)
    
    x, y, z = rotateX(x, y, z, -0.3)
    
    return {x, y, z}

elseif railroad_switch < NUM_LATITUDE_LINES + NUM_LONGITUDE_LINES then
    local current_line = math.floor(railroad_switch - NUM_LATITUDE_LINES)
    local longitude = (current_line / NUM_LONGITUDE_LINES) * 2 * math.pi
    
    local height = math.cos(drawing_phase)
    local circle_radius = math.sqrt(1 - height * height)
    local x = RADIUS * circle_radius * math.cos(longitude)
    local z = RADIUS * circle_radius * math.sin(longitude)
    local y = RADIUS * height
    
    x, y, z = rotateX(x, y, z, -0.3)
    
    return {x, y, z}
    
else
    local current_ring = math.floor((railroad_switch - (NUM_LATITUDE_LINES + NUM_LONGITUDE_LINES)) / 3)
    local ring_radius = RADIUS + RING_SPACING * (current_ring + 1)
    
    local x = ring_radius * math.cos(drawing_phase)
    local z = ring_radius * math.sin(drawing_phase)
    local y = 0
    
    x, y, z = rotateX(x, y, z, -0.3)
    
    return {x, y, z}
end

return {0, 0, 0}