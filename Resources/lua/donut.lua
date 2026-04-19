local NUM_LATITUDE_LINES = 16
local NUM_LONGITUDE_LINES = 24
local MAJOR_RADIUS = 0.6
local MINOR_RADIUS = 0.2

-- u: angle around the tube (0 to 2π)
-- v: angle around the center (0 to 2π)
local function torus_point(u, v)
    local x = (MAJOR_RADIUS + MINOR_RADIUS * math.cos(u)) * math.cos(v)
    local y = (MAJOR_RADIUS + MINOR_RADIUS * math.cos(u)) * math.sin(v)
    local z = MINOR_RADIUS * math.sin(u)
    return x, y, z
end

-- Build a table of drawing functions once (globals persist across samples)
if not shapes then
    shapes = {}

    for i = 0, NUM_LATITUDE_LINES - 1 do
        local u = (i / NUM_LATITUDE_LINES) * 2 * math.pi
        shapes[#shapes + 1] = function(v)
            local x, y, z = torus_point(u, v)
            return osci_rotate({x, y, z}, 1, 0, 0)
        end
    end

    for i = 0, NUM_LONGITUDE_LINES - 1 do
        local v = (i / NUM_LONGITUDE_LINES) * 2 * math.pi
        shapes[#shapes + 1] = function(u)
            local x, y, z = torus_point(u, v)
            return osci_rotate({x, y, z}, 1, 0, 0)
        end
    end
end

return osci_chain(phase, shapes)