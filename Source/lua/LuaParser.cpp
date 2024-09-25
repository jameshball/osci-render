#include "LuaParser.h"

std::function<void(const std::string&)> LuaParser::onPrint;
std::function<void()> LuaParser::onClear;

void LuaParser::maximumInstructionsReached(lua_State* L, lua_Debug* D) {
    lua_getstack(L, 1, D);
    lua_getinfo(L, "l", D);
    
	std::string msg = std::to_string(D->currentline) + ": Maximum instructions reached! You may have an infinite loop.";
	lua_pushstring(L, msg.c_str());
	lua_error(L);
}

void LuaParser::setMaximumInstructions(lua_State*& L, int count) {
    lua_sethook(L, LuaParser::maximumInstructionsReached, LUA_MASKCOUNT, count);
}

void LuaParser::resetMaximumInstructions(lua_State*& L) {
    lua_sethook(L, LuaParser::maximumInstructionsReached, 0, 0);
}

static int pointToTable(lua_State* L, Point point, int numDims) {
    lua_newtable(L);
    if (numDims == 1) {
        lua_pushnumber(L, point.x);
        lua_rawseti(L, -2, 1);
    } else if (numDims == 2) {
        lua_pushnumber(L, point.x);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, point.y);
        lua_rawseti(L, -2, 2);
    } else {
        lua_pushnumber(L, point.x);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, point.y);
        lua_rawseti(L, -2, 2);
        lua_pushnumber(L, point.z);
        lua_rawseti(L, -2, 3);
    }

    return 1;
}

static Point tableToPoint(lua_State* L, int index) {
    Point point;
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

    return point;
}

static int luaLine(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    Point point1 = nargs == 3 ? tableToPoint(L, 2) : Point(-1, -1);
    Point point2 = nargs == 3 ? tableToPoint(L, 3) : Point(1, 1);

    Line line = Line(point1, point2);
    Point point = line.nextVector(t);

    return pointToTable(L, point, 3);
}

static Point genericRect(double phase, Point topLeft, double width, double height) {
    double t = phase / juce::MathConstants<double>::twoPi;
    double totalLength = 2 * (width + height);
    double progress = t * totalLength;

    Line line = Line(0, 0);
    double adjustedProgress;
    double x = topLeft.x;
    double y = topLeft.y;

    if (progress < width) {
        line = Line(x, y, x + width, y);
        adjustedProgress = progress / width;
    } else if (progress < width + height) {
        line = Line(x + width, y, x + width, y + height);
        adjustedProgress = (progress - width) / height;
    } else if (progress < 2 * width + height) {
        line = Line(x + width, y + height, x, y + height);
        adjustedProgress = (progress - width - height) / width;
    } else {
        line = Line(x, y + height, x, y);
        adjustedProgress = (progress - 2 * width - height) / height;
    }

    return line.nextVector(adjustedProgress);
}

static int luaRect(lua_State* L) {
    int nargs = lua_gettop(L);

    double phase = lua_tonumber(L, 1);
    double width = nargs == 1 ? 1 : lua_tonumber(L, 2);
    double height = nargs == 1 ? 1.5 : lua_tonumber(L, 3);

    Point topLeft = nargs == 4 ? tableToPoint(L, 4) : Point(-width / 2, -height / 2);
    Point point = genericRect(phase, topLeft, width, height);
    
    return pointToTable(L, point, 2);
}

static int luaSquare(lua_State* L) {
    int nargs = lua_gettop(L);

    double phase = lua_tonumber(L, 1);
    double width = nargs == 1 ? 1 : lua_tonumber(L,2);

    Point topLeft = nargs == 3 ? tableToPoint(L, 3) : Point(-width / 2, -width / 2);
    Point point = genericRect(phase, topLeft, width, width);
    
    return pointToTable(L, point, 2);
}

static int luaEllipse(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radiusX = nargs == 1 ? 0.6 : lua_tonumber(L, 2);
    double radiuxY = nargs == 1 ? 0.8 : lua_tonumber(L, 3);

    CircleArc ellipse = CircleArc(0, 0, radiusX, radiuxY, 0, juce::MathConstants<double>::twoPi);
    Point point = ellipse.nextVector(t);
    
    return pointToTable(L, point, 2);
}

static int luaCircle(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radius = nargs == 1 ? 0.8 : lua_tonumber(L, 2);

    CircleArc ellipse = CircleArc(0, 0, radius, radius, 0, juce::MathConstants<double>::twoPi);
    Point point = ellipse.nextVector(t);
    
    return pointToTable(L, point, 2);
}

static int luaArc(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    double radiusX = nargs == 1 ? 1 : lua_tonumber(L, 2);
    double radiusY = nargs == 1 ? 1 : lua_tonumber(L, 3);
    double startAngle = nargs == 1 ? 0 : lua_tonumber(L, 4);
    double endAngle = nargs == 1 ? juce::MathConstants<double>::halfPi : lua_tonumber(L, 5);

    CircleArc arc = CircleArc(0, 0, radiusX, radiusY, startAngle, endAngle);
    Point point = arc.nextVector(t);
    
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

    return pointToTable(L, Point(x, y), 2);
}

static int luaBezier(lua_State* L) {
    int nargs = lua_gettop(L);

    double t = lua_tonumber(L, 1) / juce::MathConstants<double>::twoPi;
    Point point1 = nargs == 1 ? Point(-1, -1) : tableToPoint(L, 2);
    Point point2 = nargs == 1 ? Point(-1, 1) : tableToPoint(L, 3);
    Point point3 = nargs == 1 ? Point(1, 1) : tableToPoint(L, 4);

    Point point;

    if (nargs == 5) {
        Point point4 = tableToPoint(L, 5);

        CubicBezierCurve curve = CubicBezierCurve(point1.x, point1.y, point2.x, point2.y, point3.x, point3.y, point4.x, point4.y);
        point = curve.nextVector(t);
    } else {
        QuadraticBezierCurve curve = QuadraticBezierCurve(point1.x, point1.y, point2.x, point2.y, point3.x, point3.y);
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

    return pointToTable(L, Point(x, y), 2);
}

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

static int luaMix(lua_State* L) {
    int nargs = lua_gettop(L);

    Point point1 = tableToPoint(L, 1);
    Point point2 = tableToPoint(L, 2);
    double weight = lua_tonumber(L, 3);

    Point point = Point(
        point1.x * (1 - weight) + point2.x * weight,
        point1.y * (1 - weight) + point2.y * weight,
        point1.z * (1 - weight) + point2.z * weight
    );

    return pointToTable(L, point, 3);
}

static int luaTranslate(lua_State* L) {
    Point point = tableToPoint(L, 1);
    Point translation = tableToPoint(L, 2);

    point.translate(translation.x, translation.y, translation.z);

    return pointToTable(L, point, 3);
}

static int luaScale(lua_State* L) {
    Point point = tableToPoint(L, 1);
    Point scale = tableToPoint(L, 2);

    point.scale(scale.x, scale.y, scale.z);

    return pointToTable(L, point, 3);
}

static int luaRotate(lua_State* L) {
    double nargs = lua_gettop(L);
    bool twoDimRotate = nargs == 2;

    Point point;

    if (twoDimRotate) {
        Point point = tableToPoint(L, 1);
        double angle = lua_tonumber(L, 2);

        point.rotate(0, 0, angle);

        return pointToTable(L, point, 2);
    } else {
        Point point = tableToPoint(L, 1);
        double xRotate = lua_tonumber(L, 2);
        double yRotate = lua_tonumber(L, 3);
        double zRotate = lua_tonumber(L, 4);

        point.rotate(xRotate, yRotate, zRotate);

        return pointToTable(L, point, 3);
    }
}

static int luaPrint(lua_State* L) {
    int nargs = lua_gettop(L);

    for (int i = 1; i <= nargs; ++i) {
        LuaParser::onPrint(lua_tolstring(L, i, nullptr));
        lua_pop(L, 1);
    }

    return 0;
}

static int luaClear(lua_State* L) {
    LuaParser::onClear();
    return 0;
}

static const struct luaL_Reg luaLib[] = {
    {"osci_line", luaLine},
    {"osci_rect", luaRect},
    {"osci_ellipse", luaEllipse},
    {"osci_circle", luaCircle},
    {"osci_arc", luaArc},
    {"osci_polygon", luaPolygon},
    {"osci_bezier", luaBezier},
    {"osci_square", luaSquare},
    {"osci_lissajous", luaLissajous},
    {"osci_square_wave", luaSquareWave},
    {"osci_saw_wave", luaSawWave},
    {"osci_triangle_wave", luaTriangleWave},
    {"osci_mix", luaMix},
    {"osci_translate", luaTranslate},
    {"osci_scale", luaScale},
    {"osci_rotate", luaRotate},
    {"print", luaPrint},
    {"clear", luaClear},
    {NULL, NULL} /* end of array */
};

extern int luaopen_customprintlib(lua_State* L) {
    lua_getglobal(L, "_G");
    luaL_setfuncs(L, luaLib, 0);
    lua_pop(L, 1);
    return 0;
}

LuaParser::LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript) : script(script), fallbackScript(fallbackScript), errorCallback(errorCallback), fileName(fileName) {}

void LuaParser::reset(lua_State*& L, juce::String script) {
    functionRef = -1;

    if (L != nullptr) {
        lua_close(L);
    }
    
    L = luaL_newstate();
    luaL_openlibs(L);
	luaopen_customprintlib(L);
    
    this->script = script;
    parse(L);
}

void LuaParser::reportError(const char* errorChars) {
    std::string error = errorChars;
    std::regex nilRegex = std::regex(R"(attempt to.*nil value.*'slider_\w')");
    // ignore nil errors about global variables, these are likely caused by other errors
    if (std::regex_search(error, nilRegex)) {
        return;
    }

    // remove any newlines from error message
    error = std::regex_replace(error, std::regex(R"(\n|\r)"), "");
    // remove script content from error message
    error = std::regex_replace(error, std::regex(R"(^\[string ".*"\]:)"), "");
    // extract line number from start of error message
    std::regex lineRegex(R"(^(\d+): )");
    std::smatch lineMatch;
    std::regex_search(error, lineMatch, lineRegex);

    if (lineMatch.size() > 1) {
        int line = std::stoi(lineMatch[1]);
        // remove line number from error message
        error = std::regex_replace(error, lineRegex, "");
        errorCallback(line, fileName, error);
    }
}

void LuaParser::parse(lua_State*& L) {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        reportError(error);
        lua_pop(L, 1);
        revertToFallback(L);
    } else {
        functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, double value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, int value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariables(lua_State*& L, LuaVariables& vars) {
	setGlobalVariable(L, "step", vars.step);
	setGlobalVariable(L, "sample_rate", vars.sampleRate);
	setGlobalVariable(L, "frequency", vars.frequency);
	setGlobalVariable(L, "phase", vars.phase);

    for (int i = 0; i < NUM_SLIDERS; i++) {
		setGlobalVariable(L, SLIDER_NAMES[i], vars.sliders[i]);
    }

    setGlobalVariable(L, "ext_x", vars.ext_x);
    setGlobalVariable(L, "ext_y", vars.ext_y);

    if (vars.isEffect) {
        setGlobalVariable(L, "x", vars.x);
        setGlobalVariable(L, "y", vars.y);
		setGlobalVariable(L, "z", vars.z);
    }
}

void LuaParser::incrementVars(LuaVariables& vars) {
    vars.step++;
    vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
    if (vars.phase > 2 * std::numbers::pi) {
        vars.phase -= 2 * std::numbers::pi;
    }
}

void LuaParser::clearStack(lua_State*& L) {
    lua_settop(L, 0);
}

void LuaParser::revertToFallback(lua_State*& L) {
    functionRef = -1;
    usingFallbackScript = true;
    if (script != fallbackScript) {
        reset(L, fallbackScript);
    }
}

void LuaParser::readTable(lua_State*& L, std::vector<float>& values) {
    auto length = lua_objlen(L, -1);

    for (int i = 1; i <= length; i++) {
        lua_pushinteger(L, i);
        lua_gettable(L, -2);
        float value = lua_tonumber(L, -1);
        lua_pop(L, 1);
        values.push_back(value);
    }
}

// only the audio thread runs this fuction
std::vector<float> LuaParser::run(lua_State*& L, LuaVariables& vars) {
    // if we haven't seen this state before, reset it
    int stateIndex = std::find(seenStates.begin(), seenStates.end(), L) - seenStates.begin();
    if (stateIndex == seenStates.size()) {
        reset(L, script);
        seenStates.push_back(L);
    }

    std::vector<float> values;
	
	setGlobalVariables(L, vars);
    
	// Get the function from the registry
	lua_rawgeti(L, LUA_REGISTRYINDEX, functionRef);

    setMaximumInstructions(L, 5000000);
    
    if (lua_isfunction(L, -1)) {
        const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (ret != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            reportError(error);
            revertToFallback(L);
        } else {            
            if (lua_istable(L, -1)) {
                readTable(L, values);
            }
        }
    } else {
        revertToFallback(L);
    }

    resetMaximumInstructions(L);

    if (functionRef != -1 && !usingFallbackScript) {
        resetErrors();
    }

	clearStack(L);
    
	incrementVars(vars);
    
	return values;
}

bool LuaParser::isFunctionValid() {
    return functionRef != -1;
}

juce::String LuaParser::getScript() {
    return script;
}

void LuaParser::resetErrors() {
    errorCallback(-1, fileName, "");
}

void LuaParser::close(lua_State*& L) {
    if (L != nullptr) {
        lua_close(L);
    }
}
