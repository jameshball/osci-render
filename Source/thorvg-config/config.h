// osci-render thorvg configuration shim.
//
// thorvg expects a meson-generated `config.h`. We provide this minimal
// static configuration so we can compile the thorvg sources directly as
// part of the osci-render build, without the meson build step.
//
// We only enable the Lottie loader. No rendering engines (cpu/gl/wg) are
// enabled because we do not use thorvg for rasterisation — we only walk the
// scene-tree via Accessor to extract vector paths and feed them into the
// oscilloscope as osci::Shape objects.
//
// Lottie expressions (AE scripting via jerryscript) are intentionally off
// to keep the compile footprint small; advanced Lottie features that rely
// on expressions will be ignored at load time.

#pragma once

#define THORVG_VERSION_STRING "1.0.0"

// Note: THORVG_LOTTIE_LOADER_SUPPORT is also defined as =1 in the .jucer
// global defines so that our own source files (which include <thorvg.h>
// directly, not tvgCommon.h) see it. Match the value here to avoid a
// macro-redefined warning.
#define THORVG_LOTTIE_LOADER_SUPPORT 1

// Intentionally NOT defined:
//   THORVG_CPU_ENGINE_SUPPORT
//   THORVG_GL_ENGINE_SUPPORT
//   THORVG_WG_ENGINE_SUPPORT
//   THORVG_SVG_LOADER_SUPPORT
//   THORVG_PNG_LOADER_SUPPORT
//   THORVG_JPG_LOADER_SUPPORT
//   THORVG_WEBP_LOADER_SUPPORT
//   THORVG_TTF_LOADER_SUPPORT
//   THORVG_LOTTIE_EXPRESSIONS_SUPPORT
//   THORVG_GIF_SAVER_SUPPORT
//   THORVG_FILE_IO_SUPPORT
//   THORVG_THREAD_SUPPORT
//   THORVG_PARTIAL_RENDER_SUPPORT
//   THORVG_LOG_ENABLED
