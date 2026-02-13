import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.tx802_utils import send_bank


def main():
    parser = argparse.ArgumentParser(description="Send SysEx data (currently Voice Banks - VMEM) to a Yamaha TX802.")
    parser.add_argument("--bankfile", help="Path to the .syx file to send.")
    parser.add_argument("--device-id", type=int, default=1, help="MIDI device ID (1-16, default: 1)")
    parser.add_argument("--output-port", help="MIDI output port name (if not specified, will use saved config or prompt)")
    parser.add_argument("--stopafter", type=int, help="Only send the first N voices (1-31), causing a partial transfer")

    args = parser.parse_args()

    if not args.bankfile:
        parser.error("Parameter --bankfile is required")

    # Call the function from tx_utils
    success = send_bank(args.bankfile, args.device_id, args.output_port, args.stopafter)

    # Exit with appropriate status code
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()