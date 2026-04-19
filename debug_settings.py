#!/usr/bin/env python3
"""
Quick and dirty debug script to extract and inspect .settings files
from osci-render and sosci standalone apps.

The .settings file is a JUCE PropertiesFile (XML) containing:
  - filterState: base64-encoded plugin state from getStateInformation()
  - windowX/windowY: window position
  - audioSetup: audio device configuration
  - shouldMuteInput: mute flag

The filterState decodes to JUCE's copyXmlToBinary format:
  bytes 0-3: magic 0x21324356 (little-endian)
  bytes 4-7: XML string length (little-endian uint32)
  bytes 8+:  XML string (UTF-8)

The inner XML is a <project> element with version, effects, parameters, etc.

Usage:
  python3 debug_settings.py <path-to-settings-file>
  python3 debug_settings.py sosci.settings
  python3 debug_settings.py ~/Library/Application Support/osci-render.settings
"""

import sys
import os
import struct
import xml.etree.ElementTree as ET
from xml.dom import minidom

MAGIC_XML_NUMBER = 0x21324356

# JUCE uses a CUSTOM base64 alphabet (NOT standard RFC 4648):
# ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
# Decoding table: ASCII value - 43 -> 6-bit value
JUCE_B64_DECODE = [
    63, 0, 0, 0, 0, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    0, 0, 0, 0, 0, 0, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52
]


def juce_base64_decode(encoded_str):
    """Decode JUCE's custom base64 format: '<size>.<data>'
    
    JUCE uses LSB-first bit packing within bytes, matching its
    getBitRange/setBitRange implementation.
    """
    dot_idx = encoded_str.find('.')
    if dot_idx < 0:
        raise ValueError("No dot separator found in JUCE base64 string")

    num_bytes = int(encoded_str[:dot_idx])
    data_part = encoded_str[dot_idx + 1:]

    result = bytearray(num_bytes)

    bit_pos = 0
    for ch in data_part:
        c = ord(ch) - 43  # '+' is ASCII 43
        if 0 <= c < len(JUCE_B64_DECODE):
            value = JUCE_B64_DECODE[c]
            # Write 6 bits at bit_pos using JUCE's LSB-first bit ordering
            bits_to_set = value
            byte_idx = bit_pos >> 3
            offset_in_byte = bit_pos & 7
            bits_remaining = 6

            while bits_remaining > 0 and byte_idx < num_bytes:
                bits_this_time = min(bits_remaining, 8 - offset_in_byte)
                mask = ((1 << bits_this_time) - 1)
                result[byte_idx] |= (bits_to_set & mask) << offset_in_byte

                byte_idx += 1
                bits_to_set >>= bits_this_time
                bits_remaining -= bits_this_time
                offset_in_byte = 0

            bit_pos += 6

    return bytes(result)


def read_settings_file(path):
    """Parse the outer .settings XML file."""
    tree = ET.parse(path)
    root = tree.getroot()
    return root


def extract_filter_state(root):
    """Extract the base64 filterState value from the settings XML."""
    for value_elem in root.findall(".//VALUE"):
        if value_elem.get("name") == "filterState":
            return value_elem.get("val")
    return None


def decode_juce_binary_state(juce_b64_data):
    """Decode JUCE's copyXmlToBinary format from custom base64."""
    raw = juce_base64_decode(juce_b64_data)

    if len(raw) < 8:
        return None, raw, "Binary data too short (< 8 bytes)"

    magic = struct.unpack_from("<I", raw, 0)[0]
    if magic != MAGIC_XML_NUMBER:
        # Not JUCE binary format - might be raw XML
        try:
            return ET.fromstring(raw), raw, "Raw XML (no JUCE binary wrapper)"
        except ET.ParseError:
            return None, raw, f"Unknown format (magic=0x{magic:08X}, expected 0x{MAGIC_XML_NUMBER:08X})"

    str_len = struct.unpack_from("<I", raw, 4)[0]
    xml_bytes = raw[8 : 8 + str_len]
    xml_str = xml_bytes.decode("utf-8", errors="replace")

    try:
        xml_root = ET.fromstring(xml_str)
        return xml_root, raw, "JUCE binary format (magic OK)"
    except ET.ParseError as e:
        return None, raw, f"JUCE binary format but XML parse failed: {e}"


def pretty_xml(elem):
    """Pretty-print an XML element."""
    rough = ET.tostring(elem, encoding="unicode")
    return minidom.parseString(rough).toprettyxml(indent="  ")


def extract_info(settings_root, state_xml):
    """Extract key debug information."""
    info = {}

    # Settings-level info
    for value_elem in settings_root.findall(".//VALUE"):
        name = value_elem.get("name")
        if name == "windowX":
            info["windowX"] = value_elem.get("val")
        elif name == "windowY":
            info["windowY"] = value_elem.get("val")
        elif name == "shouldMuteInput":
            info["shouldMuteInput"] = value_elem.get("val")

    # Audio setup
    audio_setup = settings_root.find(".//VALUE[@name='audioSetup']")
    if audio_setup is not None:
        device = audio_setup.find("DEVICESETUP")
        if device is not None:
            info["audioDeviceType"] = device.get("deviceType", "?")
            info["audioOutputDevice"] = device.get("audioOutputDeviceName", "?")
            info["audioInputDevice"] = device.get("audioInputDeviceName", "?")
            info["sampleRate"] = device.get("audioDeviceRate", "?")
            info["bufferSize"] = device.get("audioDeviceBufferSize", "?")

    if state_xml is None:
        return info

    # Plugin state info
    info["version"] = state_xml.get("version", "UNKNOWN")

    # Count effects
    effects = state_xml.findall(".//effect")
    info["effectCount"] = len(effects)
    effect_names = []
    for eff in effects:
        eid = eff.get("id", "?")
        enabled = eff.get("enabled", "?")
        effect_names.append(f"{eid} (enabled={enabled})")
    info["effects"] = effect_names

    # Count parameters
    bool_params = state_xml.findall(".//booleanParameter")
    float_params = state_xml.findall(".//floatParameter")
    int_params = state_xml.findall(".//intParameter")
    info["booleanParameterCount"] = len(bool_params)
    info["floatParameterCount"] = len(float_params)
    info["intParameterCount"] = len(int_params)

    # LFOs
    lfos = state_xml.findall(".//lfo")
    info["lfoCount"] = len(lfos)

    # Envelopes
    envs = state_xml.findall(".//envelope")
    info["envelopeCount"] = len(envs)

    # Files
    files = state_xml.findall(".//file")
    info["fileCount"] = len(files)
    file_names = []
    for f in files:
        fname = f.get("name", "?")
        fext = f.get("ext", "?")
        file_names.append(f"{fname}.{fext}")
    info["files"] = file_names

    # Properties
    props = state_xml.findall(".//property")
    info["propertyCount"] = len(props)
    prop_details = []
    for p in props:
        pname = p.get("key", p.get("name", "?"))
        ptype = p.get("type", "?")
        pval = p.get("value", "?")
        # Truncate long values
        if len(str(pval)) > 80:
            pval = str(pval)[:80] + "..."
        prop_details.append(f"{pname} ({ptype}) = {pval}")
    info["properties"] = prop_details

    # Recording params
    rec = state_xml.find(".//recording")
    if rec is not None:
        info["recording"] = {k: v for k, v in rec.attrib.items()}

    # Custom function
    custom_func = state_xml.find(".//customFunction")
    if custom_func is not None:
        func_b64 = custom_func.get("base64", "")
        if func_b64:
            try:
                func_bytes = juce_base64_decode(func_b64)
                func_text = func_bytes.decode("utf-8", errors="replace")
                info["customFunction"] = func_text[:200] + ("..." if len(func_text) > 200 else "")
            except Exception:
                # Might be standard base64 in older versions
                import base64 as b64mod
                try:
                    func_text = b64mod.b64decode(func_b64).decode("utf-8", errors="replace")
                    info["customFunction"] = func_text[:200] + ("..." if len(func_text) > 200 else "")
                except Exception:
                    info["customFunction"] = f"<encoded, {len(func_b64)} chars>"

    return info


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        # Try default locations
        defaults = [
            os.path.expanduser("~/Library/Application Support/osci-render.settings"),
            os.path.expanduser("~/Library/Application Support/sosci.settings"),
            "osci-render.settings",
            "sosci.settings",
        ]
        found = [p for p in defaults if os.path.isfile(p)]
        if found:
            print(f"Found settings files: {found}")
            print(f"Run: python3 {sys.argv[0]} <path>")
        sys.exit(1)

    path = sys.argv[1]
    if not os.path.isfile(path):
        print(f"Error: File not found: {path}")
        sys.exit(1)

    print(f"{'=' * 60}")
    print(f"Settings File Debug Report")
    print(f"{'=' * 60}")
    print(f"File: {os.path.abspath(path)}")
    print(f"Size: {os.path.getsize(path)} bytes")
    print()

    # Parse outer settings XML
    settings_root = read_settings_file(path)

    # Extract filterState
    filter_state_b64 = extract_filter_state(settings_root)

    if filter_state_b64 is None:
        print("filterState: NOT FOUND (empty settings file)")
        print()
        print("This means no plugin state has been saved yet.")
        print("The app may have crashed before saving state, or")
        print("this is a fresh installation.")
        # Still show other settings
        info = extract_info(settings_root, None)
        if info:
            print()
            print("Other settings found:")
            for k, v in info.items():
                print(f"  {k}: {v}")
        sys.exit(0)

    print(f"filterState: present ({len(filter_state_b64)} base64 chars)")

    # Decode binary state
    state_xml, raw_data, format_info = decode_juce_binary_state(filter_state_b64)
    print(f"Binary data: {len(raw_data)} bytes")
    print(f"Format: {format_info}")

    if state_xml is None:
        print()
        print("ERROR: Could not decode plugin state XML!")
        print(f"First 64 bytes (hex): {raw_data[:64].hex()}")
        sys.exit(1)

    # Extract debug info
    info = extract_info(settings_root, state_xml)

    print()
    print(f"--- Plugin State ---")
    print(f"Version: {info.get('version', 'UNKNOWN')}")
    print()

    print(f"--- Audio Setup ---")
    print(f"  Device Type:  {info.get('audioDeviceType', 'N/A')}")
    print(f"  Output:       {info.get('audioOutputDevice', 'N/A')}")
    print(f"  Input:        {info.get('audioInputDevice', 'N/A')}")
    print(f"  Sample Rate:  {info.get('sampleRate', 'N/A')}")
    print(f"  Buffer Size:  {info.get('bufferSize', 'N/A')}")
    print(f"  Mute Input:   {info.get('shouldMuteInput', 'N/A')}")
    print()

    print(f"--- Window ---")
    print(f"  Position: ({info.get('windowX', '?')}, {info.get('windowY', '?')})")
    print()

    print(f"--- Effects ({info.get('effectCount', 0)}) ---")
    for eff in info.get("effects", []):
        print(f"  {eff}")
    print()

    print(f"--- Parameters ---")
    print(f"  Boolean: {info.get('booleanParameterCount', 0)}")
    print(f"  Float:   {info.get('floatParameterCount', 0)}")
    print(f"  Int:     {info.get('intParameterCount', 0)}")
    print()

    if info.get("lfoCount", 0) > 0:
        print(f"--- LFOs: {info['lfoCount']} ---")
        print()

    if info.get("envelopeCount", 0) > 0:
        print(f"--- Envelopes: {info['envelopeCount']} ---")
        print()

    print(f"--- Files ({info.get('fileCount', 0)}) ---")
    for f in info.get("files", []):
        print(f"  {f}")
    print()

    print(f"--- Properties ({info.get('propertyCount', 0)}) ---")
    for p in info.get("properties", []):
        print(f"  {p}")
    print()

    if "recording" in info:
        print(f"--- Recording ---")
        for k, v in info["recording"].items():
            print(f"  {k}: {v}")
        print()

    if "customFunction" in info:
        print(f"--- Custom Lua Function ---")
        print(f"  {info['customFunction']}")
        print()

    # Option to dump full XML
    if "--xml" in sys.argv:
        print(f"{'=' * 60}")
        print("Full Plugin State XML:")
        print(f"{'=' * 60}")
        print(pretty_xml(state_xml))

    if "--raw-xml" in sys.argv:
        # Just output the raw XML string for piping
        print(ET.tostring(state_xml, encoding="unicode"))


if __name__ == "__main__":
    main()
