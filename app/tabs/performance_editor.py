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

preset_dropdowns = []
components_to_refresh = []  # Will be populated in setup_tab

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
    global preset_dropdowns, components_to_refresh
    preset_dropdowns = []

    # state.init_tx802_performance_state()
    preset_bank_state = gr.State(state.PRESET_BANK)

    col_widths = {
        "TG": 50, "Link": 70, "Preset": 160,
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
        ("Preset", col_widths["Preset"]),
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
        "TG": "#", "Link": "TG", "Preset": "Preset",
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

                            elif col_name == "Preset":
                                # Preset dropdown: display the preset name, return device code "I01"–"I32"
                                # Build choices as (label, value) pairs
                                preset_choices = [
                                    (f"[I{slot:02d}] {preset_name}", f"I{slot:02d}")
                                    for preset_name, slot in state.PRESET_BANK
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
                                preset_dropdowns.append(elem)

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

    # Update the components_to_refresh with the preset_dropdowns
    components_to_refresh = preset_dropdowns

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

    def handle_single_change(changed_value, index, current_preset_bank):
        """
        Handles a change in a single UI element and sends the corresponding
        SysEx message to the TX802.
        """
        interactive_cols_per_tg = 10

        status_message = ""
        save_status = "No save"

        tg = index // interactive_cols_per_tg + 1  # Tone Generator number (1-8)
        param_pos = index % interactive_cols_per_tg  # Parameter index within the TG's elements
        param_name = param_names_per_tg[param_pos]  # Get the base parameter name
        key = f"{param_name}{tg}"  # Construct the full parameter key

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
                # Special handling for PRESET changes on TGs that are Off
                tg_should_be_off = False
                extra_commands = {}

                # If we're changing a PRESET and the TG is Off, we'll need to turn it back Off
                if param_name == "PRESET" and state.tg_states[tg]["TG"] == "Off":
                    tg_should_be_off = True
                    extra_commands = {f"TG{tg}": "Off"}

                # First send the main parameter change
                success = edit_performance(
                    port=state.midi_output,
                    device_id=1,
                    delay_after=0.02,
                    play_notes=False,
                    **{key: internal_val}
                )

                # If it's a PRESET change that turned an Off TG On, turn it back Off
                if success and tg_should_be_off:
                    success = edit_performance(
                        port=state.midi_output,
                        device_id=1,
                        delay_after=0.02,
                        play_notes=False,
                        **extra_commands
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
            # Pass the element's value, its index, and the current preset bank state
            inputs=[element, gr.State(idx), preset_bank_state],
            outputs=[output_display, save_status_display] # Update the status textbox
        )

def refresh_tab():
    import app.state as state
    from core.tx802_utils import edit_performance

    # Set the current tab
    state.set_current_tab("Perform Edit")

    # Rebuild the (label, value) pairs exactly as in setup_tab
    preset_choices = [
        (f"[I{slot:02d}] {preset_name}", f"I{slot:02d}")
        for preset_name, slot in state.PRESET_BANK
    ]

    updates = []
    for i in range(len(preset_dropdowns)):
        default_preset = state.tg_states[i + 1]["PRESET"]
        updates.append(
            gr.update(choices=preset_choices, value=default_preset)
        )

    # Check if we're specifically returning from preset_browser
    if state.previous_tab == "Preset Browser":
        # print("PERFORMANCE EDITOR - Returning from preset Browser, restoring performance state:")
        button_commands = {}

        # Only proceed if we have a valid MIDI output
        if not state.midi_output:
            print("  • No MIDI output configured - skipping TG restoration")
            # Set the current tab in the state
            state.set_current_tab("Perform Edit")
            return updates

        # 1. Turn back ON any TGs 2-8 that should be ON
        for tg_num in range(2, 9):
            current_state = state.tg_states[tg_num]["TG"]
            if current_state == "On":
                # print(f"  • Turning ON: TG{tg_num}")
                button_commands[f"TG{tg_num}"] = "On"

        # 2. For TG1, restore parameters from saved state
        # print("  • Restoring TG1 parameters to saved values:")
        for param, saved_value in state.tg_states[1].items():
            # Skip TG (always ON) and PRESET (already handled by UI)
            if param in ["TG", "PRESET"]:
                continue

            default_value = state.DEFAULT_TG_STATE.get(param)
            if saved_value != default_value:
                # print(f"    - TG1 {param}: {default_value} → {saved_value}")
                button_commands[f"{param}1"] = saved_value

        # Send all commands in a single edit_performance call
        if button_commands:
            try:
                edit_performance(
                    port=state.midi_output,
                    device_id=1,
                    delay_after=0.02,
                    play_notes=False,
                    **button_commands
                )
            except Exception as e:
                print(f"  • Error sending commands: {e}")
    else:
        # Normal refresh (not from Preset Browser)
        pass

    return updates

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