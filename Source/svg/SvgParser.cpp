#include "SvgParser.h"
#include <regex>
#include "MoveTo.h"
#include "LineTo.h"
#include "CurveTo.h"
#include "EllipticalArcTo.h"
#include "ClosePath.h"


SvgParser::SvgParser(juce::String svgFile) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(svgFile.toStdString().c_str());
	
	if (result) {
		pugi::xml_node svg = doc.child("svg");

		shapes = std::vector<std::unique_ptr<Shape>>();

		for (pugi::xml_node path : svg.children("path")) {
			for (pugi::xml_attribute attr : path.attributes()) {
				if (std::strcmp(attr.name(), "d") == 0) {
					auto path = parsePath(juce::String(attr.value()));
					shapes.insert(shapes.end(), std::make_move_iterator(path.begin()), std::make_move_iterator(path.end()));
				}
			}
		}
		
		std::pair<double, double> dimensions = getDimensions(svg);

		if (dimensions.first != -1.0 && dimensions.second != -1.0) {
			double width = dimensions.first;
			double height = dimensions.second;
			
			Shape::normalize(shapes, width, height);
		} else {
			Shape::normalize(shapes);
		}
	}
}

SvgParser::~SvgParser() {
}

std::vector<std::unique_ptr<Shape>> SvgParser::draw() {
	// clone with deep copy
	std::vector<std::unique_ptr<Shape>> tempShapes = std::vector<std::unique_ptr<Shape>>();
	for (auto& shape : shapes) {
		tempShapes.push_back(shape->clone());
	}
	return tempShapes;
}

std::vector<std::string> SvgParser::preProcessPath(std::string path) {
	std::regex re1(",");
	std::regex re2("-(?!e)");
	std::regex re3("\\s+");
	std::regex re4("(^\\s|\\s$)");

	path = std::regex_replace(path, re1, " ");
	// reverse path and use a negative lookahead 
	std::reverse(path.begin(), path.end());
	path = std::regex_replace(path, re2, "- ");
	std::reverse(path.begin(), path.end());
	path = std::regex_replace(path, re3, " ");
	path = std::regex_replace(path, re4, "");

	std::regex commands("(?=[mlhvcsqtazMLHVCSQTAZ])");
	std::vector<std::string> commandsVector;
	
	std::sregex_token_iterator iter(path.begin(), path.end(), commands, -1);
	
	for (; iter != std::sregex_token_iterator(); ++iter) {
		commandsVector.push_back(iter->str());
	}

	return commandsVector;
}

juce::String SvgParser::simplifyLength(juce::String length) {
	return length.replace("em|ex|px|in|cm|mm|pt|pc", "");
}

std::pair<double, double> SvgParser::getDimensions(pugi::xml_node& svg) {
	double width = -1.0;
	double height = -1.0;

	if (!svg.attribute("viewBox").empty()) {
		juce::String viewBox = juce::String(svg.attribute("viewBox").as_string());
		juce::StringArray viewBoxValues = juce::StringArray::fromTokens(viewBox, " ", "");

		if (viewBoxValues.size() == 4) {
			width = viewBoxValues[2].getDoubleValue();
			height = viewBoxValues[3].getDoubleValue();
		}
	}

	std::regex re("em|ex|px|in|cm|mm|pt|pc");

	if (!svg.attribute("width").empty()) {
		std::string widthString = svg.attribute("width").as_string();
		widthString = std::regex_replace(widthString, re, "");
		width = juce::String(widthString).getDoubleValue();
	}

	if (!svg.attribute("height").empty()) {
		std::string heightString = svg.attribute("height").as_string();
		heightString = std::regex_replace(heightString, re, "");
		height = juce::String(heightString).getDoubleValue();
	}

	return std::make_pair<>(width, height);
}

std::vector<std::unique_ptr<Shape>> SvgParser::parsePath(juce::String path) {
	if (path.isEmpty()) {
		return std::vector<std::unique_ptr<Shape>>();
	}
	
	state.currPoint = Vector2();
	state.prevCubicControlPoint = std::nullopt;
	state.prevQuadraticControlPoint = std::nullopt;

	std::vector<std::string> commands = preProcessPath(path.toStdString());
	std::vector<std::unique_ptr<Shape>> pathShapes;
	
	for (auto& stdCommand : commands) {
		if (!stdCommand.empty()) {
			juce::String command(stdCommand);
			char commandChar = command[0];
			std::vector<float> nums;

			if (commandChar != 'z' && commandChar != 'Z') {
				juce::StringArray tokens = juce::StringArray::fromTokens(command.substring(1), " ", "");

				for (juce::String token : tokens) {
					token = token.trim();
					if (token.isNotEmpty()) {
						juce::StringArray decimalSplit = juce::StringArray::fromTokens(token, ".", "");
						if (decimalSplit.size() == 1) {
							auto num = decimalSplit[0].getFloatValue();
							nums.push_back(num);
						} else {
							juce::String decimal = decimalSplit[0] + "." + decimalSplit[1];
							nums.push_back(decimal.getFloatValue());

							for (int i = 2; i < decimalSplit.size(); i++) {
								decimal = "." + decimalSplit[i];
								nums.push_back(decimal.getFloatValue());
							}
						}
					}
				}
			}

			std::vector<std::unique_ptr<Shape>> commandShapes;

			switch (commandChar) {
			case 'M':
				commandShapes = MoveTo::absolute(state, nums);
				break;
			case 'm':
				commandShapes = MoveTo::relative(state, nums);
				break;
			case 'L':
				commandShapes = LineTo::absolute(state, nums);
				break;
			case 'l':
				commandShapes = LineTo::relative(state, nums);
				break;
			case 'H':
				commandShapes = LineTo::horizontalAbsolute(state, nums);
				break;
			case 'h':
				commandShapes = LineTo::horizontalRelative(state, nums);
				break;
			case 'V':
				commandShapes = LineTo::verticalAbsolute(state, nums);
				break;
			case 'v':
				commandShapes = LineTo::verticalRelative(state, nums);
				break;
			case 'C':
				commandShapes = CurveTo::absolute(state, nums);
				break;
			case 'c':
				commandShapes = CurveTo::relative(state, nums);
				break;
			case 'S':
				commandShapes = CurveTo::smoothAbsolute(state, nums);
				break;
			case 's':
				commandShapes = CurveTo::smoothRelative(state, nums);
				break;
			case 'Q':
				commandShapes = CurveTo::quarticAbsolute(state, nums);
				break;
			case 'q':
				commandShapes = CurveTo::quarticRelative(state, nums);
				break;
			case 'T':
				commandShapes = CurveTo::quarticSmoothAbsolute(state, nums);
				break;
			case 't':
				commandShapes = CurveTo::quarticSmoothRelative(state, nums);
				break;
			case 'A':
				commandShapes = EllipticalArcTo::absolute(state, nums);
				break;
			case 'a':
				commandShapes = EllipticalArcTo::relative(state, nums);
				break;
			case 'Z':
				commandShapes = ClosePath::absolute(state, nums);
				break;
			case 'z':
				commandShapes = ClosePath::relative(state, nums);
				break;
			}

			pathShapes.insert(pathShapes.end(), std::make_move_iterator(commandShapes.begin()), std::make_move_iterator(commandShapes.end()));

			if (commandChar != 'c' && commandChar != 'C' && commandChar != 's' && commandChar != 'S') {
				state.prevCubicControlPoint = std::nullopt;
			}
			if (commandChar != 'q' && commandChar != 'Q' && commandChar != 't' && commandChar != 'T') {
				state.prevQuadraticControlPoint = std::nullopt;
			}
		}
	}

	return pathShapes;
}
