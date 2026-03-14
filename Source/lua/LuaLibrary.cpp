#include "LuaLibrary.h"
#include "LuaParser.h"

#include <lua.hpp>

// ============================================================================
// Helpers
// ============================================================================

static int pointToTable(lua_State* L, osci::Point point, int numDims) {
    lua_newtable(L);

    lua_pushnumber(L, point.x);
    lua_rawseti(L, -2, 1);

    if (numDims >= 2) {
        lua_pushnumber(L, point.y);
        lua_rawseti(L, -2, 2);
    }

    if (numDims >= 3) {
        lua_pushnumber(L, point.z);
        lua_rawseti(L, -2, 3);

        if (point.r >= 0.0f) {
            lua_pushnumber(L, point.r);
            lua_rawseti(L, -2, 4);
            lua_pushnumber(L, point.g);
            lua_rawseti(L, -2, 5);
            lua_pushnumber(L, point.b);
            lua_rawseti(L, -2, 6);
        }
    }

    return 1;
}

static osci::Point tableToPoint(lua_State* L, int index) {
    osci::Point point;
    lua_pushinteger(L, 1);
    lua_gettable(L, index);
    point.x = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_pushinteger(L, 2);
    lua_gettable(L, index);
    point.y = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_pushinteger(L, 3);
    lua_gettable(L, index);
    point.z = lua_tonumber(L, -1);
    lua_pop(L, 1);

    // Read optional r,g,b colour channels (indices 4,5,6)
    lua_pushinteger(L, 4);
    lua_gettable(L, index);
    if (!lua_isnil(L, -1)) {
        point.r = lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_pushinteger(L, 5);
        lua_gettable(L, index);
        point.g = lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_pushinteger(L, 6);
        lua_gettable(L, index);
        point.b = lua_tonumber(L, -1);
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }

    return point;
}

// ============================================================================
// Shape / geometry primitives
// ============================================================================

static int luaLine(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    osci::Point point1 = nargs == 3 ? tableToPoint(L, 2) : osci::Point(-1, -1);
    osci::Point point2 = nargs == 3 ? tableToPoint(L, 3) : osci::Point(1, 1);

    osci::Line line = osci::Line(point1, point2);
    osci::Point point = line.nextVector(t);

    return pointToTable(L, point, 3);
}

static osci::Point genericRect(double phase, osci::Point topLeft, double width, double height) {
    double t = phase / juce::MathConstants<double>::twoPi;
    double totalLength = 2 * (width + height);
    double progress = t * totalLength;

    osci::Line line = osci::Line(0, 0);
    double adjustedProgress;
    double x = topLeft.x;
    double y = topLeft.y;

    if (progress < width) {
        line = osci::Line(x, y, x + width, y);
        adjustedProgress = progress / width;
    } else if (progress < width + height) {
        line = osci::Line(x + width, y, x + width, y + height);
        adjustedProgress = (progress - width) / height;
    } else if (progress < 2 * width + height) {
        line = osci::Line(x + width, y + height, x, y + height);
        adjustedProgress = (progress - width - height) / width;
    } else {
        line = osci::Line(x, y + height, x, y);
        adjustedProgress = (progress - 2 * width - height) / height;
    }

    return line.nextVector(adjustedProgress);
}

static int luaRect(lua_State* L) {
    int nargs = lua_gettop(L);

    double phase = lua_tonumber(L, 1);
    double width = nargs == 1 ? 1 : lua_tonumber(L, 2);
    double height = nargs == 1 ? 1.5 : lua_tonumber(L, 3);

    osci::Point topLeft = nargs == 4 ? tableToPoint(L, 4) : osci::Point(-width / 2, -height / 2);
    osci::Point point = genericRect(phase, topLeft, width, height);
    
    return pointToTable(L, point, 2);
}

static int luaSquare(lua_State* L) {
    int nargs = lua_gettop(L);

    double phase = lua_tonumber(L, 1);
    double width = nargs == 1 ? 1 : lua_tonumber(L,2);

    osci::Point topLeft = nargs == 3 ? tableToPoint(L, 3) : osci::Point(-width / 2, -width / 2);
    osci::Point point = genericRect(phase, topLeft, width, width);
    
    return pointToTable(L, point, 2);
}

static int luaEllipse(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radiusX = nargs == 1 ? 0.6 : lua_tonumber(L, 2);
    double radiuxY = nargs == 1 ? 0.8 : lua_tonumber(L, 3);

    osci::CircleArc ellipse = osci::CircleArc(0, 0, radiusX, radiuxY, 0, juce::MathConstants<double>::twoPi);
    osci::Point point = ellipse.nextVector(t);
    
    return pointToTable(L, point, 2);
}

static int luaCircle(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radius = nargs == 1 ? 0.8 : lua_tonumber(L, 2);

    osci::CircleArc ellipse = osci::CircleArc(0, 0, radius, radius, 0, juce::MathConstants<double>::twoPi);
    osci::Point point = ellipse.nextVector(t);
    
    return pointToTable(L, point, 2);
}

static int luaArc(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radiusX = nargs == 1 ? 1 : lua_tonumber(L, 2);
    double radiusY = nargs == 1 ? 1 : lua_tonumber(L, 3);
    double startAngle = nargs == 1 ? 0 : lua_tonumber(L, 4);
    double endAngle = nargs == 1 ? juce::MathConstants<double>::halfPi : lua_tonumber(L, 5);

    osci::CircleArc arc = osci::CircleArc(0, 0, radiusX, radiusY, startAngle, endAngle);
    osci::Point point = arc.nextVector(t);
    
    return pointToTable(L, point, 2);
}

static int luaPolygon(lua_State* L) {
    int nargs = lua_gettop(L);

    double n = nargs == 1 ? 5 : lua_tonumber(L, 2);
    double t = n * lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;

    int floor_t = (int) t;

    double pi_n = juce::MathConstants<double>::pi / n;
    double two_floor_t_plus_one = 2 * floor_t + 1;
    double inner_cos = cos(pi_n * two_floor_t_plus_one);
    double inner_sin = sin(pi_n * two_floor_t_plus_one);
    double multiplier = 2 * t - 2 * floor_t - 1;

    double x = cos(pi_n) * inner_cos - multiplier * sin(pi_n) * inner_sin;
    double y = cos(pi_n) * inner_sin + multiplier * sin(pi_n) * inner_cos;

    return pointToTable(L, osci::Point(x, y), 2);
}

static int luaBezier(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    osci::Point point1 = nargs == 1 ? osci::Point(-1, -1) : tableToPoint(L, 2);
    osci::Point point2 = nargs == 1 ? osci::Point(-1, 1) : tableToPoint(L, 3);
    osci::Point point3 = nargs == 1 ? osci::Point(1, 1) : tableToPoint(L, 4);

    osci::Point point;

    if (nargs == 5) {
        osci::Point point4 = tableToPoint(L, 5);

        osci::CubicBezierCurve curve = osci::CubicBezierCurve(point1.x, point1.y, point2.x, point2.y, point3.x, point3.y, point4.x, point4.y);
        point = curve.nextVector(t);
    } else {
        osci::QuadraticBezierCurve curve = osci::QuadraticBezierCurve(point1.x, point1.y, point2.x, point2.y, point3.x, point3.y);
        point = curve.nextVector(t);
    }
    
    return pointToTable(L, point, 2);
}

static int luaLissajous(lua_State* L) {
    int nargs = lua_gettop(L);

    double phase = lua_tonumber(L, 1);
    double radius = nargs == 1 ? 1 : lua_tonumber(L, 2);
    double ratioA = nargs == 1 ? 1 : lua_tonumber(L, 3);
    double ratioB = nargs == 1 ? 5 : lua_tonumber(L, 4);
    double theta = nargs == 1 ? 0 : lua_tonumber(L, 5);

    double x = radius * sin(ratioA * phase);
    double y = radius * cos(ratioB * phase + theta);

    return pointToTable(L, osci::Point(x, y), 2);
}

// ============================================================================
// Waveforms
// ============================================================================

static double squareWave(double phase) {
    double t = phase / juce::MathConstants<double>::twoPi;
    return (t - (int) t < 0.5) ? 1 : 0;
}

static double sawWave(double phase) {
    double t = phase / juce::MathConstants<double>::twoPi;
    return t - std::floor(t);
}

static double triangleWave(double phase) {
    double t = phase / juce::MathConstants<double>::twoPi;
    return abs(2 * (t - std::floor(t)) - 1);
}

static int luaSquareWave(lua_State* L) {
    double phase = lua_tonumber(L, 1);
    lua_pushnumber(L, squareWave(phase));
    return 1;
}

static int luaSawWave(lua_State* L) {
    double phase = lua_tonumber(L, 1);
    lua_pushnumber(L, sawWave(phase));
    return 1;
}

static int luaTriangleWave(lua_State* L) {
    double phase = lua_tonumber(L, 1);
    lua_pushnumber(L, triangleWave(phase));
    return 1;
}

static int luaPulseWave(lua_State* L) {
    double phase = lua_tonumber(L, 1);
    double duty = lua_gettop(L) >= 2 ? lua_tonumber(L, 2) : 0.5;
    double t = phase / juce::MathConstants<double>::twoPi;
    t = t - std::floor(t);
    lua_pushnumber(L, t < duty ? 1.0 : 0.0);
    return 1;
}

// ============================================================================
// Transforms
// ============================================================================

static int luaMix(lua_State* L) {
    int nargs = lua_gettop(L);

    osci::Point point1 = tableToPoint(L, 1);
    osci::Point point2 = tableToPoint(L, 2);
    double weight = lua_tonumber(L, 3);

    osci::Point point(
        point1.x * (1 - weight) + point2.x * weight,
        point1.y * (1 - weight) + point2.y * weight,
        point1.z * (1 - weight) + point2.z * weight
    );

    if (point1.r >= 0.0f || point2.r >= 0.0f) {
        float r1 = point1.r >= 0.0f ? point1.r : 0.0f;
        float g1 = point1.r >= 0.0f ? point1.g : 0.0f;
        float b1 = point1.r >= 0.0f ? point1.b : 0.0f;
        float r2 = point2.r >= 0.0f ? point2.r : 0.0f;
        float g2 = point2.r >= 0.0f ? point2.g : 0.0f;
        float b2 = point2.r >= 0.0f ? point2.b : 0.0f;
        point.r = r1 * (1 - weight) + r2 * weight;
        point.g = g1 * (1 - weight) + g2 * weight;
        point.b = b1 * (1 - weight) + b2 * weight;
    }

    return pointToTable(L, point, 3);
}

static int luaTranslate(lua_State* L) {
    osci::Point point = tableToPoint(L, 1);
    osci::Point translation = tableToPoint(L, 2);

    point.translate(translation.x, translation.y, translation.z);

    return pointToTable(L, point, 3);
}

static int luaScale(lua_State* L) {
    osci::Point point = tableToPoint(L, 1);
    osci::Point scale = tableToPoint(L, 2);

    point.scale(scale.x, scale.y, scale.z);

    return pointToTable(L, point, 3);
}

static int luaRotate(lua_State* L) {
    int nargs = lua_gettop(L);
    bool twoDimRotate = nargs == 2;

    if (twoDimRotate) {
        osci::Point point = tableToPoint(L, 1);
        double angle = lua_tonumber(L, 2);

        point.rotate(0, 0, angle);

        return pointToTable(L, point, 2);
    } else {
        osci::Point point = tableToPoint(L, 1);
        double xRotate = lua_tonumber(L, 2);
        double yRotate = lua_tonumber(L, 3);
        double zRotate = lua_tonumber(L, 4);

        point.rotate(xRotate, yRotate, zRotate);

        return pointToTable(L, point, 3);
    }
}

// ============================================================================
// Shape chaining
// ============================================================================

static int luaChain(lua_State* L) {
    double phase = lua_tonumber(L, 1);
    
    if (!lua_istable(L, 2)) {
        lua_pushstring(L, "osci_chain: second argument must be a table of functions");
        lua_error(L);
        return 0;
    }
    
    int numSegments = (int)lua_objlen(L, 2);
    if (numSegments <= 0) {
        lua_newtable(L);
        lua_pushnumber(L, 0);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, 0);
        lua_rawseti(L, -2, 2);
        return 1;
    }
    
    double t = numSegments * phase / (2.0 * juce::MathConstants<double>::pi);
    int segmentIndex = (int)std::floor(t);
    double segmentPhase = std::fmod(t, 1.0);
    
    // Clamp to valid range
    if (segmentIndex < 0) segmentIndex = 0;
    if (segmentIndex >= numSegments) segmentIndex = numSegments - 1;
    
    // Scale segment phase back to 0..2pi for the sub-function
    double subPhase = segmentPhase * 2.0 * juce::MathConstants<double>::pi;
    
    // Get the function at segmentIndex+1 (Lua 1-indexed)
    lua_rawgeti(L, 2, segmentIndex + 1);
    
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushnumber(L, 0);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, 0);
        lua_rawseti(L, -2, 2);
        return 1;
    }
    
    // Call the function with subPhase
    lua_pushnumber(L, subPhase);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        lua_error(L);
        return 0;
    }
    
    return 1;
}

// ============================================================================
// Math / DSP utilities
// ============================================================================

static int luaNoise(lua_State* L) {
    lua_pushnumber(L, juce::Random::getSystemRandom().nextDouble() * 2.0 - 1.0);
    return 1;
}

static int luaLerp(lua_State* L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    double t = lua_tonumber(L, 3);
    lua_pushnumber(L, a + (b - a) * t);
    return 1;
}

static int luaSmoothstep(lua_State* L) {
    double edge0 = lua_tonumber(L, 1);
    double edge1 = lua_tonumber(L, 2);
    double x = lua_tonumber(L, 3);
    double t = (edge1 != edge0) ? juce::jlimit(0.0, 1.0, (x - edge0) / (edge1 - edge0)) : 0.0;
    lua_pushnumber(L, t * t * (3.0 - 2.0 * t));
    return 1;
}

static int luaClamp(lua_State* L) {
    double x = lua_tonumber(L, 1);
    double lo = lua_tonumber(L, 2);
    double hi = lua_tonumber(L, 3);
    lua_pushnumber(L, juce::jlimit(lo, hi, x));
    return 1;
}

static int luaMap(lua_State* L) {
    double x = lua_tonumber(L, 1);
    double inMin = lua_tonumber(L, 2);
    double inMax = lua_tonumber(L, 3);
    double outMin = lua_tonumber(L, 4);
    double outMax = lua_tonumber(L, 5);
    double t = (inMax != inMin) ? (x - inMin) / (inMax - inMin) : 0.0;
    lua_pushnumber(L, outMin + (outMax - outMin) * t);
    return 1;
}

// ============================================================================
// Vector math
// ============================================================================

static int luaDot(lua_State* L) {
    osci::Point a = tableToPoint(L, 1);
    osci::Point b = tableToPoint(L, 2);
    lua_pushnumber(L, a.x * b.x + a.y * b.y + a.z * b.z);
    return 1;
}

static int luaCross(lua_State* L) {
    osci::Point a = tableToPoint(L, 1);
    osci::Point b = tableToPoint(L, 2);
    osci::Point result(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
    return pointToTable(L, result, 3);
}

static int luaLength(lua_State* L) {
    osci::Point p = tableToPoint(L, 1);
    lua_pushnumber(L, std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z));
    return 1;
}

static int luaNormalize(lua_State* L) {
    osci::Point p = tableToPoint(L, 1);
    double len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 0.0) {
        p.x /= len;
        p.y /= len;
        p.z /= len;
    }
    return pointToTable(L, p, 3);
}

// ============================================================================
// Console
// ============================================================================

static int luaPrint(lua_State* L) {
    int nargs = lua_gettop(L);

    for (int i = 1; i <= nargs; ++i) {
        size_t len = 0;
        const char* str = lua_tolstring(L, i, &len);
        if (str != nullptr) {
            LuaParser::onPrint(std::string(str, len));
        } else {
            LuaParser::onPrint(lua_typename(L, lua_type(L, i)));
        }
        lua_pop(L, 1);
    }

    return 0;
}

static int luaClear(lua_State* L) {
    LuaParser::onClear();
    return 0;
}

// ============================================================================
// Registration table
// ============================================================================

static const struct luaL_Reg luaLib[] = {
    // Shape / geometry primitives
    {"osci_line", luaLine},
    {"osci_rect", luaRect},
    {"osci_ellipse", luaEllipse},
    {"osci_circle", luaCircle},
    {"osci_arc", luaArc},
    {"osci_polygon", luaPolygon},
    {"osci_bezier", luaBezier},
    {"osci_square", luaSquare},
    {"osci_lissajous", luaLissajous},
    // Waveforms
    {"osci_square_wave", luaSquareWave},
    {"osci_saw_wave", luaSawWave},
    {"osci_triangle_wave", luaTriangleWave},
    {"osci_pulse_wave", luaPulseWave},
    // Transforms
    {"osci_mix", luaMix},
    {"osci_translate", luaTranslate},
    {"osci_scale", luaScale},
    {"osci_rotate", luaRotate},
    // Shape chaining
    {"osci_chain", luaChain},
    // Math / DSP utilities
    {"osci_noise", luaNoise},
    {"osci_lerp", luaLerp},
    {"osci_smoothstep", luaSmoothstep},
    {"osci_clamp", luaClamp},
    {"osci_map", luaMap},
    // Vector math
    {"osci_dot", luaDot},
    {"osci_cross", luaCross},
    {"osci_length", luaLength},
    {"osci_normalize", luaNormalize},
    // Console
    {"print", luaPrint},
    {"clear", luaClear},
    {NULL, NULL}
};

extern int luaopen_oscilibrary(lua_State* L) {
    lua_getglobal(L, "_G");
    luaL_setfuncs(L, luaLib, 0);
    lua_pop(L, 1);
    return 0;
}
