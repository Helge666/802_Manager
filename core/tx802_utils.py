import mido
import mido.backends.rtmidi
from mido.ports import BaseOutput
import traceback
import time
import json
import os

from starlette.templating import pass_context

# Constants
##################################################
CONFIG_JSON = "802_manager_settings.json"
CONFIG_FILE_NAME = os.path.join(os.path.dirname(__file__), "..", "config", CONFIG_JSON)
CONFIG_FILE_NAME = os.path.abspath(CONFIG_FILE_NAME)

YAMAHA_ID = 0x43
# Group 6 (00110), Subgroup 2 (10) = 00011010 = 0x1A for PCED parameter change
PCED_PARAM_CHANGE_GROUP_SUBGROUP_BYTE = 0x1A
# Group 6 (00110), Subgroup 3 (11) = 00011011 = 0x1B for Remote Switch parameter change
REMOTE_SWITCH_GROUP_SUBGROUP_BYTE = 0x1B
VMEM_VOICE_SIZE = 128  # Size of each voice in bytes (calculated from total size)
VMEM_HEADER_SIZE = 6   # F0 43 0n 09 20 00
VMEM_HEADER_START = bytes([0x09, 0x20, 0x00])  # Following F0 43 0n
VMEM_EXPECTED_SIZE = 4104
# Performance Memory (PMEM) constants
PMEM_HEADER_START = bytes([0x7E, 0x01, 0x28])  # Following F0 43 0n
PMEM_IDENTIFIER = b'LM--8952PM'  # The identifier string that follows
PMEM_BULK_SIZE = 11589  # Total bulk size as mentioned in the spec

# MIDI Port functions
##################################################
def load_config():
    if os.path.exists(CONFIG_FILE_NAME):
        try:
            with open(CONFIG_FILE_NAME, 'r') as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError) as e:
            print(f"Warning: Could not load config: {e}")
    return {}

def save_config(config):
    try:
        with open(CONFIG_FILE_NAME, 'w') as f:
            json.dump(config, f, indent=4)
        print(f"Configuration saved to '{CONFIG_FILE_NAME}'")
    except IOError as e:
        print(f"Warning: Could not save config: {e}")

def select_midi_ports(config):
    print("\nScanning for MIDI ports...")
    try:
        available_out_ports = mido.get_output_names()
        available_in_ports = mido.get_input_names()
    except Exception as e:
        print(f"Error scanning for MIDI ports: {e}")
        traceback.print_exc()
        return None, None, False

    if not available_out_ports or not available_in_ports:
        print("Error: No MIDI ports found.")
        return None, None, False

    print("\nAvailable MIDI Output Ports:")
    for i, port in enumerate(available_out_ports):
        print(f"  {i + 1}: {port}")

    saved_out = config.get('output_port')
    selected_out = None
    selected_in = None

    while True:
        # Output port
        prompt_out = "Select MIDI Output Port number"
        if saved_out and saved_out in available_out_ports:
            default_out_idx = available_out_ports.index(saved_out) + 1
            prompt_out += f" (default: {default_out_idx} - {saved_out})"
        prompt_out += ": "

        try:
            choice_out = input(prompt_out)
            if not choice_out and saved_out in available_out_ports:
                selected_out = saved_out
                print(f"Using saved output port: {saved_out}")
            else:
                selected_out = available_out_ports[int(choice_out) - 1]

            # Test opening
            mido.open_output(selected_out).close()
            return selected_out, selected_in, True
        except (ValueError, IndexError):
            print("Invalid input. Please enter a valid number.")
        except Exception as e:
            print(f"Error opening ports: {e}")
            traceback.print_exc()
            retry = input("Retry port selection? (y/n): ").lower()
            if retry != 'y':
                return None, None, False

def resolve_output_port(output_port, config=None):
    """
    Resolves the output port, handling both already open port objects and port names.
    Should be used by all functions that require MIDI iports, but isn't yet.

    Args:
        output_port: Can be an open port object, a port name, or None
        config: Optional configuration dictionary (loaded if None)

    Returns:
        tuple: (port_object, port_name, should_close, ports_ok)
            - port_object: The opened port object or None
            - port_name: Name of the port or None
            - should_close: True if this function opened the port (caller should close it)
            - ports_ok: True if port resolution succeeded
    """
    # Check if output_port is already an open port object
    is_port_object = isinstance(output_port, BaseOutput) and not getattr(output_port, 'closed', True)

    if is_port_object:
        # Already open port object provided
        return output_port, None, False, True
    else:
        # Original code path for port name or None
        if config is None:
            config = load_config()

        # Determine port name
        if output_port and isinstance(output_port, str):
            out_port_name = output_port
            ports_ok = out_port_name in mido.get_output_names()
            in_port_name = None  # Not selecting input port in this case
        else:
            # Use port selection dialog
            out_port_name, in_port_name, ports_ok = select_midi_ports(config)
            if ports_ok and out_port_name:
                config['output_port'] = out_port_name
                if in_port_name:
                    config['input_port'] = in_port_name
                save_config(config)

        if not ports_ok or not out_port_name:
            return None, None, False, False

        # Open the selected port
        try:
            print(f"\nOpening Output Port: {out_port_name}")
            port_object = mido.open_output(out_port_name)
            print("Output Port opened successfully.")
            return port_object, out_port_name, True, True  # Caller should close this port
        except Exception as e:
            print(f"Error opening MIDI port '{out_port_name}': {e}")
            traceback.print_exc()
            return None, out_port_name, False, False

# MIDI note functions
##################################################
def play_test_notes(port):
    if not port or port.closed:
        print("Output port not open. Cannot play notes.")
        return

    print("\nPlaying test notes (C4, E4, G4)...")
    notes = [60, 64, 67]
    velocity = 100
    duration = 0.5
    chord_duration = 2.0  # Duration for the chord

    try:
        # Play individual notes
        for note in notes:
            port.send(mido.Message('note_on', channel=0, note=note, velocity=velocity))
            time.sleep(duration)
            port.send(mido.Message('note_off', channel=0, note=note, velocity=0))
            time.sleep(0.1)

        # Play all notes together as a chord
        print("Playing chord...")
        # Send note_on for all notes
        for note in notes:
            port.send(mido.Message('note_on', channel=0, note=note, velocity=velocity))

        # Hold for chord duration
        time.sleep(chord_duration)

        # Send note_off for all notes
        for note in notes:
            port.send(mido.Message('note_off', channel=0, note=note, velocity=0))

        print("Test notes and chord finished.")
    except Exception as e:
        print(f"Error playing notes: {e}")
        traceback.print_exc()


# Sysex functions
##################################################
def to_ascii_hex(value):
    """
    Converts an integer (0-127) to a list of two ASCII byte values
    representing its hexadecimal value (00 to 7F).
    """
    hex_str = f"{value:02X}"
    return [ord(c) for c in hex_str]

def send_sysex_message(port, message_data, description="", delay_after=0.1):
    """
    Sends a System Exclusive message to the specified port.
    message_data should NOT include the F0 start and F7 end bytes - mido adds these automatically.
    """

    try:
        # When using mido, you don't include F0 and F7 in the data parameter
        # mido adds these automatically
        if message_data[0] == 0xF0:
            message_data = message_data[1:]  # Remove F0
        if message_data[-1] == 0xF7:
            message_data = message_data[:-1]  # Remove F7

        # Check for any bytes > 127
        for i, byte in enumerate(message_data):
            if byte > 127:
                print(f"Warning: Byte 0x{byte:02X} > 127 in '{description}'.")
                return False

        msg = mido.Message('sysex', data=message_data)
        print(f"Sending {description}")
        # Uncomment to print sysex to console
        print(msg.hex())
        port.send(msg)
        if delay_after > 0:
            time.sleep(delay_after)
        return True
    except Exception as e:
        print(f"Error sending SysEx ({description}): {str(e)}")
        traceback.print_exc()
        return False

# Performance functions
##################################################
def edit_performance(port, device_id=1, delay_after=0.05, play_notes=False, **kwargs):
    """
    Edits parameters in the TX802 Performance Edit Buffer (PCED) via SysEx Parameter Change messages.

    Parameters are specified using their user-facing values (e.g., Channel 1-16, Voice 1-128).
    The function handles conversion to the internal 0-based representation where necessary.

    Supported Parameters (Case-Insensitive Keys):
    - VNUM<TG> (TG=1-8): Voice Number (1-256) -> Internal 0-255
    - PRESET<TG> (TG=1-8): Bank preset I01–I64, C01–C64, A01–A64 or B01–B64, converts to VNUM
    - VCHOFS<TG> (TG=1-8): Voice Channel Offset (0-7)
    - RXCH<TG> (TG=1-8): MIDI Receive Channel (1-16 or 'Omni') -> Internal 0-15, 16=OMNI
    - DETUNE<TG> (TG=1-8): Detune (0-14, 7=Center / optionally: -7 to +7 with 0=no detune)
    - OUTVOL<TG> (TG=1-8): Output Volume (0-99)
    - OUTCH<TG> (TG=1-8): Output Assign (0: off, 1: I/L, 2: II/R, 3: I+II / L+R)
    - NTMTL<TG> (TG=1-8): Note Limit Low (0-127, C-2 to G8)
    - NTMTH<TG> (TG=1-8): Note Limit High (0-127, C-2 to G8)
    - NOTELOW<TG> (TG=1-8): MIDI Note Low Limit specified as note name (C-2 to G8); converted internally to NTMTL<TG>
    - NOTEHIGH<TG> (TG=1-8): MIDI Note High Limit specified as note name (C-2 to G8); converted internally to NTMTH<TG>
    - NOTESHIFT<TG> (TG=1-8): MIDI Note Shift (0-48, 24=Center / optionally: -24 to +24 with 0=no shift)
    - NSHFT<TG> (TG=1-8): Alias for NOTESHIFT for backwards compatibility
    - FDAMP<TG> (TG=1-8): EG Forced Damp (0: off, 1: on)
    - PAN<TG> (TG=1-8): Panning; accepts Off, I/Left, II/Right, I+II/Center; maps to OUTCH<TG>
    - LINK<TG> (TG=1-8): 0 unlinks/switches OFF the TG; self (1-8) links/switches ON the TG
    - TG<TG> (TG=1-8): Tone Generator On/Off; accepts On → LINK<TG>=<TG>, Off → LINK<TG>=0
    - PNAM<Index> (Index=1-20): Performance Name Character (ASCII char or code 0-127)
    """
    print(f"\n--- Editing Performance Parameters (Device ID: {device_id}) ---")

    # Ensure output port is open
    # if not port or port.closed: # Use this with a real mido port
    if not port: # Modified for dummy testing
        print("Error: Output port is not open.")
        return False

    # Prepare device ID byte (0x10 for ID 1, 0x11 for ID 2, ..., 0x1F for ID 16)
    internal_device_id = device_id - 1
    if not 0 <= internal_device_id <= 15:
        print(f"Warning: Invalid device ID {device_id}. Using 1.")
        internal_device_id = 0
        device_id = 1  # Reflect the change in device_id
    device_id_byte = 0x10 + internal_device_id

    # Parameter Mapping: human_name -> (param_num, user_min, user_max, type, needs_adjustment)
    # user_min/user_max are the expected input range from the user.
    # needs_adjustment: True if user value needs -1 for internal representation (except special cases).
    # Type can be 'int' or 'char'
    param_map = {}
    for tg in range(1, 9):  # TG 1 to 8
        param_map[f'KASG{tg}'] = (80 + (tg - 1), 0, 1, 'kasg', False)
        param_map[f'VCHOFS{tg}'] = (0 + (tg - 1), 0, 7, 'int', False)
        param_map[f'RXCH{tg}'] = (8 + (tg - 1), 1, 16, 'int', True) # User inputs 1-16 (16=OMNI)
        param_map[f'VNUM{tg}'] = (16 + (tg - 1), 1, 256, 'int', True) # User inputs 1-255
        param_map[f'DETUNE{tg}'] = (24 + (tg - 1), 0, 14, 'int', False)
        param_map[f'OUTVOL{tg}'] = (32 + (tg - 1), 0, 99, 'int', False)
        param_map[f'OUTCH{tg}'] = (40 + (tg - 1), 0, 3, 'int', False)
        param_map[f'NTMTL{tg}'] = (48 + (tg - 1), 0, 127, 'int', False)
        param_map[f'NTMTH{tg}'] = (56 + (tg - 1), 0, 127, 'int', False)
        param_map[f'NSHFT{tg}'] = (64 + (tg - 1), 0, 48, 'int', False)
        param_map[f'FDAMP{tg}'] = (72 + (tg - 1), 0, 1, 'int', False)
        param_map[f'LINK{tg}'] = (tg - 1, 0, 8, 'int', True)
    for idx in range(1, 21):  # PNAM 1 to 20 (Performance name allows for 20 chars)
        param_map[f'PNAM{idx}'] = (96 + (idx - 1), 0, 127, 'char', False) # ASCII range

    all_success = True

    for key, value in kwargs.items():
        key_upper = key.upper()

        # Optional Parameter Mappings
        # ―――――――――――――――――――――――――――――――――――
        if key_upper.startswith("NOTELOW"):
            # TG extrahieren (Zahl 1–8)
            tg = key_upper[len("NOTELOW"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                midi_name = str(value).upper()
                if midi_name in MIDI_NOTES:
                    numeric = MIDI_NOTES.index(midi_name)
                    # Umwandlung auf den internen Parameternamen/-wert
                    key_upper = f"NTMTL{tg}"
                    value = numeric
                else:
                    print(f"Warning: Unknown MIDI note name '{value}' for NOTELOW{tg}. Skipping.")
                    all_success = False
                    continue
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue

        if key_upper.startswith("NOTEHIGH"):
            # TG extrahieren (Zahl 1–8)
            tg = key_upper[len("NOTEHIGH"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                midi_name = str(value).upper()
                if midi_name in MIDI_NOTES:
                    numeric = MIDI_NOTES.index(midi_name)
                    key_upper = f"NTMTH{tg}"
                    value = numeric
                else:
                    print(f"Warning: Unknown MIDI note name '{value}' for NOTEHIGH{tg}. Skipping.")
                    all_success = False
                    continue
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue

        if key_upper.startswith("PAN"):
            # TG extrahieren (Zahl 1–8)
            tg = key_upper[len("PAN"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                val_str = str(value).upper()
                pan_map = {
                    'OFF': 0,
                    'I': 1, 'LEFT': 1,
                    'II': 2, 'RIGHT': 2,
                    'I+II': 3, 'CENTER': 3
                }
                if val_str in pan_map:
                    numeric = pan_map[val_str]
                    # Intern zu OUTCH<TG> umwandeln
                    key_upper = f"OUTCH{tg}"
                    value = numeric
                else:
                    print(f"Warning: Unknown PAN value '{value}' for PAN{tg}. Skipping.")
                    all_success = False
                    continue
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue

        if key_upper.startswith("TG"):
            # TG-Nummer extrahieren (1–8)
            tg = key_upper[len("TG"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                val_str = str(value).upper()
                if val_str == 'ON':
                    # On → LINK<TG>=<TG>
                    key_upper = f"LINK{tg}"
                    value = int(tg)
                elif val_str == 'OFF':
                    # Off → LINK<TG>=0
                    key_upper = f"LINK{tg}"
                    value = 0
                else:
                    print(f"Warning: Unknown TG value '{value}' for TG{tg}. Use On or Off. Skipping.")
                    all_success = False
                    continue
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue

        # Allow "Omni" string for RXCH<TG> → internal 16
        if key_upper.startswith("RXCH"):
            if isinstance(value, str) and value.strip().upper() == "OMNI":
                value = 16

        # Allow for Preset instead of VNUM to match the unit's nomenclature
        if key_upper.startswith("PRESET"):
            # TG-Nummer extrahieren (1–8)
            tg = key_upper[len("PRESET"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                preset_str = str(value).upper()
                # Muster: Bankbuchstabe (I/C/A/B) + zweistellige Zahl 01–64
                import re
                m = re.fullmatch(r'([ICAB])([0-6]\d)', preset_str)
                if m:
                    bank, num_str = m.groups()
                    num = int(num_str)
                    if 1 <= num <= 64:
                        # Bank-Offsets definieren
                        base = {'I': 0, 'C': 64, 'A': 128, 'B': 192}[bank]
                        numeric = base + (num - 1)
                        # Auf VNUM<TG> mappen und +1, damit VNUM intern korrekt (value-1) → numeric
                        key_upper = f"VNUM{tg}"
                        value = numeric + 1
                    else:
                        print(f"Warning: Number {num} for PRESET{tg} out of range (01-64). Skipping.")
                        all_success = False
                        continue
                else:
                    print(f"Warning: Invalid format '{value}' for PRESET{tg}. Expected I01–I64, C01–C64, A01–A64, B01–B64.")
                    all_success = False
                    continue
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue

        # Alias: NOTESHIFT<TG> as NSHFT<TG>
        if key_upper.startswith("NOTESHIFT"):
            # Extract TG-Nummer
            tg = key_upper[len("NOTESHIFT"):]
            if tg.isdigit() and 1 <= int(tg) <= 8:
                # just rename, valuue remains
                key_upper = f"NSHFT{tg}"
            else:
                print(f"Warning: Invalid TG '{tg}' for {key}. Skipping.")
                all_success = False
                continue
        # ―――――――――――――――――――――――――――――――――――

        mapped_param = None
        param_name = None

        # Lookup parameter (case-insensitive)
        for map_key, map_val in param_map.items():
            if map_key.upper() == key_upper:
                mapped_param = map_val
                param_name = map_key  # preserve the proper case for reporting
                break

        if not mapped_param:
            print(f"Warning: Skipping unknown or unsupported parameter '{key}'.")
            all_success = False
            continue

        param_num, user_min, user_max, param_type, needs_adjustment = mapped_param
        internal_value = None # This will hold the final value to be sent

        # --- Value Validation and Conversion ---
        try:
            if param_type == 'int':
                user_value = int(value)
                # ――― Special DETUNE Handling: allow -7..+7 around center ―――
                if param_name.startswith("DETUNE"):
                    # relative ±7 → internal 0–14
                    if -7 <= user_value <= 7:
                        internal_value = user_value + 7
                    # absolute 0–14 as before
                    elif 0 <= user_value <= user_max:
                        internal_value = user_value
                    else:
                        print(f"Warning: Value {user_value} for '{param_name}' out of allowed range (-7 to +{user_max}). Skipping.")
                        all_success = False
                        continue
                # ――― Special NSHFT Handling: allow -24..+24 around center ―――
                if param_name.startswith("NSHFT"):
                    # relative ±24 → internal 0–48
                    if -24 <= user_value <= 24:
                        internal_value = user_value + 24
                    # absolute 0–48 as before
                    elif 0 <= user_value <= user_max:
                        internal_value = user_value
                    else:
                        print(f"Warning: Value {user_value} for '{param_name}' out of allowed range (-24 to +{user_max}). Skipping.")
                        all_success = False
                        continue
                else:
                    # Validate against the user-expected range
                    if not (user_min <= user_value <= user_max):
                        print(f"Warning: Value {user_value} for '{param_name}' out of user range ({user_min}-{user_max}). Skipping.")
                        all_success = False
                        continue

                    # Apply 1-based to 0-based adjustment if needed
                    if needs_adjustment:
                        # Special case: LINKx Off -> internal 0
                        if param_name.startswith("LINK") and user_value == 0:
                            internal_value = 0
                        # Special case: RXCH OMNI
                        elif param_name.startswith("RXCH") and user_value == 16:
                            internal_value = 16
                        else:
                            internal_value = user_value - 1
                    else:
                        internal_value = user_value

            elif param_type == 'char':
                # Handle character parameter (PNAM)
                char_code = -1
                if isinstance(value, str) and len(value) == 1:
                    char_code = ord(value)
                elif isinstance(value, int):
                    char_code = value
                else:
                     print(f"Warning: Invalid value type '{type(value)}' for '{param_name}'. Expecting single char or int code. Skipping.")
                     all_success = False
                     continue

                # Validate ASCII code against the allowed range (user_min/max is 0-127 here)
                if not (user_min <= char_code <= user_max):
                     print(f"Warning: ASCII code {char_code} for '{param_name}' out of range ({user_min}-{user_max}). Skipping.")
                     all_success = False
                     continue
                internal_value = char_code # No adjustment needed for char codes
            elif param_type == 'kasg':
                kasg_val = int(value)

                # Validate
                if not (0 <= kasg_val <= 1):
                    print(f"Warning: Value {kasg_val} for '{param_name}' out of valid KASG range (0-1). Skipping.")
                    all_success = False
                    continue

                internal_value = kasg_val  # Send as raw 0 or 1


        except ValueError:
            print(f"Warning: Could not interpret value '{value}' for '{param_name}' as {param_type}. Skipping.")
            all_success = False
            continue
        except TypeError: # Catch potential ord() errors for non-strings
             print(f"Warning: Could not process value '{value}' for '{param_name}'. Check type. Skipping.")
             all_success = False
             continue


        if internal_value is None: # Should only happen if logic above fails unexpectedly
             print(f"Internal Error: Failed to determine internal value for '{param_name}' with input '{value}'. Skipping.")
             all_success = False
             continue

        # --- SysEx Message Construction ---
        sysex_data = []
        description = f"PCED Param '{param_name}' (Param# {param_num}) = {value} -> Internal={internal_value}"

        # Special handling for VNUM: encode as two bytes (MSB, LSB)
        if param_name.startswith("VNUM"):
            # Double check internal range (0-127) after adjustment
            if not (0 <= internal_value <= 255):
                 print(f"Internal Error: Adjusted VNUM {internal_value} out of device range 0-255. Original value: {value}. Skipping.")
                 all_success = False
                 continue
            msb = (internal_value >> 7) & 0x7F
            lsb = internal_value & 0x7F
            sysex_data = [
                YAMAHA_ID,
                device_id_byte,
                PCED_PARAM_CHANGE_GROUP_SUBGROUP_BYTE,
                param_num,
                msb,
                lsb
            ]
        else:
            # Standard single-byte parameter handling
            sysex_data = [
                YAMAHA_ID,
                device_id_byte,
                PCED_PARAM_CHANGE_GROUP_SUBGROUP_BYTE,
                param_num,
                internal_value # Use the processed and potentially adjusted value
            ]

        # --- Send Message ---
        if not send_sysex_message(port, sysex_data, description, delay_after=delay_after):
            print(f"Error: Failed to send parameter '{param_name}'.")
            all_success = False
            # Decide whether to continue or break on failure (currently continues)

    if play_notes: play_test_notes(port)

    print("--- Performance Parameter Edit Finished ---")
    return all_success

def send_parameter_edits(edits_str, device_id=1, output_port=None, delay=0.05):
    """
    Parses and sends performance parameter edits to the TX802.

    Args:
        edits_str: A comma-separated string of KEY=VALUE pairs (e.g., "VNUM1=45,OUTCH2=3,OUTVOL3=99")
        device_id: MIDI device ID (1-16).
        output_port: Optional name of the MIDI output port.
        delay: Delay after sending each parameter change (seconds).

    Returns:
        bool: True if all parameters were sent successfully, False otherwise.
    """
    if not edits_str:
        print("Error: No parameters provided.")
        return False

    # Parse the comma-separated string into a list
    param_strs = [p.strip() for p in edits_str.split(',')]

    # Parse parameters into dictionary
    params_to_edit = {}
    try:
        for p_str in param_strs:
            # Direct parsing of KEY=VALUE strings
            if '=' not in p_str:
                print(f"Invalid parameter format: '{p_str}'. Expected 'KEY=VALUE'.")
                return False

            key, value_str = p_str.split('=', 1)
            key = key.strip()
            value_str = value_str.strip()

            if not key:
                print("Parameter key cannot be empty.")
                return False

            # Try to convert value to int, otherwise keep as string
            try:
                value = int(value_str)
            except ValueError:
                # If it's not an int, keep it as a string (e.g., for PNAM characters)
                value = value_str
                # Basic validation for single characters if it's likely a PNAM key
                if key.upper().startswith("PNAM") and len(value) != 1:
                    print(f"Warning: Value '{value}' for PNAM parameter '{key}' is not a single character. Sending as string.")

            params_to_edit[key.upper()] = value  # Use uppercase internally

    except ValueError as e:
        print(f"Error parsing parameters: {e}")
        return False

    print("Parameters to set:")
    for k, v in params_to_edit.items():
        print(f"  {k}: {v}")

    # Load config and resolve output port
    config = load_config()

    if output_port:
        out_port_name = output_port
        ports_ok = True
    else:
        out_port_name, _, ports_ok = select_midi_ports(config)
        if ports_ok and out_port_name:
            config['output_port'] = out_port_name
            save_config(config)

    if not ports_ok or not out_port_name:
        print("Error: No valid MIDI output port selected.")
        return False

    # Open port and call edit_performance
    try:
        print(f"\nOpening Output Port: {out_port_name}")
        with mido.open_output(out_port_name) as out_port:
            print("Output Port opened successfully.")
            return edit_performance(
                port=out_port,
                device_id=device_id,
                delay_after=delay,
                play_notes=True,
                **params_to_edit
            )
    except Exception as e:
        print(f"Error during MIDI operation: {e}")
        import traceback
        traceback.print_exc()
        return False


# Remote control button functions
##################################################
def parse_button_with_repeat(button_str):
    """
    Parse a button string that may include repeat count in format BUTTON_NAME=N

    Args:
        button_str: String in format 'BUTTON_NAME' or 'BUTTON_NAME=N' where N is number of repeats

    Returns:
        tuple: (button_name, repeat_count)
    """
    if '=' in button_str:
        button_name, repeat_str = button_str.split('=', 1)
        button_name = button_name.strip().upper()

        # Handle CODE=n specially: don't treat repeat_str as repeat count
        if button_name.startswith('CODE'):
            return f'CODE={repeat_str}', 1  # No repeat, just one send

        # Special case for TEXT parameter
        if button_name == 'TEXT':
            return 'TEXT', repeat_str  # Return text as the repeat_str

        # Special case for virtual commands - no parameters needed
        if button_name in {'POS1', 'PRTCT_ON', 'PRTCT_OFF'}:
            return button_name, 1

        try:
            repeat_count = int(repeat_str)
            if repeat_count < 1:
                print(f"Warning: Invalid repeat count {repeat_count} for {button_name}, using 1")
                repeat_count = 1
        except ValueError:
            print(f"Warning: Invalid repeat count format for {button_name}, using 1")
            repeat_count = 1
    else:
        button_name = button_str.strip().upper()

        # Special case for POS1 virtual command
        if button_name in {'POS1', 'PRTCT_ON', 'PRTCT_OFF'}:
            return button_name, 1

        repeat_count = 1

    return button_name, repeat_count

def get_button_sequence_for_char(char):
    """
    Maps a character to a sequence of button presses.

    Args:
        char: A single character to convert to button presses

    Returns:
        list: A list of (button_name, repeat_count) tuples
    """
    # Define the virtual buttons mapping
    VIRTUAL_BUTTONS = {
        # Lowercase letters
        'a': [('LOWERCASE', 1), ('0', 2)],  # Press 0 twice to get 'a'
        'b': [('LOWERCASE', 1), ('0', 3)],  # Press 0 three times to get 'b'
        'c': [('LOWERCASE', 1), ('0', 4)],  # Press 0 four times to get 'c'
        'd': [('LOWERCASE', 1), ('1', 2)],
        'e': [('LOWERCASE', 1), ('1', 3)],
        'f': [('LOWERCASE', 1), ('1', 4)],
        'g': [('LOWERCASE', 1), ('2', 2)],
        'h': [('LOWERCASE', 1), ('2', 3)],
        'i': [('LOWERCASE', 1), ('2', 4)],
        'j': [('LOWERCASE', 1), ('3', 2)],
        'k': [('LOWERCASE', 1), ('3', 3)],
        'l': [('LOWERCASE', 1), ('3', 4)],
        'm': [('LOWERCASE', 1), ('4', 2)],
        'n': [('LOWERCASE', 1), ('4', 3)],
        'o': [('LOWERCASE', 1), ('4', 4)],
        'p': [('LOWERCASE', 1), ('5', 2)],
        'q': [('LOWERCASE', 1), ('5', 3)],
        'r': [('LOWERCASE', 1), ('5', 4)],
        's': [('LOWERCASE', 1), ('6', 2)],
        't': [('LOWERCASE', 1), ('6', 3)],
        'u': [('LOWERCASE', 1), ('6', 4)],
        'v': [('LOWERCASE', 1), ('7', 2)],
        'w': [('LOWERCASE', 1), ('7', 3)],
        'x': [('LOWERCASE', 1), ('7', 4)],
        'y': [('LOWERCASE', 1), ('8', 2)],
        'z': [('LOWERCASE', 1), ('8', 3)],

        # Uppercase letters
        'A': [('UPPERCASE', 1), ('0', 2)],
        'B': [('UPPERCASE', 1), ('0', 3)],
        'C': [('UPPERCASE', 1), ('0', 4)],
        'D': [('UPPERCASE', 1), ('1', 2)],
        'E': [('UPPERCASE', 1), ('1', 3)],
        'F': [('UPPERCASE', 1), ('1', 4)],
        'G': [('UPPERCASE', 1), ('2', 2)],
        'H': [('UPPERCASE', 1), ('2', 3)],
        'I': [('UPPERCASE', 1), ('2', 4)],
        'J': [('UPPERCASE', 1), ('3', 2)],
        'K': [('UPPERCASE', 1), ('3', 3)],
        'L': [('UPPERCASE', 1), ('3', 4)],
        'M': [('UPPERCASE', 1), ('4', 2)],
        'N': [('UPPERCASE', 1), ('4', 3)],
        'O': [('UPPERCASE', 1), ('4', 4)],
        'P': [('UPPERCASE', 1), ('5', 2)],
        'Q': [('UPPERCASE', 1), ('5', 3)],
        'R': [('UPPERCASE', 1), ('5', 4)],
        'S': [('UPPERCASE', 1), ('6', 2)],
        'T': [('UPPERCASE', 1), ('6', 3)],
        'U': [('UPPERCASE', 1), ('6', 4)],
        'V': [('UPPERCASE', 1), ('7', 2)],
        'W': [('UPPERCASE', 1), ('7', 3)],
        'X': [('UPPERCASE', 1), ('7', 4)],
        'Y': [('UPPERCASE', 1), ('8', 2)],
        'Z': [('UPPERCASE', 1), ('8', 3)],

        # Numbers - First press gives the number itself
        '0': [('0', 1)],
        '1': [('1', 1)],
        '2': [('2', 1)],
        '3': [('3', 1)],
        '4': [('4', 1)],
        '5': [('5', 1)],
        '6': [('6', 1)],
        '7': [('7', 1)],
        '8': [('8', 1)],
        '9': [('9', 1)],

        # Special characters
        ' ': [('SPACE', 1)],
        '!': [('8', 4)],  # Fourth press
        '#': [('9', 2)],
        '&': [('9', 3)],
        '-': [('DASH', 1)],
        '/': [('DASH', 2)],
        '.': [('DASH', 3)],
        "'": [('DASH', 4)],
    }

    if char in VIRTUAL_BUTTONS:
        return VIRTUAL_BUTTONS[char]
    else:
        print(f"Warning: Character '{char}' not supported, replacing with '.'")
        return [('DASH', 3)]  # Replace with period as fallback

def process_text_parameter(text):
    """
    Process a TEXT parameter and convert it into a sequence of button presses.
    Automatically adds CURSOR_RIGHT after each character to move to the next position.
    Special handling for space to prevent double spaces.
    Pads text to 20 characters with spaces to clear any existing text.
    Optimizes case changes by only sending UPPERCASE/LOWERCASE when case changes.

    Args:
        text: The text to convert

    Returns:
        list: A list of (button_name, repeat_count) tuples for each character
    """
    button_sequence = []

    # First, go to the first position (like POS1)
    button_sequence.append(('CURSOR_LEFT', 19))  # Press cursor left 19 times to ensure we're at the start

    # Limit text to 20 characters if longer
    if len(text) > 20:
        text = text[:20]
        print(f"Warning: Text truncated to 20 characters: '{text}'")

    # Pad text to exactly 20 characters with spaces to clear any existing text
    padded_text = text.ljust(20)

    # Track the current case state - start with no case set
    # We'll set it to 'UPPERCASE' or 'LOWERCASE' as we process characters
    current_case = None

    for i, char in enumerate(padded_text):
        # Get button presses for this character
        char_buttons = get_button_sequence_for_char(char)

        # Process the button sequence for this character with case optimization
        for button_name, repeat_count in char_buttons:
            # If this is a case change command
            if button_name in ('UPPERCASE', 'LOWERCASE'):
                # Only send the case change if it's different from current case
                if button_name != current_case:
                    button_sequence.append((button_name, repeat_count))
                    current_case = button_name
            else:
                # For non-case buttons, add them normally
                button_sequence.append((button_name, repeat_count))

        # Add CURSOR_RIGHT after each character except after spaces
        # The TX802 automatically advances the cursor after spaces
        # Char #20 doesn't need a CURSOR_RIGHT either.
        if not (char == ' ') and i < 19:  # Appears to be 0-based, therefore <19 and not <20!
            button_sequence.append(('CURSOR_RIGHT', 1))

    return button_sequence

def press_button(port, button_name, device_id=1, delay_after=0):
    """
    Send a remote switch message to simulate pressing a button on the TX802 front panel.

    Args:
        port: MIDI output port
        button_name: Name of the button (e.g., 'PERFORMANCE_SELECT', 'CURSOR_LEFT', etc.)
        device_id: MIDI device ID (1-16, default: 1)
        delay_after: Time to wait after sending the command (seconds)

    Returns:
        bool: True if successful, False otherwise
    """
    # Button name to code mapping
    # Some buttons duplicated with different names for user convenience
    BUTTON_CODES = {
        # Special Function Buttons
        'RESET': 64,  # Soft reboot, inittialises menus and edit buffers
        'INT': 75,
        'CRT': 76,
        'LOWERCASE': 78,
        'UPPERCASE': 79,

        # Multi-Press Character Buttons
        '0': 65,  # 0 => a => b => c
        '1': 66,  # 1 => d => e => f
        '2': 67,  # 2 => g => h => i
        '3': 68,  # 3 => j => k => l
        '4': 69,  # 4 => m => n => o
        '5': 70,  # 5 => p => q => r
        '6': 71,  # 6 => s => t => u
        '7': 72,  # 7 => v => w => x
        '8': 73,  # 8 => y => z => !
        '9': 74,  # 9 => # => & => p
        'DASH': 80,  # - => / => . => '

        # Single-Press Character Button
        'SPACE': 77,    # Unit emits space + cursor_right on reception

        # Navigation Buttons
        'CURSOR_LEFT': 75,
        'CURSOR_RIGHT': 76,
        'ENTER': 77,

        # Muti-Meaning Buttons
        'MINUS_ONE': 78,
        'OFF': 78,
        'NO': 78,
        'PLUS_ONE': 79,
        'ON': 79,
        'YES': 79,
        'STORE': 88,
        'COMPARE': 88,

        # System Mode buttons
        'PERFORM_SELECT': 81,
        'VOICE_SELECT': 82,
        'SYSTEM_SETUP': 83,
        'UTILITY': 84,
        'PERFORM_EDIT': 85,
        'VOICE_EDIT_I': 86,
        'VOICE_EDIT_II': 87,

        # TG On/Off/Select buttons
        'TG1': 89,
        'TG2': 90,
        'TG3': 91,
        'TG4': 92,
        'TG5': 93,
        'TG6': 94,
        'TG7': 95,
        'TG8': 96
    }

    # Validate button name
    button_name = button_name.upper()

    if button_name.startswith('WAIT'):
        time.sleep(1)
        return True
    elif button_name.startswith('CODE='):
        try:
            button_code = int(button_name.split('=')[1])
            if not (0 <= button_code <= 127):
                print(f"Error: CODE value out of range (0-127): {button_code}")
                return False
        except (ValueError, IndexError):
            print(f"Error: Invalid CODE format: {button_name}")
            return False
    elif button_name.startswith('WAIT='):
        button_code = int(button_name.split('=')[1])
        print("Would press wait:", button_code)
    else:
        if button_name not in BUTTON_CODES:
            print(f"Error: Unknown button name '{button_name}'. Valid buttons are: {', '.join(BUTTON_CODES.keys())}")
            return False
        button_code = BUTTON_CODES[button_name]

    # Prepare device ID
    internal_device_id = device_id - 1
    if not 0 <= internal_device_id <= 15:
        print(f"Warning: Invalid device ID {device_id}. Using 1.")
        internal_device_id = 0
        device_id = 1
    id_byte = 0x10 + internal_device_id

    # Prepare message
    msg_data = [YAMAHA_ID, id_byte, REMOTE_SWITCH_GROUP_SUBGROUP_BYTE, button_code, 0x00]

    # Send message
    description = f"Button '{button_name}' (Code {button_code})"
    return send_sysex_message(port, msg_data, description, delay_after=delay_after)

def process_button_sequence(out_port, sequence, device_id=1, delay=0.1, verbose=True):
    """
    Process a sequence of button commands.

    Args:
        out_port: MIDI output port
        sequence: String with comma-separated button commands or a list of button commands
        device_id: MIDI Device ID (1-16)
        delay: Delay after button press (seconds)
        verbose: Whether to print status messages

    Returns:
        bool: True if all commands were processed successfully, False otherwise
    """
    # Convert string to list if needed
    if isinstance(sequence, str):
        button_sequence = [btn.strip() for btn in sequence.split(',')]
    else:
        button_sequence = sequence

    if verbose:
        print(f"\nProcessing button sequence: {' → '.join(button_sequence)}")

    success = True

    # Process each button in the sequence
    for button_spec in button_sequence:
        button_name, repeat_value = parse_button_with_repeat(button_spec)

        # Handle TEXT special case
        if button_name.upper() == 'TEXT':
            text = repeat_value  # In this case, repeat_value is the text string
            if verbose:
                print(f"  Processing TEXT parameter: '{text}'")
            text_button_sequence = process_text_parameter(text)

            # Process each button press required for the text
            for char_button, char_repeat in text_button_sequence:
                for i in range(char_repeat):
                    if not press_button(out_port, char_button, device_id, delay):
                        success = False

        # Handle virtual commands
        elif button_name.upper() == 'POS1':
            if verbose:
                print(f"  Processing POS1 virtual command (sending CURSOR_LEFT 19 times)")
            for i in range(19):  # Press CURSOR_LEFT 19 times
                if not press_button(out_port, 'CURSOR_LEFT', device_id, delay):
                    success = False
        elif button_name.upper() == 'PRTCT_OFF':
            if verbose:
                print(f"  Processing PRTCT_OFF virtual command (sending SYSTEM_SETUP,TG8,NO)")
            if not press_button(out_port, 'SYSTEM_SETUP', device_id, delay):
                success = False
            if not press_button(out_port, 'TG8', device_id, delay):
                success = False
            if not press_button(out_port, 'NO', device_id, delay):
                success = False
        elif button_name.upper() == 'PRTCT_ON':
            if verbose:
                print(f"  Processing PRTCT_OFF virtual command (sending SYSTEM_SETUP,TG8,YES)")
            if not press_button(out_port, 'SYSTEM_SETUP', device_id, delay):
                success = False
            if not press_button(out_port, 'TG8', device_id, delay):
                success = False
            if not press_button(out_port, 'YES', device_id, delay):
                success = False
        else:
            # Handle normal button press with repeat
            for i in range(repeat_value):
                if verbose:
                    if repeat_value > 1:
                        print(f"  Executing '{button_name}' (repeat {i + 1}/{repeat_value})")
                    else:
                        print(f"  Executing '{button_name}'")
                if not press_button(out_port, button_name, device_id, delay):
                    success = False

    if verbose:
        print("Button sequence completed.")

    return success

def send_button_sequence(sequence, device_id=1, delay=0.1, output_port=None, verbose=True):
    """
    Sends a button sequence to the TX802

    Args:
        sequence: String with comma-separated button commands or a list of button commands
        device_id: MIDI Device ID (1-16)
        delay: Delay after button press (seconds)
        output_port: Name of MIDI output port to use (if None, will use config or prompt)
        verbose: Whether to print status messages

    Returns:
        bool: True if successful, False otherwise
    """
    # Load config and select MIDI ports
    config = load_config()

    if output_port:
        out_port_name = output_port
        ports_ok = True
    else:
        out_port_name, _, ports_ok = select_midi_ports(config)
        if ports_ok:
            config['output_port'] = out_port_name
            save_config(config)

    if not ports_ok:
        return False

    out_port = None
    success = False
    try:
        if verbose:
            print(f"\nOpening Output Port: {out_port_name}")
        out_port = mido.open_output(out_port_name)
        if verbose:
            print("Output Port opened successfully.")

        # Process the button sequence
        success = process_button_sequence(out_port, sequence, device_id, delay, verbose)

    except Exception as e:
        if verbose:
            print(f"\nAn unexpected error occurred: {e}")
            traceback.print_exc()
        success = False
    finally:
        if out_port and not out_port.closed:
            try:
                if verbose:
                    print("\nSending All Notes Off...")
                for channel in range(16):
                    out_port.send(mido.Message('control_change', channel=channel, control=123, value=0))
                time.sleep(0.1)
            except Exception as e_off:
                if verbose:
                    print(f"Error sending All Notes Off: {e_off}")
            if verbose:
                print("Closing MIDI Output Port...")
            out_port.close()
            if verbose:
                print("Closed Output Port.")
        if verbose:
            print("\nFinished.")

    return success


# Patch, bank and performance bank functions
##################################################
def validate_and_send(filename, port, stopafter=None):
    """
    Validates and sends a SysEx file (VMEM or PMEM).
    Improved to handle TX802 vintage hardware limitations.

    Args:
        filename: Path to the .syx file
        port: MIDI output port
        stopafter: Number of voices to send before stopping (VMEM only)

    Returns:
        bool: True if successful, False otherwise
    """
    print(f"\n===== VALIDATING SYSEX FILE: {filename} =====")

    # File Existence and Extension Check
    if not os.path.exists(filename):
        print(f"Error: File not found: '{filename}'")
        return False
    if not filename.lower().endswith('.syx'):
        print(f"Error: File does not have a .syx extension: '{filename}'")
        return False

    print("File exists and has .syx extension.")

    # Read File Content
    try:
        with open(filename, 'rb') as f:
            sysex_data = f.read()
        print(f"Read {len(sysex_data)} bytes from file.")
    except IOError as e:
        print(f"Error reading file: {e}")
        return False

    # Basic SysEx Structure Validation
    if not sysex_data or sysex_data[0] != 0xF0 or sysex_data[-1] != 0xF7:
        print("Error: File content does not start with 0xF0 and end with 0xF7. Invalid SysEx.")
        return False
    if len(sysex_data) < 4:  # Need at least F0, ID, device, F7
        print("Error: SysEx data too short.")
        return False
    print("Basic SysEx structure (F0...F7) looks OK.")

    # Manufacturer ID Check
    if sysex_data[1] != YAMAHA_ID:
        print(f"Error: Manufacturer ID is not Yamaha (0x{YAMAHA_ID:02X}). Found: 0x{sysex_data[1]:02X}.")
        return False
    print(f"Manufacturer ID is Yamaha (0x{YAMAHA_ID:02X}). OK.")

    # Identify Bank Type and Validate (VMEM or PMEM)
    # Device ID (0n) is sysex_data[2]. We check the bytes *after* it.
    header_part = sysex_data[3:-1]  # Exclude F0, ManuID, DevID, and F7

    is_vmem = False
    is_pmem = False

    # Check for VMEM (Voice Memory - 32 voices)
    if header_part.startswith(VMEM_HEADER_START):
        print("Detected potential VMEM (Voice Memory) format.")
        if len(sysex_data) == VMEM_EXPECTED_SIZE:
            print(f"File size ({len(sysex_data)} bytes) matches expected VMEM size ({VMEM_EXPECTED_SIZE} bytes). OK.")
            is_vmem = True
        else:
            print(f"Error: File size ({len(sysex_data)} bytes) does NOT match expected VMEM size ({VMEM_EXPECTED_SIZE} bytes).")
            return False

    # Check for PMEM (Performance Memory)
    elif header_part.startswith(PMEM_HEADER_START):
        print("Detected PMEM (Performance Memory) format.")
        # The header bytes 0x7E, 0x01, 0x28 are sufficient to identify a PMEM file
        is_pmem = True

        # Size check as additional validation
        expected_pmem_size = 11589
        if abs(len(sysex_data) - expected_pmem_size) < 100:
            print(f"File size ({len(sysex_data)} bytes) is close to expected PMEM size ({expected_pmem_size} bytes). OK.")
        else:
            print(f"Warning: File size ({len(sysex_data)} bytes) differs from expected PMEM size ({expected_pmem_size} bytes).")
            print("Will attempt to process anyway.")

    else:
        # Try to print the first few header bytes for debugging
        start_bytes = ' '.join([f'0x{b:02X}' for b in header_part[:10]])
        print(f"Error: Unrecognized SysEx data format. Header starts with: {start_bytes}...")
        print(f"Expected either:")
        print(f"  - VMEM start: {' '.join([f'0x{b:02X}' for b in VMEM_HEADER_START])}")
        print(f"  - PMEM start: {' '.join([f'0x{b:02X}' for b in PMEM_HEADER_START])}")
        return False

    # Send Data
    if is_vmem:
        print(f"\n===== SENDING VMEM SYSEX FILE: {filename} =====")
        try:
            # Ensure port is valid and open
            if not port or port.closed:
                print("Error: Output port is not open. Cannot send SysEx.")
                return False

            # Short pause before sending
            time.sleep(0.1)

            # Handle partial bank transfer if stopafter is specified
            if stopafter is not None:
                try:
                    stopafter = int(stopafter)
                    if stopafter < 1 or stopafter > 31:
                        print(f"Warning: Invalid stopafter value ({stopafter}). Must be between 1 and 31. Sending full bank.")
                    else:
                        # Calculate where to cut the SysEx data
                        # We'll send complete voices up to stopafter, then a few bytes of the next voice
                        cutoff_index = VMEM_HEADER_SIZE + (stopafter * VMEM_VOICE_SIZE) + 4  # Add a few bytes into next voice

                        # Make sure we don't exceed the file size
                        if cutoff_index >= len(sysex_data) - 1:  # -1 to leave room for F7
                            print(f"Warning: Requested to stop after voice {stopafter}, but that would exceed the file size.")
                            print(f"Sending the entire bank instead.")
                        else:
                            print(f"Sending partial bank - stopping after voice {stopafter} (at byte {cutoff_index}).")

                            # Create a truncated version of the SysEx data (no end marker)
                            truncated_data = sysex_data[:cutoff_index]

                            # Send the truncated data
                            if not send_sysex_message(
                                    port,
                                    truncated_data,
                                    description=f"VMEM data (voices 1-{stopafter}, partial)",
                                    delay_after=0.2
                            ):
                                print("Failed to send partial VMEM data.")
                                return False

                            print(f"Partial VMEM data transfer complete (voices 1-{stopafter}).")
                            print("TX802 is now in an incomplete SysEx receiving state.")
                            return True
                except ValueError:
                    print(f"Warning: Invalid stopafter value '{stopafter}'. Must be an integer. Sending full bank.")

            # If we get here, send the complete bank
            success = send_sysex_message(
                port,
                sysex_data,  # Complete sysex message
                description="VMEM data bank",
                delay_after=0.2  # Reasonable delay for the TX802
            )

            if not success:
                print("Failed to send VMEM data.")
                return False

            print("VMEM data transfer complete.")
            return True

        except Exception as e:
            print(f"Error sending SysEx message: {e}")
            traceback.print_exc()
            return False

    elif is_pmem:
        print(f"\n===== SENDING PMEM SYSEX FILE: {filename} =====")
        try:
            # Ensure port is valid and open
            if not port or port.closed:
                print("Error: Output port is not open. Cannot send SysEx.")
                return False

            # Short pause before sending
            time.sleep(0.1)

            # Note: 'stopafter' is not applicable for PMEM as mentioned in your tx802_send_performance.py
            if stopafter is not None:
                print("Warning: 'stopafter' parameter is not supported for PMEM data. Sending complete bank.")

            # Check if this is a multi-block PMEM file
            f0_count = sysex_data.count(0xF0)
            if f0_count > 1:
                print(f"Detected multi-block PMEM file with {f0_count} SysEx messages.")

                # Split the data into separate SysEx messages
                messages = []
                start_idx = 0
                while True:
                    f0_idx = sysex_data.find(0xF0, start_idx)
                    if f0_idx == -1:
                        break
                    f7_idx = sysex_data.find(0xF7, f0_idx)
                    if f7_idx == -1:
                        print("Warning: Found F0 without matching F7. Data may be corrupted.")
                        break
                    messages.append(sysex_data[f0_idx:f7_idx + 1])
                    start_idx = f7_idx + 1

                print(f"Split into {len(messages)} separate SysEx blocks.")

                # Send each block with a pause between
                for i, msg in enumerate(messages):
                    print(f"Sending PMEM block {i + 1} of {len(messages)} ({len(msg)} bytes)...")
                    success = send_sysex_message(
                        port,
                        msg,
                        description=f"PMEM block {i + 1}/{len(messages)}",
                        delay_after=0.1  # Slightly longer delay between blocks
                    )
                    if not success:
                        print(f"Failed to send PMEM block {i + 1}.")
                        return False

                print("Multi-block PMEM data transfer complete.")
                return True
            else:
                # Single block PMEM
                success = send_sysex_message(
                    port,
                    sysex_data,
                    description="PMEM data bank",
                    delay_after=0.2
                )

                if not success:
                    print("Failed to send PMEM data.")
                    return False

                print("PMEM data transfer complete.")
                return True

        except Exception as e:
            print(f"Error sending PMEM SysEx message: {e}")
            traceback.print_exc()
            return False

    # Should not reach here if validation is correct, but as a safeguard:
    print("Error: Did not identify a valid, sendable SysEx format.")
    return False


def send_bank(sysex_file=None, device_id=1, output_port=None, stopafter=None):
    """
    Main function for sending a (partial) patchbank to the TX802

    Args:
        sysex_file: Path to the .syx file to send (optional)
        device_id: MIDI Device ID (1-16)
        output_port: Can be a port name, an open port object, or None
        stopafter: Number of voices to send before stopping (optional)

    Returns:
        bool: True if successful, False otherwise
    """
    # Load config for any subsequent operations
    config = load_config()

    # Check if output_port is already an open port object
    is_port_object = isinstance(output_port, BaseOutput) and not getattr(output_port, 'closed', True)

    out_port = None
    out_port_name = None
    in_port = None
    in_port_name = None
    ports_ok = False
    should_close_port = False

    if is_port_object:
        # Use the provided port directly
        out_port = output_port
        ports_ok = True
        print("Using already open MIDI output port")
        should_close_port = False
    else:
        # Original port selection logic
        if output_port and isinstance(output_port, str):
            out_port_name = output_port
            available_out_ports = mido.get_output_names()
            if out_port_name in available_out_ports:
                ports_ok = True
            else:
                print(f"Error: Specified output port '{output_port}' not found.")
                return False
        else:
            out_port_name, in_port_name, ports_ok = select_midi_ports(config)
            if ports_ok:
                config['output_port'] = out_port_name
                if in_port_name:
                    config['input_port'] = in_port_name
                save_config(config)

        if not ports_ok:
            print("Exiting due to port selection issue.")
            return False

    transfer_success = False  # Track if transfer happened

    try:
        # Open ports if we're not using an already open port
        if not is_port_object:
            print(f"\nOpening Output Port: {out_port_name}")
            out_port = mido.open_output(out_port_name)
            should_close_port = True

            if in_port_name:
                print(f"Opening Input Port: {in_port_name}")
                in_port = mido.open_input(in_port_name)

        # --- Disable Memory Protection using process_button_sequence
        if not process_button_sequence(out_port, "PRTCT_OFF", device_id=device_id, delay=0.1, verbose=True):
            print("Error: Failed to send remote control sequence. Aborting.")
            return False

        # --- Process and Send SysEx File (if provided) ---
        if sysex_file:
            if validate_and_send(sysex_file, out_port, stopafter):
                transfer_success = True
                print(f"Successfully processed and sent '{sysex_file}'.")

                # If we did a partial transfer, send a button press to exit SysEx state
                if stopafter is not None:
                    print("\nSending button press to exit incomplete SysEx state...")
                    time.sleep(0.5)  # Short pause
                    if process_button_sequence(out_port, "VOICE_SELECT", device_id=device_id, delay=0.2, verbose=True):
                        print("Button press sent successfully to exit SysEx state.")
                    else:
                        print("Failed to send button press to exit SysEx state.")
                        transfer_success = False
            else:
                print(f"Failed to process or send '{sysex_file}'.")
        else:
            print("\nNo SysEx file provided. Skipping file transfer.")
            # Set transfer_success to True if no file is provided,
            # so confirmation notes still play.
            transfer_success = True

        # --- Play Confirmation Notes ONLY if protection disable AND transfer (or skip) were successful ---
        if transfer_success:
            # Add a pause after transfer to make sure device is ready
            time.sleep(1.0)

            # Refresh the buffer with a simple program change toggle
            if not process_button_sequence(out_port, "VOICE_SELECT,PLUS_ONE,MINUS_ONE", device_id=device_id, delay=0.1, verbose=True):
                print("Error: Failed to send remote control sequence. Aborting.")
                return False

            # Another small pause before confirmation notes
            time.sleep(0.2)

            # Then play the confirmation notes
            play_test_notes(out_port)
            return True
        else:
            print("\nSkipping confirmation notes due to errors during SysEx processing.")
            return False

    except Exception as e:
        print(f"\nAn unexpected error occurred during MIDI operation: {e}")
        traceback.print_exc()
        return False

    finally:
        # Only close ports if we opened them (and they're still open)
        if should_close_port and out_port and not out_port.closed:
            print("\nSending All Notes Off...")
            try:
                # Send All Notes Off on all channels (MIDI standard)
                for channel in range(16):
                    out_port.send(mido.Message('control_change', channel=channel, control=123, value=0))
                time.sleep(0.1)  # Short pause after All Notes Off
            except Exception as e:
                print(f"Error sending All Notes Off: {e}")
            out_port.close()
            print("Closed Output Port.")

        if in_port and not in_port.closed:
            in_port.close()
            print("Closed Input Port.")

        print("\nFinished.")

def send_performance(sysex_file=None, device_id=1, output_port=None):
    """
    Main function for sending a Performance Bank (PMEM) SysEx file to the TX802.
    Handles MIDI port setup, memory protect disable, file validation/sending,
    and confirmation notes. 'stopafter' is NOT supported for PMEM.

    Args:
        sysex_file: Path to the .syx file containing PMEM data.
        device_id: Target MIDI device ID (1-16).
        output_port: Name of MIDI output port (optional, uses config/prompt otherwise).

    Returns:
        bool: True if successful, False otherwise.
    """
    if not sysex_file:
        print("Error: No SysEx file provided for send_performance_bank.")
        return False

    print(f"\n--- Sending Performance Bank (PMEM): {sysex_file} ---")
    # Load config and select MIDI ports
    config = load_config()
    ports_ok = False
    if output_port:
        out_port_name = output_port
        if out_port_name in mido.get_output_names():
             ports_ok = True
             in_port_name = config.get('input_port') # Keep input port if saved
        else:
             print(f"Error: Specified output port '{output_port}' not found.")
             return False
    else:
        out_port_name, in_port_name, ports_ok = select_midi_ports(config)
        if ports_ok and out_port_name:
            config['output_port'] = out_port_name
            if in_port_name: # Save input port if selected/available
                 config['input_port'] = in_port_name
            save_config(config)

    if not ports_ok or not out_port_name:
        print("Exiting due to port selection issue.")
        return False

    out_port = None
    transfer_success = False

    try:
        print(f"\nOpening Output Port: {out_port_name}")
        with mido.open_output(out_port_name) as out_port:
            print("Output Port opened successfully.")

            # --- Disable Memory Protection ---
            if not process_button_sequence(out_port, "PRTCT_OFF", device_id=device_id, delay=0.1, verbose=True):
                print("Error: Failed to send remote control sequence to disable memory protection. Aborting.")
                return False
            print("Memory protection disabled (attempted).")
            time.sleep(0.2) # Small pause after button sequence

            # --- Validate and Send SysEx File ---
            transfer_success = validate_and_send(sysex_file, out_port, device_id)

            if transfer_success:
                print(f"Successfully processed and sent '{sysex_file}'.")
            else:
                print(f"Failed to process or send '{sysex_file}'.")
                # No confirmation notes if transfer failed

            # --- Post-Transfer Actions (No special handling needed like partial VMEM) ---
            if transfer_success:
                 # Add a pause after transfer
                 time.sleep(1.0)

                 # Refresh buffer/UI (optional but good practice)
                 print("\nSending button sequence to refresh TX display/buffer...")
                 # Switch to PERFORMANCE mode to see changes
                 if not process_button_sequence(out_port, "PERFORM_SELECT,PLUS_ONE,MINUS_ONE", device_id=device_id, delay=0.1, verbose=True):
                     print("Warning: Failed to send refresh sequence.")
                     # Continue to confirmation notes anyway

                 time.sleep(0.2) # Pause before notes

                 # --- Play Confirmation Notes ---
                 # Note: Need to ensure a performance is loaded that makes sound
                 print("\nPlaying confirmation notes (ensure Perf 1 has audible voice on Ch 1)...")
                 play_test_notes(out_port)

            else: # Transfer failed
                 print("\nSkipping post-transfer actions and confirmation notes due to errors.")


            # Send All Notes Off before closing port
            print("\nSending All Notes Off...")
            for channel in range(16):
                out_port.send(mido.Message('control_change', channel=channel, control=123, value=0))
            time.sleep(0.1)

    except mido.MidiError as e:
         print(f"MIDI Error with port '{out_port_name}': {e}")
         transfer_success = False
    except Exception as e:
        print(f"\nAn unexpected error occurred during MIDI operation: {e}")
        traceback.print_exc()
        transfer_success = False
    finally:
        # Port is closed automatically by 'with' statement
        print("\nFinished Performance Bank Send.")

    return transfer_success


def send_patch_to_buffer(sysex_data, device_id=1, output_port=None):
    """
    Sends a single voice patch to the TX802's Voice Edit Buffer.

    Args:
        sysex_data: The complete SysEx data (163 bytes) for a single voice patch
        device_id: MIDI device ID (1-16)
        output_port: MIDI output port (can be a port name, an open port object, or None)

    Returns:
        bool: True if successful, False otherwise
    """
    print(f"\n--- Sending Voice to Edit Buffer (Device ID: {device_id}) ---")

    if not isinstance(sysex_data, bytes):
        print("Error: SysEx data must be provided as bytes")
        return False

    # Verify that this is a valid single voice sysex file
    from core.dx7_utils import verify_single_voice_sysex
    is_valid, message, _ = verify_single_voice_sysex(sysex_data)
    if not is_valid:
        print(f"Error: Invalid voice SysEx data: {message}")
        return False

    # Update device ID in the SysEx message if needed
    if sysex_data[2] != 0x10 + (device_id - 1) & 0x0F:
        # Create new data with updated device ID
        internal_device_id = (device_id - 1) & 0x0F
        modified_data = bytearray(sysex_data)
        modified_data[2] = 0x10 + internal_device_id  # Update device ID byte
        sysex_data = bytes(modified_data)
        print(f"Updated device ID in SysEx message to {device_id}")

    # Load config for any subsequent operations
    config = load_config()

    # Resolve the output port
    out_port, out_port_name, should_close_port, ports_ok = resolve_output_port(output_port, config)

    if not ports_ok or not out_port:
        print("Error: No valid MIDI output port selected.")
        return False

    try:
        # --- Disable Memory Protection ---
        if not process_button_sequence(out_port, "PRTCT_OFF", device_id=device_id, delay=0.1, verbose=True):
            print("Warning: Failed to disable memory protection. Continuing anyway.")

        # Send the SysEx data
        if not send_sysex_message(out_port, sysex_data, "Single Voice to Edit Buffer", delay_after=0.2):
            print("Error: Failed to send Voice data to Edit Buffer.")
            return False

        if process_button_sequence(out_port, "VOICE_SELECT", device_id=device_id, delay=0.2, verbose=True):
            print("Sent VOICE_SELECT button press to exit the unit's reception confirmation.")

        print("Voice data successfully sent to Edit Buffer.")

        # Short pause to let the synth process the data
        time.sleep(0.5)

        # Play test notes to audition the sound
        play_test_notes(out_port)

        return True
    except Exception as e:
        print(f"Error during MIDI operation: {e}")
        traceback.print_exc()
        return False
    finally:
        # Only close the port if we opened it
        if should_close_port and out_port and not out_port.closed:
            print("\nSending All Notes Off...")
            try:
                for channel in range(16):
                    out_port.send(mido.Message('control_change', channel=channel, control=123, value=0))
                time.sleep(0.1)
            except Exception as e:
                print(f"Error sending All Notes Off: {e}")
            out_port.close()
            print("Closed Output Port.")


# Miscellaneous / Helpers
##################################################
def tx802_startup_items():
    return [
        "RESET",                                    # Reset internals (e.g. buffers)
        "WAIT=3",                                   # Allow some time for unit to resume listening
        "PRTCT_OFF",                                # Required to change values on the device
        "UTILITY", "TG5", "YES", "YES", "WAIT",     # Init Performance (TG8-TG2 => TG1)
        "SYSTEM_SETUP", "TG4", "TG4", "MINUS_ONE",  # Set Voice Bank receive to I1-I32
        "VOICE_SELECT"                              # Switch LCD to main menu
    ]

def get_midi_note_name(note_number):
    if not 0 <= note_number <= 127:
        return "Invalid"
    notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = (note_number // 12) - 2
    note_index = note_number % 12
    return f"{notes[note_index]}{octave}"

MIDI_NOTES = [get_midi_note_name(i) for i in range(128)]