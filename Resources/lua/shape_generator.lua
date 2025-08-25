-- Arbitrary shape generator 1.2
-- by DJ_Level_3 8/3/2025

-- slider_a - trace start
-- slider_b - trace end
-- slider_c - reduce

-- utility function to linerarly interpolate between startVal and endVal
-- t should go from 0 to 1
function lerp(startVal, endVal, t)
    return (endVal - startVal) * t + startVal
end

-- These are the lookup tables for the X, Y, and time coordinates of the
-- shape's vertices
lutL = lutL or {-1, 0, 1, -1}
lutR = lutR or {-1, 1, -1, -1}
lutT = lutT or {0, 500, 1000, 2000}

startval = math.max(lutT[1], math.min(lutT[#lutT], lutT[#lutT] * slider_a))
endval = startval+ math.max(lutT[1], math.min(lutT[#lutT], lutT[#lutT] * slider_b))

-- the current sample in time and the current index in the lookup tables
timeNow = timeNow or 0
ind = ind or 1

vTimeNow = timeNow
-- increment the segment if necessary
while ((vTimeNow) > lutT[(ind % #lutT)+1]) do
    ind = ind + 1
    if (ind > #lutT) then
        ind = 1
        vTimeNow = vTimeNow - lutT[#lutT]
    end
end

-- prevent div-by-zero and do a neat effect
diff = math.max(1e-10, (lutT[ind+1] - lutT[ind]) / math.min(1,math.max(1e-10,1-slider_c)))

-- Actually perform the lookup and interpolation
x = lerp(lutL[ind], lutL[ind+1], ((timeNow % lutT[#lutT]) - lutT[ind])/diff)
y = lerp(lutR[ind], lutR[ind+1], ((timeNow % lutT[#lutT]) - lutT[ind])/diff)

-- increment the time variable by the appropriate amount to get the desired frequency
timeNow = timeNow + ((endval-startval) * frequency / sample_rate)

-- reset if necessary
if (timeNow > endval) then
    ind = 1
    timeNow = startval
end

return {x,y}