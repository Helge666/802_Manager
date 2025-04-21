import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.tx802_utils import send_patch_to_buffer
from core.dx7_utils import get_patch_from_db, connect_to_db


def main():
    parser = argparse.ArgumentParser(description="Send a single patch to a Yamaha TX802's Voice Edit Buffer.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--patchfile", help="Path to a .syx file containing a single voice patch")
    group.add_argument("--patchid", type=int, help="Database ID of the patch to send")
    parser.add_argument("--db", help="Path to patch database (required with --patchid)", default=".tx802_patches.db")
    parser.add_argument("--device-id", type=int, default=1, help="MIDI device ID (1-16, default: 1)")
    parser.add_argument("--output-port", help="MIDI output port name (if not specified, will use saved config or prompt)")

    args = parser.parse_args()

    # Validate args
    if args.patchid and not args.db:
        parser.error("--db is required when using --patchid")

    # Get SysEx data from either file or database
    sysex_data = None

    if args.patchfile:
        if not os.path.exists(args.patchfile):
            print(f"Error: File not found: {args.patchfile}")
            return 1

        with open(args.patchfile, 'rb') as f:
            sysex_data = f.read()

        print(f"Read {len(sysex_data)} bytes from '{args.patchfile}'")

    elif args.patchid:
        if not os.path.exists(args.db):
            print(f"Error: Database not found: {args.db}")
            return 1

        conn = connect_to_db(args.db)
        patch_id, patch_name, sysex_data = get_patch_from_db(conn, args.patchid)

        if not patch_id or not sysex_data:
            print(f"Error: Patch with ID {args.patchid} not found in database")
            conn.close()
            return 1

        conn.close()

    # Send the patch to the edit buffer
    print(f"Sent patch: '{patch_name}' (ID: {patch_id})")
    success = send_patch_to_buffer(sysex_data, args.device_id, args.output_port)

    # Exit with appropriate status code
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())