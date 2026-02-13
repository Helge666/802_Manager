import argparse
import sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.dx7_utils import create_bank


def main():
    parser = argparse.ArgumentParser(description="DX7 Bank File Creator")

    parser.add_argument("--bankfile", required=True, help="Path to the output .syx bank file to be created")
    parser.add_argument("--presetfiles", help="Comma-separated list of .syx single preset files to include")
    parser.add_argument("--db", help="Path to SQLite database containing presets")
    parser.add_argument("--presetids", help="Comma-separated list of preset IDs to retrieve from the database")

    args = parser.parse_args()

    # Validate arguments
    if args.presetids and not args.db:
        parser.error("--presetids requires --db to be specified")

    # Check if at least one source of presetes is provided
    if not args.presetfiles and not (args.db and args.presetids):
        parser.error("No preset sources specified. Use --presetfiles and/or --db with --presetids")

    # Call the function from dx7_bank_utils
    success = create_bank(args.bankfile, args.presetfiles, args.db, args.presetids)

    # Exit with appropriate status code
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
