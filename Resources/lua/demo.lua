-- This is a 'micro' version of osci-render that
-- draws the default cube and allows you to rotate
-- it and also apply some basic audio effects.
--
-- The Lua sliders control the following:
-- - slider_a = frequency/drawing speed
-- - slider_b = camera z position
-- - slider_c = focal length of camera
-- - slider_d = rotation speed
-- - slider_e = trace effect
--

-- a=start point, b=end point, t=drawing progress
function line(a, b, t)
  return {
    a.x + t * (b.x - a.x),
    a.y + t * (b.y - a.y)
  }
end

focal_length = slider_c
camera_z = slider_b * 3

-- projects a 3D point into 2D
function project(point)
  return {
    x=point.x * focal_length / (point.z - camera_z),
    y=point.y * focal_length / (point.z - camera_z)
  }
end

-- rotates a point in 3D
function rotate(point, rotate_x, rotate_y, rotate_z)
  x1 = point.x
  y1 = point.y
  z1 = point.z

  -- rotate around x-axis
  cos = math.cos(rotate_x)
  sin = math.sin(rotate_x)
  y2 = cos * y1 - sin * z1
  z2 = sin * y1 + cos * z1

  -- rotate around y-axis
  cos = math.cos(rotate_y)
  sin = math.sin(rotate_y)
  x2 = cos * x1 + sin * z2
  z3 = -sin * x1 + cos * z2

  -- rotate around z-axis
  cos = math.cos(rotate_z)
  sin = math.sin(rotate_z)
  x3 = cos * x2 - sin * y2
  y3 = sin * x2 + cos * y2

  return {
    x=x3,
    y=y3,
    z=z3
  }
end

num_points = 16
-- 3D cube draw path
points = {
  {x=-1.0, y=-1.0, z=1.0},
  {x=1.0, y=-1.0, z=1.0},
  {x=1.0, y=-1.0, z=-1.0},
  {x=-1.0, y=-1.0, z=-1.0},
  {x=1.0, y=-1.0, z=-1.0},
  {x=1.0, y=1.0, z=-1.0},
  {x=-1.0, y=1.0, z=-1.0},
  {x=-1.0, y=-1.0, z=-1.0},
  {x=-1.0, y=-1.0, z=1.0},
  {x=1.0, y=-1.0, z=1.0},
  {x=1.0, y=1.0, z=1.0},
  {x=-1.0, y=1.0, z=1.0},
  {x=1.0, y=1.0, z=1.0},
  {x=1.0, y=1.0, z=-1.0},
  {x=-1.0, y=1.0, z=-1.0},
  {x=-1.0, y=1.0, z=1.0}
}

-- Percentage of the image that has currently been drawn.
-- The 'or' syntax sets 'drawing_progress' to 0 initially, or the
-- previous value of 'drawing_progress' the last time this script ran.
drawing_progress = drawing_progress or 0
drawing_progress = drawing_progress + 0.001 * slider_a
progress_cutoff = slider_e

if drawing_progress > progress_cutoff then
  drawing_progress = 0
end

-- get the index of the start point of the current line
start_index = math.floor(drawing_progress * num_points + 1)
-- end point of current line is the next in the points table
end_index = start_index + 1
if end_index > num_points then
  end_index = 1
end

-- doing this to avoid recomputation for every sample.
-- prev_start ~= start_index == true whenever a new line has started
if prev_start ~= start_index then
  rotate_speed = slider_d * step / 50000
  -- rotate and project the start and end points
  proj_start = project(rotate(points[start_index], rotate_speed, rotate_speed, 0))
  proj_end = project(rotate(points[end_index], rotate_speed, rotate_speed, 0))
end

prev_start = start_index

-- work out what the relative progress drawn of the current line is
draw_length = 1.0 / num_points
line_progress = drawing_progress - (start_index - 1) * draw_length
line_progress = num_points * line_progress

return line(proj_start, proj_end, line_progress)