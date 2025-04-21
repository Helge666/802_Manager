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
    parser.add_argument("--patchfiles", help="Comma-separated list of .syx single patch files to include")
    parser.add_argument("--db", help="Path to SQLite database containing patches")
    parser.add_argument("--patchids", help="Comma-separated list of patch IDs to retrieve from the database")

    args = parser.parse_args()

    # Validate arguments
    if args.patchids and not args.db:
        parser.error("--patchids requires --db to be specified")

    # Check if at least one source of patches is provided
    if not args.patchfiles and not (args.db and args.patchids):
        parser.error("No patch sources specified. Use --patchfiles and/or --db with --patchids")

    # Call the function from dx7_bank_utils
    success = create_bank(args.bankfile, args.patchfiles, args.db, args.patchids)

    # Exit with appropriate status code
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
