import gradio as gr
import mido
import threading
import time

from core.tx802_utils import edit_performance, save_config, load_config
import app.state as state

# --- Constants and Helper Functions ---

# Debounce State.
_config_dirty = False
_config_save_timer = None

voice_dropdowns = []

def schedule_debounced_config_save(current_tg_states):
    """Schedules a debounced config save. Returns 'pending' immediately, and 'saved' later."""
    global _config_dirty, _config_save_timer

    _config_dirty = True  # Mark config as needing save

    def _save_later():
        global _config_dirty, _config_save_timer
        time.sleep(5.0)

        if _config_dirty:
            config = load_config()
            config["performance_params"] = current_tg_states.copy()
            save_config(config)
            _config_dirty = False

        _config_save_timer = None

    if _config_save_timer:
        _config_save_timer.cancel()

    _config_save_timer = threading.Timer(5, _save_later)
    _config_save_timer.start()

    return "⚠️ Save pending"

def get_midi_note_name(note_number):
    if not 0 <= note_number <= 127:
        return "Invalid"
    notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = (note_number // 12) - 2
    note_index = note_number % 12
    return f"{notes[note_index]}{octave}"

MIDI_NOTES = [get_midi_note_name(i) for i in range(128)]
ON_OFF_CHOICES = ["Off", "On"]
RECEIVE_CHOICES = [str(i) for i in range(1, 17)] + ["Omni"]
PAN_CHOICES = ["Off", "Left", "Right", "Center"]
FDAMP_CHOICES = ["Off", "On"]
TG_CHOICES = ["Off", "On"]

# Note choices from C-2 up to G8
_NOTE_NAME_BASE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
NOTE_CHOICES = [
    f"{note}{octave}"
    for octave in range(-2, 9)
    for note in _NOTE_NAME_BASE
    if not (octave == 8 and _NOTE_NAME_BASE.index(note) > 7)
]

# Output volume choices 0–99 (slider)
# We'll use a slider rather than raw numbers for better UX



def note_name_to_midi(note_name: str) -> int:
    try:
        return MIDI_NOTES.index(note_name)
    except ValueError:
        return -1  # Invalid note

def midi_channel_to_internal(val: str) -> int:
    if val == "Off":
        pass # neets to be taken care off later
    elif val == "Omni":
        return 16
    try:
        return int(val)
    except ValueError:
        return -1  # fallback

def output_assign_to_code(val: str) -> int:
    return {"Off": 0, "L": 1, "R": 2, "L&R": 3}.get(val, 0)

def on_off_to_bool(val: str) -> int:
    return 1 if val == "On" else 0

# --- Gradio Tab Setup Functions ---
def setup_tab():
    global voice_dropdowns
    voice_dropdowns = []

    # state.init_tx802_performance_state()
    patch_bank_state = gr.State(state.PATCH_BANK)

    col_widths = {
        "TG": 50, "Link": 70, "Voice": 160,
        "Receive": 90, "Low": 80, "High": 80,
        "Detune": 70, "Shift": 70, "Volume": 80,
        "Output": 80, "Damp": 80
    }

    gr.Markdown("# Performance Editor")

    all_interactive_inputs = []

    # Define column layout based on width specification
    columns = [
        ("TG", col_widths["TG"]),
        ("Link", col_widths["Link"]),
        ("Voice", col_widths["Voice"]),
        ("Receive", col_widths["Receive"]),
        ("Low", col_widths["Low"]),
        ("High", col_widths["High"]),
        ("Detune", col_widths["Detune"]),
        ("Shift", col_widths["Shift"]),
        ("Volume", col_widths["Volume"]),
        ("Output", col_widths["Output"]),
        ("Damp", col_widths["Damp"])
    ]

    header_labels = {
        "TG": "#", "Link": "TG", "Voice": "Patch",
        "Receive": "Chan", "Low": "Low", "High": "High", "Detune": "Det",
        "Shift": "Shift", "Volume": "Vol", "Output": "Out", "Damp": "Damp"
    }
    for i in range(-1, 8):
        with gr.Group():
            with gr.Row(scale=0, equal_height=True):
                for col_name, col_width in columns:
                    with gr.Column(scale=0, min_width=col_width):
                        if i == -1:  # Header row
                            gr.HTML(header_labels[col_name], container=False)
                        else:  # Data rows
                            if col_name == "TG":
                                gr.Textbox(value=str(i + 1), show_label=False, interactive=False, container=False)

                            elif col_name == "Link":
                                # TG On/Off dropdown
                                default_val = state.tg_states[i + 1]["TG"]
                                elem = gr.Dropdown(
                                    choices=TG_CHOICES,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Voice":
                                # Patch dropdown: display the patch name, return device code "I01"–"I32"
                                # Build choices as (label, value) pairs
                                preset_choices = [
                                    (patch_name, f"I{slot:02d}")
                                    for patch_name, slot in state.PATCH_BANK
                                ]
                                # Default is whatever PRESET the TG currently has
                                default_val = state.tg_states[i + 1]["PRESET"]
                                elem = gr.Dropdown(
                                    choices=preset_choices,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Receive":
                                # MIDI receive channel dropdown: 1–16 or Omni
                                default_val = state.tg_states[i + 1]["RXCH"]
                                elem = gr.Dropdown(
                                    choices=RECEIVE_CHOICES,
                                    value=str(default_val),
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Low":
                                # Lower split point dropdown (note names)
                                default_val = state.tg_states[i + 1]["NOTELOW"]
                                elem = gr.Dropdown(
                                    choices=NOTE_CHOICES,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "High":
                                # Upper split point dropdown (note names)
                                default_val = state.tg_states[i + 1]["NOTEHIGH"]
                                elem = gr.Dropdown(
                                    choices=NOTE_CHOICES,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Detune":
                                # Detune offset –7 to +7, dynamic default from state
                                default_val = int(state.tg_states[i + 1]["DETUNE"])
                                elem = gr.Number(
                                    minimum=-7,
                                    maximum=7,
                                    step=1,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Shift":
                                # Note shift –24 to +24, dynamic default from state
                                default_val = int(state.tg_states[i + 1]["NOTESHIFT"])
                                elem = gr.Number(
                                    minimum=-24,
                                    maximum=24,
                                    step=1,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Volume":
                                # Output volume numeric input 0–99
                                default_val = int(state.tg_states[i + 1]["OUTVOL"])
                                elem = gr.Number(
                                    minimum=0,
                                    maximum=99,
                                    step=1,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Output":
                                # Panning dropdown with human-friendly labels
                                default_val = state.tg_states[i + 1]["PAN"]
                                elem = gr.Dropdown(
                                    choices=PAN_CHOICES,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

                            elif col_name == "Damp":
                                # Forced Damp dropdown with human-friendly labels
                                default_val = state.tg_states[i + 1]["FDAMP"]
                                elem = gr.Dropdown(
                                    choices=FDAMP_CHOICES,
                                    value=default_val,
                                    show_label=False,
                                    interactive=True,
                                    container=False
                                )
                                all_interactive_inputs.append(elem)

    with gr.Row():
        output_display = gr.Textbox(label="Action", interactive=False, scale=5)
        save_status_display = gr.Textbox(label="Status", value="✅ Config Saved", visible=True, scale=1)

    # List of user‐friendly, human‐readable parameter names per Tone Generator
    param_names_per_tg = [
        "TG",           # Tone Generator On/Off
        "PRESET",       # Device memory location (I01–I32 currently)
        "RXCH",         # MIDI receive channel
        "NOTELOW",      # Lower split point
        "NOTEHIGH",     # Upper split point
        "DETUNE",       # Detune offset (–7 to +7)
        "NOTESHIFT",    # Note shift (–24 to +24)
        "OUTVOL",       # Output volume (0–99)
        "PAN",          # Panning: Off | I/Left | II/Right | I+II/Center
        "FDAMP"         # EG Forced Damp On/Off
    ]

    def handle_single_change(changed_value, index, current_patch_bank):
        """
        Handles a change in a single UI element and sends the corresponding
        SysEx message to the TX802.
        """
        interactive_cols_per_tg = 10

        status_message = ""
        save_status = "No save"

        tg = index // interactive_cols_per_tg + 1  # Tone Generator number (1-8) [cite: 74]
        param_pos = index % interactive_cols_per_tg  # Parameter index within the TG's elements [cite: 74]

        param_name = param_names_per_tg[param_pos]  # Get the base parameter name (e.g., "VNUM", "RXCH") [cite: 74]

        # print(f"[DEBUG] index={index}, tg={tg}, param_pos={param_pos}, param_name={param_name}, changed_value={changed_value}")

        key = f"{param_name}{tg}"  # Construct the full parameter key (e.g., "VNUM1", "RXCH1") [cite: 74]

        # --- Directly forward the user-facing value to edit_performance ---
        internal_val = changed_value
        user_facing_value = changed_value

        if internal_val is not None:
            # --- Send the SysEx message first ---
            if not isinstance(state.midi_output, mido.ports.BaseOutput) or getattr(state.midi_output, 'closed', True):
                status_message = "Error: MIDI Output Port not configured or closed."
                print(status_message)
                return status_message

            try:

                # print(f"[CHECKPOINT] Will send: {key=} {internal_val=}, before config save.")

                # print(f"[DEBUG] Calling edit_performance with: {key} = {internal_val} (type={type(internal_val)})")

                success = edit_performance(
                    port=state.midi_output,
                    device_id=1,
                    delay_after=0.02,
                    play_notes=False,
                    **{key: internal_val}
                )

                if success:
                    status_message = lcd_display()
                    state.update_tg_state(tg, param_name, internal_val)

                    # --- Schedule config save ---
                    save_status = schedule_debounced_config_save(state.tg_states)

                else:
                    status_message = f"Failed to send: {key} = {user_facing_value}"
                    print(status_message)
            except Exception as e:
                status_message = f"Error sending SysEx: {e}"
                print(status_message)

            return status_message, save_status

    # Attach a change handler to each element with index tracking
    for idx, element in enumerate(all_interactive_inputs):
        element.change(
            fn=handle_single_change,  # Pass the function directly
            # Pass the element's value, its index, and the current patch bank state
            inputs=[element, gr.State(idx), patch_bank_state],
            outputs=[output_display, save_status_display] # Update the status textbox
        )

def refresh_tab():
    # Rebuild the (label, value) pairs exactly as in setup_tab
    print("########## refresh performance_editor ##########")
    preset_choices = [
        (patch_name, f"I{slot:02d}")
        for patch_name, slot in state.PATCH_BANK
    ]
    updates = []
    # One gr.update per voice_dropdown, matching get_refresh_outputs()
    for i in range(len(voice_dropdowns)):
        default_preset = state.tg_states[i + 1]["PRESET"]
        updates.append(
            gr.update(choices=preset_choices, value=default_preset)
        )
    return updates



def get_refresh_outputs():
    """Returns the components that need to be refreshed"""
    global voice_dropdowns
    return voice_dropdowns


def lcd_display():
    # Store all link states in a dictionary
    all_link_states = {}

    # First pass: collect all link states
    for tg, tg_settings in state.tg_states.items():
        link_state = tg_settings['TG']
        all_link_states[int(tg)] = link_state

    # Create the single visual representation
    visual = "["
    for i in range(1, 9):
        column = f"I{i:02d}"

        if i in all_link_states and all_link_states[i] == 1:
            visual += "<--"
        else:
            visual += column

        # Add separator except after the last column
        if i < 8:
            visual += "|"

    visual += "]"
    return visual