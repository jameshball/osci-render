# Third-Party Notices

## stb_image

osci-motion's PNG/JPEG raster import (`Source/motion/import/RasterDecoder.cpp`)
uses `stb_image.h` in this folder, upstream stb_image 2.30
from https://github.com/nothings/stb at commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20`. Downloaded from the matching
https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h.
The unmodified source contains the complete dual MIT/public-domain license;
osci-motion uses the MIT license option. Only PNG/JPEG support is compiled,
with private symbols, allocation budgets and bounded input callbacks.
Animated GIF uses the decoder's independently implemented checked parser/LZW
decoder and compositor, not stb_image's animated GIF API.
