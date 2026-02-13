import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.tx802_utils import send_button_sequence


def main():
    parser = argparse.ArgumentParser(description="Send button presses to Yamaha TX802 via MIDI SysEx.")
    parser.add_argument("--device-id", type=int, default=1, choices=range(1, 17), help="MIDI Device ID (1-16). Default: 1.")
    parser.add_argument("--delay", type=float, default=0.1, help="Delay after button press (seconds). Default: 0.1")
    parser.add_argument("--buttons", type=str, help="Comma-separated sequence of buttons to press with optional repeat counts (e.g., 'PERFORMANCE_SELECT,PERFORMANCE_EDIT,PLUS=5,TEXT=Hello')")

    args = parser.parse_args()

    if not args.buttons:
        parser.error("Parameter --buttons is required")

    # Call the imported function from tx_utils.py
    success = send_button_sequence(args.buttons, args.device_id, args.delay)

    # Return appropriate exit code
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())