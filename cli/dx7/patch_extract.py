import argparse
import sys, os
import pathlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.dx7_utils import extract_bank


def main():
    parser = argparse.ArgumentParser(description="Yamaha DX7 SysEx Bank Extractor")

    parser.add_argument("--bankfile", required=True, help="Path to the input DX7 bank file (.syx)")
    parser.add_argument("--folder", help="Folder where individual preset files will be saved")
    parser.add_argument("--db", help="Path to SQLite database for storing presetss")
    parser.add_argument("--report", action="store_true", help="Generate preset parameter reports")
    parser.add_argument("--origin", help="Origin (e.g. creator or website) to store with presets in the database")

    args = parser.parse_args()

    # Basic validation
    if args.report and not args.folder:
        parser.error("--report requires --folder to be specified")

    # Call the function from dx7_bank_utils
    success = extract_bank(args.bankfile, args.folder, args.db, args.report, args.origin)

    # Exit with appropriate status code
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()