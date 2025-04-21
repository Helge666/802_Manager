import gradio as gr
import threading
import app.state
from core import tx802_utils

state_manager = app.state
midi_output = app.state.midi_output

# Semaphore to prevent concurrent MIDI operations
midi_lock = threading.Semaphore(1)


def setup_tab():
    """Setup the TX802 Front Panel tab UI"""

    # --- Gradio UI ---
    with gr.Row():
        with gr.Column(scale=6, variant="default"):
            with gr.Column():
                gr.Markdown(f"# Yamaha TX802 Remote Control")

    # Status area that spans across the whole screen
    status = gr.Textbox(label="Status")

    with gr.Row():
        with gr.Column(scale=6, variant="default"):
            with gr.Column():
                # UI Sections
                with gr.Row():
                    gr.Markdown("## TONE GENERATOR ON/OFF / PARAMETER SELECT")
                with gr.Row():
                    for i in range(1, 9):
                        gr.Button(str(i), scale=0).click(lambda i=i: send_button(f"TG{i}"), outputs=status)

                with gr.Row():
                    gr.Markdown("## MODE SELECT")
                with gr.Row():
                    gr.Button("PERFORM SELECT", scale=0, elem_id="PERFORM_SELECT").click(lambda: send_button("PERFORM_SELECT"), outputs=status)
                    gr.Button("VOICE SELECT", scale=0, elem_id="VOICE_SELECT").click(lambda: send_button("VOICE_SELECT"), outputs=status)
                    gr.Button("SYSTEM SETUP", scale=0, elem_id="SYSTEM_SETUP").click(lambda: send_button("SYSTEM_SETUP"), outputs=status)
                    gr.Button("UTILITY", scale=0, elem_id="UTILITY").click(lambda: send_button("UTILITY"), outputs=status)

                with gr.Row():
                    gr.Button("PERFORM EDIT", scale=0).click(lambda: send_button("PERFORM_EDIT"), outputs=status)
                    gr.Button("VOICE EDIT I", scale=0).click(lambda: send_button("VOICE_EDIT_I"), outputs=status)
                    gr.Button("VOICE EDIT II", scale=0).click(lambda: send_button("VOICE_EDIT_II"), outputs=status)
                    gr.Button("STORE/COMPARE", scale=0).click(lambda: send_button("STORE"), outputs=status)

                with gr.Row():
                    gr.Markdown("## NUMBER/CHARACTER BUTTONS")
                with gr.Row():
                    gr.Button("7 V W X", scale=0).click(lambda label="7": send_button(label), outputs=status)
                    gr.Button("8 Y Z !", scale=0).click(lambda label="8": send_button(label), outputs=status)
                    gr.Button("9 # & +", scale=0).click(lambda label="9": send_button(label), outputs=status)
                    gr.Button("- / . ,", scale=0).click(lambda label="DASH": send_button(label), outputs=status)

                with gr.Row():
                    gr.Button("4 M N O", scale=0).click(lambda label="4": send_button(label), outputs=status)
                    gr.Button("5 P Q R", scale=0).click(lambda label="5": send_button(label), outputs=status)
                    gr.Button("6 S T U", scale=0).click(lambda label="6": send_button(label), outputs=status)
                    gr.Button("ON/YES +1", scale=0).click(lambda: send_button("YES"), outputs=status)

                with gr.Row():
                    gr.Button("1 D E F", scale=0).click(lambda label="1": send_button(label), outputs=status)
                    gr.Button("2 G H I", scale=0).click(lambda label="2": send_button(label), outputs=status)
                    gr.Button("3 J K L", scale=0).click(lambda label="3": send_button(label), outputs=status)
                    gr.Button("OFF/NO -1", scale=0).click(lambda: send_button("NO"), outputs=status)

                with gr.Row():
                    gr.Button("0 A B C", scale=0).click(lambda label="0": send_button(label), outputs=status)
                    gr.Button("INT ←", scale=0).click(lambda: send_button("INT"), outputs=status)
                    gr.Button("CRT →", scale=0).click(lambda: send_button("CRT"), outputs=status)
                    gr.Button("ENTER/SPACE→", scale=0).click(lambda: send_button("ENTER"), outputs=status)

                # Test Note Button
                def play_test():
                    if not midi_lock.acquire(blocking=False):
                        return "MIDI operation in progress, please wait..."

                    try:
                        if not midi_output:
                            return "No MIDI output configured. Please set it in the MIDI Setup tab."
                        tx802_utils.play_test_notes(midi_output)
                        return "Test notes played."
                    except Exception as e:
                        return f"Error playing notes: {str(e)}"
                    finally:
                        midi_lock.release()

        with gr.Column(scale=4, variant="default"):
            with gr.Row():
                gr.Markdown("## MACROS")
            with gr.Row():
                gr.Button("PRTCT OFF", scale=0, variant="primary").click(lambda: send_button("PRTCT_OFF"), outputs=status)
                gr.Button("PRTCT ON", scale=0).click(lambda: send_button("PRTCT_ON"), outputs=status)
                gr.Button("REBOOT", scale=0).click(lambda: send_button("REBOOT"), outputs=status)
                gr.Button("POS1", scale=0).click(lambda: send_button("POS1"), outputs=status)
                gr.Button("Play Notes", scale=0).click(play_test, outputs=status)

    def send_button(button_name):
        if not midi_lock.acquire(blocking=False):
            return "MIDI operation in progress, please wait..."

        try:
            if not midi_output:
                return "MIDI output not configured. Please set it in the MIDI Setup tab."

            success = tx802_utils.process_button_sequence(
                midi_output,
                sequence=[button_name],
                device_id=1,
                delay=0.1,
                verbose=False
            )
            return f"Sent: {button_name}" if success else f"Failed to send: {button_name}"
        except Exception as e:
            return f"Error: {str(e)}"
        finally:
            midi_lock.release()