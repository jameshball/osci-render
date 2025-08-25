local SCALE = 1.0
local ANIMATION_SPEED = 1.0

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

local vertices = {
    -- Head
    {-0.2, 0.7, -0.2},   -- 1
    {0.2, 0.7, -0.2},    -- 2
    {0.2, 0.3, -0.2},    -- 3
    {-0.2, 0.3, -0.2},   -- 4
    {-0.2, 0.7, 0.2},    -- 5
    {0.2, 0.7, 0.2},     -- 6
    {0.2, 0.3, 0.2},     -- 7
    {-0.2, 0.3, 0.2},    -- 8
    
    -- Torso
    {-0.3, 0.3, -0.2},   -- 9
    {0.3, 0.3, -0.2},    -- 10
    {0.0, -0.4, -0.2},   -- 11
    {-0.3, 0.3, 0.2},    -- 12
    {0.3, 0.3, 0.2},     -- 13
    {0.0, -0.4, 0.2},    -- 14
    
    -- Left arm
    {-0.3, 0.2, 0.0},    -- 15
    {-0.7, -0.1, 0.0},   -- 16
    {-0.9, -0.5, 0.0},   -- 17
    
    -- Right arm
    {0.3, 0.2, 0.0},     -- 18
    {0.7, -0.1, 0.0},    -- 19
    {0.9, -0.5, 0.0},    -- 20
    
    -- Left leg
    {-0.2, -0.4, 0.0},   -- 21
    {-0.3, -1.0, 0.0},   -- 22
    
    -- Right leg
    {0.2, -0.4, 0.0},    -- 23
    {0.3, -1.0, 0.0}     -- 24
}

local edges = {
    -- Head
    {1, 2}, {2, 3}, {3, 4}, {4, 1},  -- Front face
    {5, 6}, {6, 7}, {7, 8}, {8, 5},  -- Back face
    {1, 5}, {2, 6}, {3, 7}, {4, 8},  -- Connecting edges
    
    -- Torso
    {9, 10}, {10, 11}, {11, 9},      -- Front face
    {12, 13}, {13, 14}, {14, 12},    -- Back face
    {9, 12}, {10, 13}, {11, 14},     -- Connecting edges
    
    -- Arms
    {15, 16}, {16, 17},              -- Left arm
    {18, 19}, {19, 20},              -- Right arm
    
    -- Legs
    {21, 22},                        -- Left leg
    {23, 24},                        -- Right leg
}

local NUM_EDGES = #edges

local railroad_switch = NUM_EDGES * phase / (2 * math.pi)
local current_edge = math.floor(railroad_switch) + 1
local edge_phase = (railroad_switch % 1)

local time = step/sample_rate * ANIMATION_SPEED
local walk_cycle = math.sin(time * 2)

if current_edge <= NUM_EDGES then
    local v1 = vertices[edges[current_edge][1]]
    local v2 = vertices[edges[current_edge][2]]
    
    local x = v1[1] + (v2[1] - v1[1]) * edge_phase
    local y = v1[2] + (v2[2] - v1[2]) * edge_phase
    local z = v1[3] + (v2[3] - v1[3]) * edge_phase
    
    y = y + math.sin(time) * 0.05

    if edges[current_edge][1] == 18 or edges[current_edge][1] == 19 then
        z = z + walk_cycle * 0.05
    end

    if edges[current_edge][1] == 15 or edges[current_edge][1] == 16 then
        z = z - walk_cycle * 0.05
    end
    
    if edges[current_edge][1] == 21 then
        z = z + walk_cycle * 0.15
    end

    if edges[current_edge][1] == 23 then
        z = z - walk_cycle * 0.15
    end

    if edges[current_edge][1] <= 8 then
        x, y, z = rotateY(x, y, z, 0.5 * walk_cycle)
    end
    
    x = x * SCALE
    y = y * SCALE
    z = z * SCALE
    
    return {x, y, z}
end

return {0, 0}