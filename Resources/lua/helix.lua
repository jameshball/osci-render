local RADIUS = 0.3
local HEIGHT = 0.8
local ROTATIONS = 1
local SEGMENTS = 24
local BASE_PAIRS = 6
local ANIMATION_SPEED = 0.5

local total_segments = SEGMENTS * 2 + BASE_PAIRS * 2
local railroad_switch = total_segments * phase / (2 * math.pi)
local segment_index = math.floor(railroad_switch)
local segment_phase = railroad_switch % 1

local time = step/sample_rate * ANIMATION_SPEED
local rotation = time

local x, y, z = 0, 0, 0

if segment_index < SEGMENTS * 2 then
    local helix_number = math.floor(segment_index / SEGMENTS)
    local t = (segment_index % SEGMENTS + segment_phase) / SEGMENTS
    
    local angle = t * ROTATIONS * 2 * math.pi
    x = RADIUS * math.cos(angle + helix_number * math.pi)
    y = HEIGHT * (t * 2 - 1)
    z = RADIUS * math.sin(angle + helix_number * math.pi)
else
    local base_pair_index = math.floor((segment_index - SEGMENTS * 2) / 2)
    local is_start = (segment_index - SEGMENTS * 2) % 2 == 0
    
    local t = base_pair_index / (BASE_PAIRS - 1)
    local angle = t * ROTATIONS * 2 * math.pi
    
    if is_start then
        x = RADIUS * math.cos(angle)
        y = HEIGHT * (t * 2 - 1)
        z = RADIUS * math.sin(angle)
    else
        x = RADIUS * math.cos(angle + math.pi)
        y = HEIGHT * (t * 2 - 1)
        z = RADIUS * math.sin(angle + math.pi)
    end
    
    if not is_start then
        local start_x = RADIUS * math.cos(angle)
        local start_z = RADIUS * math.sin(angle)
        x = x * (1 - segment_phase) + start_x * segment_phase
        z = z * (1 - segment_phase) + start_z * segment_phase
    end
end


return {x, y, z}