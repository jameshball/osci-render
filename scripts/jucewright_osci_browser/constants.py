from __future__ import annotations

import re

from pathlib import Path


TEXT_EXAMPLES = ["Hello World", "Greek", "osci-render", "Paperclip", "sosci"]
LUA_EXAMPLES = ["Spiral", "Shape Generator", "Squiggles", "Donut", "Graph", "Gravity Well", "Helix", "Human", "Hypercube", "Mushroom", "Planet"]
MODEL_EXAMPLES = ["Cube", "Diamond", "Dodecahedron", "Humanoid Quad", "Icosahedron", "Lamp", "Shuttle", "Suzanne", "Teapot", "Tetrahedron"]
SVG_EXAMPLES = ["Air Horn", "Alien", "Bicycle", "Card", "Cash", "Cat", "Clippy", "Desktop", "Puzzle", "Skull", "Snowflake", "Yin Yang"]
FRACTAL_EXAMPLES = ["Koch Snowflake", "Sierpinski Triangle", "Dragon Curve", "Binary Tree", "Hilbert Curve"]
EFFECTS = ["Bit Crush", "Bounce", "Bulge", "Dash", "Delay", "Distort", "Duplicator", "God Ray", "Kaleidoscope", "Lua Effect", "Multiplex", "Polygonizer", "Ripple", "Rotate", "Scale", "Skew", "Smoothing", "Spiral Bit Crush", "Swirl", "Trace", "Translate", "Twist", "Unfold", "Vector Cancelling", "Vortex", "Wobble"]
MOD_TABS = ["LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8", "RAND 1", "RAND 2", "RAND 3", "INPUT", "ENV 1", "ENV 2", "ENV 3", "ENV 4", "ENV 5"]

EXAMPLE_INDEX = {name: index for index, name in enumerate([
    "Hello World", "Greek", "osci-render", "Paperclip", "sosci", "Spiral", "Shape Generator", "Squiggles", "Donut", "Graph",
    "Gravity Well", "Helix", "Human", "Hypercube", "Mushroom", "Planet", "Cube", "Diamond", "Dodecahedron",
    "Humanoid Quad", "Icosahedron", "Lamp", "Shuttle", "Suzanne", "Teapot", "Tetrahedron", "Air Horn", "Alien",
    "Bicycle", "Card", "Cash", "Cat", "Clippy", "Desktop", "Puzzle", "Skull", "Snowflake", "Yin Yang",
    "Koch Snowflake", "Sierpinski Triangle", "Dragon Curve", "Binary Tree", "Hilbert Curve",
])}

EFFECT_INDEX = {name: index for index, name in enumerate(EFFECTS)}

MOD_HANDLE_INDEX = {
    "ENV 1": 0,
    "ENV 2": 1,
    "ENV 3": 2,
    "ENV 4": 3,
    "ENV 5": 4,
    "LFO 1": 5,
    "LFO 2": 6,
    "LFO 3": 7,
    "LFO 4": 8,
    "LFO 5": 9,
    "LFO 6": 10,
    "LFO 7": 11,
    "LFO 8": 12,
    "RAND 1": 13,
    "RAND 2": 14,
    "RAND 3": 15,
    "INPUT": 16,
}

SKIP_CONTROL_NAMES = re.compile(
    r"(closeOverlay|Record output|Open Project|Save Project|Save Project As|"
    r"Open visualiser window|Toggle fullscreen visualiser|Shared texture output|"
    r"Open files and examples|Audio input|inputEnabled|midi|Website|Report Issue|Beta updates|"
    r"Download|Purchase|License|Reset|Add |Add$|Remove|Delete|Clear|Pause|"
    r"Play|Stop|Repeat|Record|Randomise|Auto-link|Render Audio|sharedTexture|Syphon|Spout)",
    re.IGNORECASE,
)
