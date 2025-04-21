import mido
from core.tx802_utils import load_config

config = load_config()

input_port_name = config.get("input_port")
if not input_port_name:
    print("❌ No input_port configured in 802_manager_settings.json")
    exit(1)

print(f"🔍 Attempting to open input port: {input_port_name}")

try:
    with mido.open_input(input_port_name) as port:
        print(f"✅ Listening on: {input_port_name}")
        print("🎹 Press keys on your MIDI keyboard to see messages...\n")
        for msg in port:
            print(f"🎵 {msg}")
except IOError as e:
    print(f"❌ Could not open input port '{input_port_name}': {e}")
