import argparse
import sys, os
import sqlite3
import random

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from core.dx7_utils import create_bank, connect_to_db


def main():
    parser = argparse.ArgumentParser(description="DX7 Surprise Me - Create a bank with randomly selected presets")
    parser.add_argument("--db", required=True, help="Path to SQLite database containing presets")
    parser.add_argument("--bankfile", required=True, help="Path to the output .syx bank file to be created")
    parser.add_argument("--count", type=int, default=32, help="Number of presets to include (default: 32)")

    args = parser.parse_args()

    # Validate input
    if not os.path.exists(args.db):
        print(f"Error: Database file '{args.db}' not found.")
        return False

    # Connect to the database
    conn = connect_to_db(args.db)
    if not conn:
        print("Failed to connect to database. Aborting.")
        return False

    try:
        # Get count of available presets
        cursor = conn.cursor()
        cursor.execute("SELECT COUNT(*) FROM presets")
        total_presets = cursor.fetchone()[0]

        if total_presets == 0:
            print("Error: No presets found in the database.")
            return False

        num_presets = min(args.count, 32)  # Ensure we don't exceed 32 presets

        if total_presets < num_presets:
            print(f"Warning: Only {total_presets} presets available in database, requested {num_presets}.")
            num_presets = total_presets

        # Get random preset IDs
        cursor.execute("SELECT id FROM presets ORDER BY RANDOM() LIMIT ?", (num_presets,))
        selected_ids = [row[0] for row in cursor.fetchall()]

        print(f"Selected {len(selected_ids)} random presets.")

        # Convert to comma-separated string of IDs
        preset_ids_str = ",".join(str(id) for id in selected_ids)

        # Create the bank file
        success = create_bank(args.bankfile, None, args.db, preset_ids_str)

        if success:
            print(f"Successfully created surprise bank file: {args.bankfile}")
            return True
        else:
            print("Bank creation failed.")
            return False

    except sqlite3.Error as e:
        print(f"Database error: {e}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        conn.close()


if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)