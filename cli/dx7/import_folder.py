import argparse
import sys, os
import pathlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.dx7_utils import extract_bank


def process_folder(base_folder, db_path, output_folder=None, report=False, dry_run=False):
    """
    Process all .syx files in the given folder and its subfolders

    Args:
        base_folder (str): The base folder to search for .syx files
        db_path (str): Path to the SQLite database for storing patches
        output_folder (str, optional): Folder where individual patch files will be saved
        report (bool): Generate patch parameter reports
        dry_run (bool): If True, don't actually import to database, just print what would happen

    Returns:
        tuple: (total_files_processed, successful_imports)
    """
    total_files = 0
    successful = 0

    # Walk through the directory tree
    for root, dirs, files in os.walk(base_folder):
        # Get the subfolder name (to use as origin)
        subfolder = os.path.basename(root)

        # Skip the base folder itself
        if root == base_folder:
            continue

        # Process .syx files in the current folder
        syx_files = [f for f in files if f.lower().endswith('.syx')]

        for file in syx_files:
            total_files += 1
            file_path = os.path.join(root, file)

            print(f"Processing: {file_path}")
            print(f"Using origin: {subfolder}")

            if dry_run:
                print(f"[DRY RUN] Would import {file_path} with origin '{subfolder}'")
                successful += 1
            else:
                # Call the extract_bank function with positional arguments
                result = extract_bank(
                    file_path,
                    output_folder,
                    db_path,
                    report,
                    subfolder
                )

                # Count successful imports
                if result:
                    successful += 1
                    print(f"Successfully processed {file_path}")
                else:
                    print(f"Failed to process {file_path}, continuing with next file")

    return total_files, successful


def main():
    parser = argparse.ArgumentParser(description="Yamaha DX7 SysEx Bank Folder Importer")

    parser.add_argument("--folder", default="C:\\",
                        help="Base folder containing subfolders with .syx files")
    parser.add_argument("--db", required=True, help="Path to SQLite database for storing patches")
    parser.add_argument("--output", help="Folder where individual patch files will be saved")
    parser.add_argument("--report", action="store_true", help="Generate patch parameter reports")
    parser.add_argument("--dry-run", action="store_true",
                        help="Don't actually import to database, just print what would happen")

    args = parser.parse_args()

    # Basic validation
    if args.report and not args.output:
        parser.error("--report requires --output to be specified")

    print(f"Starting import process from {args.folder}")
    print(f"Database: {args.db}")
    if args.dry_run:
        print("DRY RUN MODE: No changes will be made to the database")

    # Process the folder
    total, successful = process_folder(
        args.folder,
        args.db,
        args.output,
        args.report,
        args.dry_run
    )

    # Print summary
    print("\nImport Summary:")
    print(f"Total .syx files found: {total}")
    print(f"Successfully processed: {successful}")
    print(f"Failed: {total - successful}")

    # Exit with appropriate status code
    sys.exit(0 if successful > 0 else 1)


if __name__ == "__main__":
    main()