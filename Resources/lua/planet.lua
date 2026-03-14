local RADIUS = 0.4
local NUM_LATITUDE_LINES = 12
local NUM_LONGITUDE_LINES = 12
local NUM_RINGS = 3
local RING_SPACING = 0.15

-- Build all shapes once (globals persist across samples)
if not shapes then
    shapes = {}

    -- Latitude lines
    for i = 0, NUM_LATITUDE_LINES - 1 do
        local latitude = (i / (NUM_LATITUDE_LINES - 1) - 0.5) * math.pi
        shapes[#shapes + 1] = function(drawing_phase)
            local y = RADIUS * math.sin(latitude)
            local circle_radius = RADIUS * math.cos(latitude)
            local x = circle_radius * math.cos(drawing_phase)
            local z = circle_radius * math.sin(drawing_phase)
            return osci_rotate({x, y, z}, -0.3, 0, 0)
        end
    end

    -- Longitude lines
    for i = 0, NUM_LONGITUDE_LINES - 1 do
        local longitude = (i / NUM_LONGITUDE_LINES) * 2 * math.pi
        shapes[#shapes + 1] = function(drawing_phase)
            local height = math.cos(drawing_phase)
            local circle_radius = math.sqrt(1 - height * height)
            local x = RADIUS * circle_radius * math.cos(longitude)
            local z = RADIUS * circle_radius * math.sin(longitude)
            local y = RADIUS * height
            return osci_rotate({x, y, z}, -0.3, 0, 0)
        end
    end

    -- Rings (each ring is drawn 3x so it appears brighter on the oscilloscope)
    for i = 0, NUM_RINGS - 1 do
        local ring_radius = RADIUS + RING_SPACING * (i + 1)
        for j = 1, 3 do
            shapes[#shapes + 1] = function(drawing_phase)
                local x = ring_radius * math.cos(drawing_phase)
                local z = ring_radius * math.sin(drawing_phase)
                local y = 0
                return osci_rotate({x, y, z}, -0.3, 0, 0)
            end
        end
    end
end

return osci_chain(phase, shapes)