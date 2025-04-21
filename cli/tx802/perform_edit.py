import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.tx802_utils import send_parameter_edits

def main():
    parser = argparse.ArgumentParser(description="Edit parameters in the Yamaha TX802 Performance Edit Buffer (PCED).")
    parser.add_argument("--edits", type=str, required=True,
                        help="Comma-separated list of parameters to edit (e.g., 'VNUM1=45,OUTCH2=3,OUTVOL3=99')")
    parser.add_argument("--device-id", type=int, default=1, help="MIDI device ID (1-16, default: 1)")
    parser.add_argument("--output-port", help="MIDI output port name (if not specified, will use saved config or prompt)")
    parser.add_argument("--delay", type=float, default=0.05, help="Delay in seconds after sending each parameter (default: 0.05)")

    args = parser.parse_args()

    # Simply pass the edits string to the utility function, just like tx802_btn_press.py does
    success = send_parameter_edits(args.edits, args.device_id, args.output_port, args.delay)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    sys.exit(main())