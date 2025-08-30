local SCALE = 1.0
local ANIMATION_SPEED = 1.0

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
    local w_factor = 1 / (3 - w)
    return x * w_factor, y * w_factor, z * w_factor
end

local vertices = {
    -- Inner cube
    {-1, -1, -1, -1}, {1, -1, -1, -1}, {1, 1, -1, -1}, {-1, 1, -1, -1},
    {-1, -1, 1, -1}, {1, -1, 1, -1}, {1, 1, 1, -1}, {-1, 1, 1, -1},
    -- Outer cube
    {-1, -1, -1, 1}, {1, -1, -1, 1}, {1, 1, -1, 1}, {-1, 1, -1, 1},
    {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
}

local edges = {
    -- Inner cube edges
    {1, 2}, {2, 3}, {3, 4}, {4, 1},
    {5, 6}, {6, 7}, {7, 8}, {8, 5},
    {1, 5}, {2, 6}, {3, 7}, {4, 8},
    -- Outer cube edges
    {9, 10}, {10, 11}, {11, 12}, {12, 9},
    {13, 14}, {14, 15}, {15, 16}, {16, 13},
    {9, 13}, {10, 14}, {11, 15}, {12, 16},
    -- Connections between cubes
    {1, 9}, {2, 10}, {3, 11}, {4, 12},
    {5, 13}, {6, 14}, {7, 15}, {8, 16}
}

local NUM_EDGES = #edges

local railroad_switch = NUM_EDGES * phase / (2 * math.pi)
local current_edge = math.floor(railroad_switch) + 1
local edge_phase = railroad_switch % 1

local time = step/sample_rate * ANIMATION_SPEED

if current_edge <= NUM_EDGES then
    local v1 = vertices[edges[current_edge][1]]
    local v2 = vertices[edges[current_edge][2]]
    
    local x = v1[1] + (v2[1] - v1[1]) * edge_phase
    local y = v1[2] + (v2[2] - v1[2]) * edge_phase
    local z = v1[3] + (v2[3] - v1[3]) * edge_phase
    local w = v1[4] + (v2[4] - v1[4]) * edge_phase
    
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