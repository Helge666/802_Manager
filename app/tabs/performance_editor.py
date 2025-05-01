import gradio as gr
import mido

from core.tx802_utils import edit_performance
import app.state as state

# --- Constants and Helper Functions ---
voice_dropdowns = []

def get_midi_note_name(note_number):
    if not 0 <= note_number <= 127:
        return "Invalid"
    notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    octave = (note_number // 12) - 2
    note_index = note_number % 12
    return f"{notes[note_index]}{octave}"

MIDI_NOTES = [get_midi_note_name(i) for i in range(128)]
ON_OFF_CHOICES = ["Off", "On"]
RECEIVE_CHOICES = ["Off"] + [str(i) for i in range(1, 17)] + ["Omni"]
OUTPUT_CHOICES = ["L", "R", "L&R"]

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

    state.init_tx802_performance_state()
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
                                if i == 0:
                                    # TG1 is always on
                                    elem = gr.Dropdown(choices=["On", "Off"], value="On", show_label=False, interactive=False, container=False)
                                else:
                                    elem = gr.Dropdown(choices=["On", "Off"], value="Off", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Voice":
                                elem = gr.Dropdown(choices=state.PATCH_BANK, value=state.PATCH_BANK[i % len(state.PATCH_BANK)][1], show_label=False, interactive=True, container=False)
                                voice_dropdowns.append(elem)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Receive":
                                elem = gr.Dropdown(choices=RECEIVE_CHOICES, value="Off", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Low":
                                elem = gr.Dropdown(choices=MIDI_NOTES, value="C-2", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "High":
                                elem = gr.Dropdown(choices=MIDI_NOTES, value="G8", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Detune":
                                elem = gr.Number(minimum=0, maximum=15, step=1, value=7, show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Shift":
                                elem = gr.Number(minimum=0, maximum=128, step=1, value=0, show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Volume":
                                elem = gr.Number(minimum=0, maximum=100, step=1, value=80, show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Output":
                                elem = gr.Dropdown(choices=OUTPUT_CHOICES, value="L&R", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)
                            elif col_name == "Damp":
                                elem = gr.Dropdown(choices=ON_OFF_CHOICES, value="Off", show_label=False, interactive=True, container=False)
                                all_interactive_inputs.append(elem)

    output_display = gr.Textbox(label="Status", interactive=False)

    param_names_per_tg = [
        "LINK",
        "VNUM",
        "RXCH",
        "NTMTL",
        "NTMTH",
        "DETUNE",
        "NSHFT",
        "OUTVOL",
        "OUTCH",
        "FDAMP"
    ]

    def handle_single_change(changed_value, index, current_patch_bank):
        """
        Handles a change in a single UI element and sends the corresponding
        SysEx message to the TX802.
        """
        interactive_cols_per_tg = 10

        tg = index // interactive_cols_per_tg + 1  # Tone Generator number (1-8) [cite: 74]
        param_pos = index % interactive_cols_per_tg  # Parameter index within the TG's elements [cite: 74]

        param_name = param_names_per_tg[param_pos]  # Get the base parameter name (e.g., "VNUM", "RXCH") [cite: 74]

        print(f"[DEBUG] index={index}, tg={tg}, param_pos={param_pos}, param_name={param_name}, changed_value={changed_value}")

        key = f"{param_name}{tg}"  # Construct the full parameter key (e.g., "VNUM1", "RXCH1") [cite: 74]

        internal_val = None
        user_facing_value = changed_value  # Store the value from the UI for reporting

        # --- Translate UI value to internal TX802 value ---
        if param_name == "VNUM":
            # Find the 1-based index of the selected patch name in the bank
            try:
                # current_patch_bank is the list of (id, name) tuples from PATCH_BANK state
                patch_index_0_based = [name for _, name in current_patch_bank].index(changed_value)
                internal_val = patch_index_0_based + 1  # VNUM is 1-based (1-32 or 1-128 depending on context, here 1-32 likely intended for bank slots) [cite: 119, 123]
            except ValueError:
                print(f"Warning: Patch '{changed_value}' not found in current bank. Cannot send VNUM update.")
                return f"Error: Patch '{changed_value}' not found in bank."
        elif param_name == "LINK":
            if changed_value == "Off":
                internal_val = 1
                state.update_tg_state(tg, "LINK", 1)
            else:
                internal_val = tg
                state.update_tg_state(tg, "LINK", 0)
        elif param_name == "RXCH":
            internal_val = midi_channel_to_internal(changed_value)  # Convert "Off", "1"-"16", "Omni" [cite: 52]
        elif param_name in ("NTMTL", "NTMTH"):
            internal_val = note_name_to_midi(changed_value)  # Convert "C-2".."G8" to 0-127
        elif param_name == "OUTCH":
            internal_val = output_assign_to_code(changed_value)  # Convert "Off", "L", "R", "L&R" to 0-3 [cite: 55, 75]
        elif param_name == "FDAMP":
            internal_val = on_off_to_bool(changed_value)  # Convert "On"/"Off" to 1/0 [cite: 56]
        elif param_name == "DETUNE":
            # Ensure value is within 0-14 range expected by edit_performance [cite: 119, 123]
            internal_val = int(changed_value)
            if not (0 <= internal_val <= 14):
                print(f"Warning: Detune value {internal_val} out of range (0-14). Clamping.")
                internal_val = max(0, min(internal_val, 14))
        elif param_name == "NSHFT":
            # Ensure value is within 0-48 range expected by edit_performance [cite: 120, 124]
            internal_val = int(changed_value)
            if not (0 <= internal_val <= 48):
                print(f"Warning: Note Shift value {internal_val} out of range (0-48). Clamping.")
                internal_val = max(0, min(internal_val, 48))
        elif param_name == "OUTVOL":
            # Ensure value is within 0-99 range expected by edit_performance [cite: 119, 124]
            internal_val = int(changed_value)
            if not (0 <= internal_val <= 99):
                print(f"Warning: Volume value {internal_val} out of range (0-99). Clamping.")
                internal_val = max(0, min(internal_val, 99))
        else:
            internal_val = changed_value  # Use value directly (may need conversion for On/Off to 0/1 if not handled elsewhere)

        if internal_val is not None:
            # --- Send the SysEx message first ---
            if not isinstance(state.midi_output, mido.ports.BaseOutput) or getattr(state.midi_output, 'closed', True):
                status_message = "Error: MIDI Output Port not configured or closed."
                print(status_message)
                return status_message

            try:
                print(f"Sending: {key} = {user_facing_value} (Internal: {internal_val})")
                success = edit_performance(port=state.midi_output, device_id=1, delay_after=0.02, play_notes = False, **{key: internal_val})

                if success:
                    status_message = lcd_display()
                    # --- Update tg_states ---
                    if param_name == "VNUM":
                        state.handle_tg_voice_change(tg, internal_val)
                    elif param_name == "OUTCH":
                        state.handle_tg_output_change(tg, internal_val)
                    else:
                        state.update_tg_state(tg, param_name, internal_val)

                else:
                    status_message = f"Failed to send: {key} = {user_facing_value}"
                    print(status_message)
            except Exception as e:
                status_message = f"Error sending SysEx: {e}"
                print(status_message)

            return status_message

    # Attach a change handler to each element with index tracking
    for idx, element in enumerate(all_interactive_inputs):
        element.change(
            fn=handle_single_change,  # Pass the function directly
            # Pass the element's value, its index, and the current patch bank state
            inputs=[element, gr.State(idx), patch_bank_state],
            outputs=output_display  # Update the status textbox [cite: 76]
        )

def refresh_tab():
    # Update all voice dropdowns with current PATCH_BANK
    return [gr.update(choices=state.PATCH_BANK, value=state.PATCH_BANK[i % len(state.PATCH_BANK)][1]) for i in range(8)]

def get_refresh_outputs():
    """Returns the components that need to be refreshed"""
    global voice_dropdowns
    return voice_dropdowns


def lcd_display():
    # Store all link states in a dictionary
    all_link_states = {}

    # First pass: collect all link states
    for tg, tg_settings in state.tg_states.items():
        link_state = tg_settings['LINK']
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