import gradio as gr
import app.state
import mido
from core.tx802_utils import play_test_notes as play_test_notes_util, load_config, save_config

state_manager = app.state


def setup_tab():
    # Define Gradio event handlers
    def on_output_selected(port_name):
        try:
            state_manager.set_output_port(port_name)
            config["output_port"] = port_name
            save_config(config)
            return f"✅ Output port set to: {port_name}"
        except Exception as e:
            return f"❌ Failed to set output port: {e}"

    def on_input_selected(port_name):
        try:
            state_manager.set_input_port(port_name)
            config["input_port"] = port_name
            save_config(config)
            return f"✅ Input port set to: {port_name}"
        except Exception as e:
            return f"❌ Failed to set input port: {e}"

    def on_forwarding_toggle(enabled):
        try:
            if enabled:
                if not state_manager.midi_input or not state_manager.midi_output:
                    return "⚠️ Cannot enable forwarding: Configure both input and output ports first"
                success = state_manager.start_midi_forwarding()
                config["midi_forwarding"] = True
                save_config(config)
                return "✅ MIDI forwarding enabled" if success else "❌ Failed to start MIDI forwarding"
            else:
                state_manager.stop_midi_forwarding()
                config["midi_forwarding"] = False
                save_config(config)
                return "✅ MIDI forwarding disabled"
        except Exception as e:
            return f"❌ Error controlling MIDI forwarding: {e}"

    def gradio_test_notes():
        try:
            import app.state
            state_manager = app.state
            port = state_manager.midi_output
            if not port or port.closed:
                return "❌ No MIDI Output port selected or port is closed."
            play_test_notes_util(port)
            return "🎵 Test notes sent!"
        except Exception as e:
            return f"❌ Failed to play test notes: {e}"

    config = load_config()

    # Get available MIDI port names
    output_ports = state_manager.list_output_ports()
    input_ports = state_manager.list_input_ports()

    saved_out = config.get("output_port")
    saved_in = config.get("input_port")
    saved_forwarding = config.get("midi_forwarding", False)

    default_out = saved_out if saved_out in output_ports else None
    default_in = saved_in if saved_in in input_ports else None

    with gr.Row():
        with gr.Column(scale=1):
            gr.Markdown("# MIDI Port Setup")
        with gr.Column(scale=1):
            gr.Markdown("# Other Setup")

    with gr.Row():
        with gr.Column(scale=1):
            with gr.Row():
                in_port_dropdown = gr.Dropdown(
                    choices=input_ports,
                    label="Select MIDI Input Port",
                    value=default_in,
                    interactive=True
                )
                input_status = gr.Textbox(label="Input Port Status", interactive=False)

            with gr.Row():
                out_port_dropdown = gr.Dropdown(
                    choices=output_ports,
                    label="Select MIDI Output Port",
                    value=default_out,
                    interactive=True
                )
                output_status = gr.Textbox(label="Output Port Status", interactive=False)

            with gr.Row():
                forwarding_toggle = gr.Checkbox(label="Enable MIDI Forwarding", value=False, interactive=True)
                forwarding_status = gr.Textbox(show_label=False, interactive=False)


            with gr.Row():
                test_button = gr.Button("Play Test Notes")
                test_output = gr.Textbox(label="Test Status", interactive=False)
                test_button.click(gradio_test_notes, outputs=test_output)

        with gr.Column(scale=1):
            gr.Textbox("Right Side", interactive=False)

    out_port_dropdown.change(on_output_selected, inputs=out_port_dropdown, outputs=output_status)
    in_port_dropdown.change(on_input_selected, inputs=in_port_dropdown, outputs=input_status)
    forwarding_toggle.change(on_forwarding_toggle, inputs=forwarding_toggle, outputs=forwarding_status)

    # Automatically set ports if valid saved values exist
    if default_out:
        state_manager.set_output_port(default_out, auto_restart_forwarding=False)
        output_status.value = f"✅ Set to: {default_out}"
    if default_in:
        state_manager.set_input_port(default_in, auto_restart_forwarding=False)
        input_status.value = f"✅ Set to: {default_in}"

    # Auto-enable forwarding if saved setting is True
    if saved_forwarding and default_in and default_out:
        success = state_manager.start_midi_forwarding()
        forwarding_status.value = "✅ MIDI forwarding enabled" if success else "❌ Failed to start MIDI forwarding"