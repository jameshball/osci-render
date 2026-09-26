"""CoreMIDI virtual source for macOS standalone input tests (no audio playback)."""
import ctypes

CORE_MIDI = ctypes.CDLL('/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI')
CORE_FOUNDATION = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
LIB_SYSTEM = ctypes.CDLL('/usr/lib/libSystem.B.dylib')

OSStatus = ctypes.c_int32
MIDIClientRef = ctypes.c_uint32
MIDIEndpointRef = ctypes.c_uint32
MIDITimeStamp = ctypes.c_uint64
CFStringRef = ctypes.c_void_p
kCFStringEncodingUTF8 = 0x08000100

CORE_FOUNDATION.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
CORE_FOUNDATION.CFStringCreateWithCString.restype = CFStringRef
CORE_FOUNDATION.CFRelease.argtypes = [ctypes.c_void_p]
CORE_FOUNDATION.CFRelease.restype = None
CORE_MIDI.MIDIClientCreate.argtypes = [CFStringRef, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(MIDIClientRef)]
CORE_MIDI.MIDIClientCreate.restype = OSStatus
CORE_MIDI.MIDISourceCreate.argtypes = [MIDIClientRef, CFStringRef, ctypes.POINTER(MIDIEndpointRef)]
CORE_MIDI.MIDISourceCreate.restype = OSStatus
CORE_MIDI.MIDIEndpointDispose.argtypes = [MIDIEndpointRef]
CORE_MIDI.MIDIEndpointDispose.restype = OSStatus
CORE_MIDI.MIDIClientDispose.argtypes = [MIDIClientRef]
CORE_MIDI.MIDIClientDispose.restype = OSStatus
# Use the C packet-list helpers rather than re-declaring their architecture-specific packing.
CORE_MIDI.MIDIPacketListInit.argtypes = [ctypes.c_void_p]
CORE_MIDI.MIDIPacketListInit.restype = ctypes.c_void_p
CORE_MIDI.MIDIPacketListAdd.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, MIDITimeStamp, ctypes.c_size_t, ctypes.POINTER(ctypes.c_ubyte)]
CORE_MIDI.MIDIPacketListAdd.restype = ctypes.c_void_p
CORE_MIDI.MIDIReceived.argtypes = [MIDIEndpointRef, ctypes.c_void_p]
CORE_MIDI.MIDIReceived.restype = OSStatus
CORE_MIDI.MIDIGetNumberOfSources.argtypes = []
CORE_MIDI.MIDIGetNumberOfSources.restype = ctypes.c_size_t
CORE_MIDI.MIDIGetSource.argtypes = [ctypes.c_size_t]
CORE_MIDI.MIDIGetSource.restype = MIDIEndpointRef
LIB_SYSTEM.mach_absolute_time.argtypes = []
LIB_SYSTEM.mach_absolute_time.restype = ctypes.c_uint64


def check(status, action):
    if status != 0:
        raise RuntimeError(f'{action} failed: CoreMIDI OSStatus {status}')


def cf_string(text):
    value = CORE_FOUNDATION.CFStringCreateWithCString(None, text.encode('utf-8'), kCFStringEncodingUTF8)
    if not value:
        raise RuntimeError('could not create CoreFoundation string')
    return value


class VirtualSource:
    def __init__(self, name):
        self.client = MIDIClientRef()
        self.endpoint = MIDIEndpointRef()
        client_name = cf_string('Motion virtual MIDI test helper')
        source_name = cf_string(name)
        try:
            check(CORE_MIDI.MIDIClientCreate(client_name, None, None, ctypes.byref(self.client)), 'MIDIClientCreate')
            check(CORE_MIDI.MIDISourceCreate(self.client, source_name, ctypes.byref(self.endpoint)), 'MIDISourceCreate')
        except Exception:
            self.close()
            raise
        finally:
            CORE_FOUNDATION.CFRelease(client_name)
            CORE_FOUNDATION.CFRelease(source_name)

    def send(self, data):
        if not data or len(data) > 256 or any(not isinstance(x, int) or x < 0 or x > 255 for x in data):
            raise ValueError('MIDI packet data must contain 1-256 byte values')
        # 1024 is ample for this one short MIDI 1.0 packet. CoreMIDI’s helper
        # functions handle MIDIPacket alignment on both Intel and Apple Silicon.
        storage = ctypes.create_string_buffer(1024)
        packet = CORE_MIDI.MIDIPacketListInit(storage)
        bytes_ = (ctypes.c_ubyte * len(data))(*data)
        packet = CORE_MIDI.MIDIPacketListAdd(storage, ctypes.sizeof(storage), packet,
                                             LIB_SYSTEM.mach_absolute_time(), len(data), bytes_)
        if not packet:
            raise RuntimeError('MIDIPacketListAdd ran out of packet storage')
        check(CORE_MIDI.MIDIReceived(self.endpoint, storage), 'MIDIReceived')

    def source_index(self):
        # JUCE's CoreMIDI input enumeration preserves this source order.
        for index in range(CORE_MIDI.MIDIGetNumberOfSources()):
            if CORE_MIDI.MIDIGetSource(index) == self.endpoint.value:
                return index
        raise RuntimeError('Virtual MIDI source disappeared before selection')

    def close(self):
        if self.endpoint.value:
            CORE_MIDI.MIDIEndpointDispose(self.endpoint)
            self.endpoint = MIDIEndpointRef()
        if self.client.value:
            CORE_MIDI.MIDIClientDispose(self.client)
            self.client = MIDIClientRef()
