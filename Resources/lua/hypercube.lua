local SCALE = 1.0
local ANIMATION_SPEED = 1.0
local FOV_4D = math.rad(80)

local function rotate4D_XY(x, y, z, w, angle)
    return x * math.cos(angle) - y * math.sin(angle),
           x * math.sin(angle) + y * math.cos(angle),
           z,
           w
end

local function rotate4D_XZ(x, y, z, w, angle)
    return x * math.cos(angle) - z * math.sin(angle),
           y,
           x * math.sin(angle) + z * math.cos(angle),
           w
end

local function rotate4D_XW(x, y, z, w, angle)
    return x * math.cos(angle) - w * math.sin(angle),
           y,
           z,
           x * math.sin(angle) + w * math.cos(angle)
end

local function project4Dto3D(x, y, z, w)
    local camera_w = -1 / math.sin(0.5 * FOV_4D)
    local scale = 1 / (w - camera_w) / math.tan(0.5 * FOV_4D)
    return x * scale, y * scale, z * scale
end

local vertices = {
    -- Inner cube
    {-1, -1, -1, -1}, {1, -1, -1, -1}, {1, 1, -1, -1}, {-1, 1, -1, -1},
    {-1, -1, 1, -1}, {1, -1, 1, -1}, {1, 1, 1, -1}, {-1, 1, 1, -1},
    -- Outer cube
    {-1, -1, -1, 1}, {1, -1, -1, 1}, {1, 1, -1, 1}, {-1, 1, -1, 1},
    {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
}

-- Eulerian cycle through vertices
local path = {
    1, 5, 8, 4, 12, 16, 15, 11,
    3, 7, 6, 14, 15, 7, 8, 16,
    13, 5, 6, 2, 10, 9, 13, 14,
    10, 11, 12, 9, 1, 2, 3, 4
}

local NUM_EDGES = #path

local railroad_switch = NUM_EDGES * phase / (2 * math.pi)
local current_vertex = math.floor(railroad_switch) + 1
local next_vertex = current_vertex % NUM_EDGES + 1
local edge_phase = railroad_switch % 1

local time = step/sample_rate * ANIMATION_SPEED

if current_vertex <= NUM_EDGES then
    local v1 = vertices[path[current_vertex]]
    local v2 = vertices[path[next_vertex]]
    
    local x = v1[1] + (v2[1] - v1[1]) * edge_phase
    local y = v1[2] + (v2[2] - v1[2]) * edge_phase
    local z = v1[3] + (v2[3] - v1[3]) * edge_phase
    local w = v1[4] + (v2[4] - v1[4]) * edge_phase

    -- Normalize
    x = x * 0.5
    y = y * 0.5
    z = z * 0.5
    w = w * 0.5
    
    local fold_angle = math.sin(time) * math.pi / 2
    
    x, y, z, w = rotate4D_XY(x, y, z, w, time)
    x, y, z, w = rotate4D_XZ(x, y, z, w, time * 0.7)
    x, y, z, w = rotate4D_XW(x, y, z, w, fold_angle)
    
    x, y, z = project4Dto3D(x, y, z, w)
    
    x = x * SCALE
    y = y * SCALE
    z = z * SCALE
    
    return {x, y, z}
end

return {0, 0}
