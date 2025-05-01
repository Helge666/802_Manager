from mido import open_input, open_output, get_input_names, get_output_names
import threading
import time
from core.tx802_utils import edit_performance

'''
This module makes shared states available globally for all
tabs, even when they change later on due to user interaction
'''

# PATCH_BANK = ["Init"] * 32
PATCH_BANK = [("Init", i) for i in range(1, 33)]

midi_input = None
midi_output = None
midi_forward_thread = None
_stop_forwarding = threading.Event()


def _midi_forwarding_worker():
    """Worker thread function that forwards MIDI messages from input to output."""
    global midi_input, midi_output, _stop_forwarding

    if not midi_input or not midi_output:
        print("MIDI forwarding started but input or output not configured")
        return

    print(f"🔄 Starting MIDI forwarding from {midi_input.name} to {midi_output.name}")

    try:
        # Use non-blocking receive to avoid freezing the thread on shutdown
        while not _stop_forwarding.is_set():
            for msg in midi_input.iter_pending():
                # Skip SYSEX, clock, and active sensing to reduce traffic
                if msg.type not in ('sysex', 'clock', 'active_sensing'):
                    try:
                        midi_output.send(msg)
                        # print(f"Forwarded: {msg}")  # Uncomment for debugging
                    except Exception as e:
                        print(f"Error forwarding MIDI message: {e}")

            # Short sleep to prevent high CPU usage
            time.sleep(0.001)
    except Exception as e:
        print(f"Error in MIDI forwarding thread: {e}")

    print("MIDI forwarding stopped")


def start_midi_forwarding():
    """Start the MIDI forwarding thread if both input and output are configured."""
    global midi_forward_thread, _stop_forwarding

    if midi_forward_thread and midi_forward_thread.is_alive():
        print("MIDI forwarding already running")
        return False

    if not midi_input or not midi_output:
        print("Cannot start MIDI forwarding: Input or output not configured")
        return False

    _stop_forwarding.clear()
    midi_forward_thread = threading.Thread(
        target=_midi_forwarding_worker,
        daemon=True  # Use daemon thread so it exits when the main program exits
    )
    midi_forward_thread.start()
    return True


def stop_midi_forwarding():
    """Stop the MIDI forwarding thread if it's running."""
    global midi_forward_thread, _stop_forwarding

    if not midi_forward_thread or not midi_forward_thread.is_alive():
        print("MIDI forwarding not running")
        return

    _stop_forwarding.set()
    midi_forward_thread.join(timeout=1.0)  # Wait up to 1 second for thread to exit
    if midi_forward_thread.is_alive():
        print("Warning: MIDI forwarding thread did not exit cleanly")
    else:
        print("MIDI forwarding stopped successfully")


def set_output_port(name, auto_restart_forwarding=True):
    global midi_output
    if midi_output:
        try:
            # Stop forwarding before closing the port
            stop_midi_forwarding()
            midi_output.close()
        except Exception as e:
            print(f"Warning: Could not close previous output port: {e}")
    midi_output = open_output(name)
    print(f"MIDI Output set to: {name}")

    # Try to restart forwarding if input is also configured
    if auto_restart_forwarding and midi_input:
        start_midi_forwarding()

def set_input_port(name, auto_restart_forwarding=True):
    global midi_input
    if midi_input:
        try:
            # Stop forwarding before closing the port
            stop_midi_forwarding()
            midi_input.close()
        except Exception as e:
            print(f"Warning: Could not close previous input port: {e}")
    midi_input = open_input(name)
    print(f"MIDI Input set to: {name}")

    # Try to restart forwarding if output is also configured
    if auto_restart_forwarding and midi_output:
        start_midi_forwarding()


def list_output_ports():
    return get_output_names()


def list_input_ports():
    return get_input_names()


def update_patch_bank(slot, patch_name):
    """Update a specific slot in the global patch bank.

    Args:
        slot (int): Slot number (0-31)
        patch_name (str): Name of the patch (or "Init" for empty slots)
    """
    if 0 <= slot < 32:
        PATCH_BANK[slot] = (patch_name, slot + 1)
    #  print(f"Updated PATCH_BANK slot {slot} with {patch_name}")


# 8 TGs, with all parameters defaulted
tg_states = {tg: {
    "LINK": 0,
    "VNUM": 0,
    "RXCH": 0,
    "NTMTL": 0,
    "NTMTH": 127,
    "DETUNE": 7,
    "NSHFT": 0,
    "OUTVOL": 0,
    "OUTCH": 0,
    "FDAMP": 0
} for tg in range(1, 9)}

def init_tx802_performance_state():
    for tg in range(1, 9):
        for key in tg_states[tg]:
            tg_states[tg][key] = 0
        tg_states[tg]["DETUNE"] = 7
        tg_states[tg]["NTMTH"] = 127
        tg_states[tg]["LINK"] = 0

def update_tg_state(tg, key, value):
    if tg in tg_states and key in tg_states[tg]:
        tg_states[tg][key] = value

def handle_tg_voice_change(tg, vnum):
    update_tg_state(tg, "VNUM", vnum)
    # Optional: also auto-set OUTCH if you want (e.g., L&R by default)
    #  update_tg_state(tg, "OUTCH", 3)

def handle_tg_output_change(tg, output_code):
    update_tg_state(tg, "OUTCH", output_code)
