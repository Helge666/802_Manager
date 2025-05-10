import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.tx802_utils import send_preset_to_buffer
from core.dx7_utils import get_preset_from_db, connect_to_db


def main():
    parser = argparse.ArgumentParser(description="Send a single preset to a Yamaha TX802's Voice Edit Buffer.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--presetfile", help="Path to a .syx file containing a single voice preset")
    group.add_argument("--presetid", type=int, help="Database ID of the preset to send")
    parser.add_argument("--db", help="Path to preset database (required with --presetid)", default=".tx802_presetes.db")
    parser.add_argument("--device-id", type=int, default=1, help="MIDI device ID (1-16, default: 1)")
    parser.add_argument("--output-port", help="MIDI output port name (if not specified, will use saved config or prompt)")

    args = parser.parse_args()

    # Validate args
    if args.presetid and not args.db:
        parser.error("--db is required when using --presetid")

    # Get SysEx data from either file or database
    sysex_data = None

    if args.presetfile:
        if not os.path.exists(args.presetfile):
            print(f"Error: File not found: {args.presetfile}")
            return 1

        with open(args.presetfile, 'rb') as f:
            sysex_data = f.read()

        print(f"Read {len(sysex_data)} bytes from '{args.presetfile}'")

    elif args.presetid:
        if not os.path.exists(args.db):
            print(f"Error: Database not found: {args.db}")
            return 1

        conn = connect_to_db(args.db)
        preset_id, preset_name, sysex_data = get_preset_from_db(conn, args.presetid)

        if not preset_id or not sysex_data:
            print(f"Error: preset with ID {args.presetid} not found in database")
            conn.close()
            return 1

        conn.close()

    # Send the preset to the edit buffer
    print(f"Sent preset: '{preset_name}' (ID: {preset_id})")
    success = send_preset_to_buffer(sysex_data, args.device_id, args.output_port)

    # Exit with appropriate status code
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())