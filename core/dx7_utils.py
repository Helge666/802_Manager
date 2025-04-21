import os
import re
import sys
import sqlite3
import traceback
import hashlib
import pathlib
from typing import Optional, Tuple, List, Dict, Union, Any, ByteString


# --- Default INIT Patch Data (155 bytes, single voice format) ---
# CORRECTED Structure & Length: 6xOp[21], PEG[8], ALG[1], FB[1], OSYNC[1], LFO[7], TRNSP[1], NAME[10] = 155 bytes

# This is a really ugly sounding INIT patch. Needs to be revised to just be a standard sine...
DEFAULT_INIT_VOICE_155 = bytes([
    # OP6 - 21 bytes: R1-4, L1-4, BP, LD, RD, LC, RC, RS, Detune(7), AMS(0), KVS(0), OL(99), Mode(0), FC(1), FF(0), Dummy(0)
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,  # Added Detune=7 based on spec D index 20
    # OP5 - 21 bytes
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,
    # OP4 - 21 bytes
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,
    # OP3 - 21 bytes
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,
    # OP2 - 21 bytes
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,
    # OP1 (Carrier) - 21 bytes
    50, 50, 50, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 7, 0, 0, 99, 0, 1, 0, 7,
    # Pitch EG - 8 bytes (Indices 126-133)
    50, 50, 50, 50, 50, 50, 50, 50,
    # Algorithm - 1 byte (Index 134)
    0,
    # Feedback - 1 byte (Index 135)
    0,
    # OSC Sync - 1 byte (Index 136)
    0,
    # LFO (Speed, Delay, PMD, AMD, Sync, Wave, PMS) - 7 bytes (Indices 137-143)
    35, 0, 0, 0, 0, 0, 0,
    # Transpose - 1 byte (Index 144)
    24,
    # Voice Name - 10 bytes (Indices 145-154), ensure last is space 0x20
    ord('I'), ord('N'), ord('I'), ord('T'), ord(' '), ord(' '), ord(' '), ord(' '), ord(' '), 0x20,
])  # Total 155 bytes

# Dictionary for specific character replacements in patch names for DB storage
# Maps the character extracted by ASCII decode to the desired character.
DB_PATCHNAME_CHAR_MAP = {
    '\\': 'Y'  # Replace backslash (from byte 0x5C, likely Yen ¥) with Y
    # Add other mappings here later if needed, e.g., '`': 'A'
}


# --- Common Helper Functions ---

def checksum(data: bytes) -> int:
    """Calculates the checksum for DX7 SysEx data."""
    total = sum(data) & 0x7F  # Sum all bytes, masking with 0x7F
    return (128 - total) & 0x7F  # 7-bit checksum calculation


def sanitize_filename(name: str, fallback_name: str = "_invalid_name_") -> str:
    """Sanitizes a string to be safe for use as a filename across platforms."""
    if not isinstance(name, str):
        name = str(name)

    # Convert to str and strip dangerous edges
    name = name.strip().rstrip('.')

    # Replace spaces with underscores early
    name = name.replace(' ', '_')

    # Whitelist: ASCII letters, digits, underscore, dash, dot
    sanitized = re.sub(r'[^A-Za-z0-9._-]', '_', name)

    # Remove multiple underscores in a row (optional, for aesthetics)
    sanitized = re.sub(r'_+', '_', sanitized)

    # Prevent dangerous filenames or empty result
    if not sanitized or sanitized in {
        'CON', 'PRN', 'AUX', 'NUL',
        *(f'COM{i}' for i in range(1, 10)),
        *(f'LPT{i}' for i in range(1, 10))
    } or all(c == '.' for c in sanitized):
        return fallback_name

    # Max filename length (can be reduced if file extension is appended later)
    return sanitized[:200]


def verify_single_voice_sysex(sysex_data: bytes, filename: str = "") -> Tuple[bool, str, Optional[bytes]]:
    """Verifies if the provided data is a valid DX7 single voice SysEx message."""
    if not isinstance(sysex_data, bytes):
        return False, f"Invalid data type for SysEx in '{filename}'", None

    expected_length = 163
    if len(sysex_data) != expected_length:
        return False, f"Invalid SysEx length: got {len(sysex_data)}, expected {expected_length}", None

    expected_header = bytes([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B])
    if sysex_data[:6] != expected_header:
        if sysex_data[:2] == bytes([0xF0, 0x43]) and sysex_data[3:6] == bytes([0x00, 0x01, 0x1B]):
            print(f"Note: Non-standard channel nibble found in '{filename}': 0x{sysex_data[2]:02X}.")
        else:
            return False, f"Invalid header in '{filename}'", None

    if sysex_data[-1] != 0xF7:
        return False, f"Invalid end marker in '{filename}'", None

    voice_data = sysex_data[6:-2]
    file_checksum = sysex_data[-2]

    # Check internal data length MUST be 155
    if len(voice_data) != 155:
        return False, f"Internal error: Extracted voice data length {len(voice_data)} != 155", None

    calculated_checksum = checksum(voice_data)
    if calculated_checksum != file_checksum:
        return False, f"Checksum mismatch in '{filename}': file 0x{file_checksum:02X}, calc 0x{calculated_checksum:02X}", None

    return True, "Valid single voice SysEx", voice_data


def is_valid_dx7_bank(file_content: bytes) -> Tuple[bool, str]:
    """Validates if the file content appears to be a standard DX7 32-voice bank."""
    expected_size = 4104
    if len(file_content) != expected_size:
        return False, f"Invalid bank file size: got {len(file_content)}, expected {expected_size}"

    # Check standard DX7 32-voice bulk dump header
    # F0 43 0n 09 20 00 (n = channel 0-F, usually 0)
    header_start = bytes([0xF0, 0x43])
    header_end = bytes([0x09, 0x20, 0x00])
    if not (file_content.startswith(header_start) and file_content[3:6] == header_end):
        # Check header bytes exactly, allowing for channel variation in byte 2
        # F0 43 0n 09 20 00 (n = channel 0-F)
        if not (file_content[0] == 0xF0 and file_content[1] == 0x43 and
                0x00 <= file_content[2] <= 0x0F and  # Check channel byte range
                file_content[3:6] == header_end):
            return False, f"Invalid header bytes: got {file_content[:6].hex(' ')}, expected F0 43 0n 09 20 00"

    # Check footer byte
    if file_content[-1] != 0xF7:
        return False, f"Invalid end marker: got 0x{file_content[-1]:02X}, expected 0xF7"

    # Verify bank checksum (data bytes 6 to 4101 inclusive)
    data = file_content[6:4102]
    if len(data) != 4096:  # 32 patches * 128 bytes/patch
        return False, f"Unexpected data length for checksum: got {len(data)}, expected 4096"

    # Bank checksum calculation: sum data bytes, take lower 7 bits, subtract from 128, take lower 7 bits
    calculated_checksum = (128 - (sum(data) & 0x7F)) & 0x7F
    file_checksum = file_content[4102]

    if calculated_checksum != file_checksum:
        return False, f"Bank checksum mismatch: file=0x{file_checksum:02X}, calculated=0x{calculated_checksum:02X}"

    return True, "Valid DX7 32-voice bank file"


def extract_patch_name_from_sysex(sysex_data: bytes) -> str:
    """Extract patch name from SysEx data for display purposes."""
    if len(sysex_data) != 163:
        return "Unknown"

    # Name is stored at offset 145 in the 155-byte voice data
    # Voice data starts at offset 6 in the SysEx message
    name_start = 6 + 145
    name_end = name_start + 10
    name_bytes = sysex_data[name_start:name_end]

    # Try to decode as ASCII, replacing invalid chars
    try:
        name = name_bytes.decode('ascii', errors='replace').strip()
        return name if name else "Unnamed"
    except:
        # Fallback if decode fails completely
        return "Unnamed"


def extract_patch_name_from_unpacked(unpacked_data_155: bytes) -> str:
    """Extracts the raw patch name (10 bytes) from unpacked 155-byte data."""
    if len(unpacked_data_155) != 155:
        raise ValueError("Expected 155 bytes of unpacked data for name extraction.")
    name_bytes = unpacked_data_155[145:155]  # Bytes 145-154 are the name
    # Attempt to decode as ASCII, replacing errors
    try:
        name = name_bytes.decode('ascii', errors='replace').strip()
    except Exception:
        # Fallback if decode fails entirely (unlikely with replace)
        name = "".join(chr(b) if 32 <= b <= 126 else '?' for b in name_bytes).strip()
    return name if name else "Unnamed"  # Return "Unnamed" if name is empty/whitespace


def apply_db_char_mapping(name: str, mapping_dict: Optional[Dict[str, str]] = None) -> str:
    """Applies character replacements based on the provided dictionary."""
    if mapping_dict is None:
        mapping_dict = DB_PATCHNAME_CHAR_MAP
    mapped_name = "".join(mapping_dict.get(char, char) for char in name)
    return mapped_name


# --- Bank Creator Functions ---

def pack_single_to_bank_voice(voice_data_155: bytes) -> bytes:
    """
    Converts 155-byte single voice data (VCED params 0-154)
    to 128-byte packed bank format, based on dx7-sysex-format.txt
    and observations from bank_dump.txt (byte 127 is 10th name char).
    Assumes OpOnOff is implied by OpLevel or handled by synth/emulator.
    """
    if len(voice_data_155) != 155:
        raise ValueError(f"Expected 155 bytes of single voice data, got {len(voice_data_155)}")

    packed_data = bytearray(128)
    v = voice_data_155

    # Indices based on the 155-byte structure (VCED Params 0-154)
    IDX_PITCH_EG_START = 126
    IDX_ALG = 134
    IDX_FB = 135
    IDX_OSC_SYNC = 136
    IDX_LFO_START = 137
    IDX_TRANSPOSE = 144
    IDX_NAME_START = 145  # Index of first name char (Param 145)

    single_op_len = 21  # Based on spec listing params 0-20 for OP6
    packed_op_len = 17

    # Map: 155-byte index within 21-byte Op block -> Meaning (VCED Params)
    # 0-10: R1-4, L1-4, BP, LD, RD
    # 11: LC
    # 12: RC
    # 13: RS
    # 14: AMS (Param 14 + offset)
    # 15: KVS (Param 15 + offset)
    # 16: OL (Param 16 + offset)
    # 17: Mode (Param 17 + offset)
    # 18: FC (Param 18 + offset)
    # 19: FF (Param 19 + offset)
    # 20: Detune (Param 20 + offset)

    for op_idx in range(6):
        single_start = op_idx * single_op_len
        packed_start = op_idx * packed_op_len

        packed_data[packed_start: packed_start + 11] = v[single_start: single_start + 11]  # EG R1-10

        left_curve = v[single_start + 11] & 0x03  # LC
        right_curve = v[single_start + 12] & 0x03  # RC
        packed_data[packed_start + 11] = (right_curve << 2) | left_curve  # Pack Curves

        # Use Detune from VCED Param 20 (index single_start + 20)
        detune = v[single_start + 20] & 0x0F
        rate_scale = v[single_start + 13] & 0x07  # RS from index 13
        packed_data[packed_start + 12] = (detune << 3) | rate_scale  # Pack Detune/RS

        kvs = v[single_start + 15] & 0x07  # KVS from index 15
        ams = v[single_start + 14] & 0x03  # AMS from index 14, mask 0x03
        packed_data[packed_start + 13] = (kvs << 3) | ams  # Pack KVS/AMS

        packed_data[packed_start + 14] = v[single_start + 16]  # OL from index 16

        mode = v[single_start + 17] & 0x01  # Mode from index 17
        freq_coarse = v[single_start + 18] & 0x1F  # FC from index 18
        packed_data[packed_start + 15] = (freq_coarse << 1) | mode  # Pack Mode/FC

        packed_data[packed_start + 16] = v[single_start + 19]  # FF from index 19

    # --- Pack Global Parameters ---
    packed_offset = 6 * packed_op_len  # 102
    packed_data[packed_offset: packed_offset + 8] = v[IDX_PITCH_EG_START: IDX_PITCH_EG_START + 8]  # PEG
    packed_offset += 8  # 110

    alg = v[IDX_ALG] & 0x1F
    feedback = v[IDX_FB] & 0x07
    osc_sync = v[IDX_OSC_SYNC] & 0x01
    packed_data[packed_offset] = alg  # Pack Alg to 110
    packed_data[packed_offset + 1] = (osc_sync << 3) | feedback  # Pack Sync/FB to 111
    packed_offset += 2  # 112

    lfo_spd, lfo_del, lfo_pmd, lfo_amd = v[IDX_LFO_START: IDX_LFO_START + 4]
    lfo_sync = v[IDX_LFO_START + 4] & 0x01  # LKS
    lfo_wave = v[IDX_LFO_START + 5] & 0x07  # LFW
    lfo_pms = v[IDX_LFO_START + 6] & 0x07  # LPMS
    packed_data[packed_offset: packed_offset + 4] = bytes([lfo_spd, lfo_del, lfo_pmd, lfo_amd])
    packed_data[packed_offset + 4] = (lfo_pms << 4) | (lfo_wave << 1) | lfo_sync
    packed_offset += 5  # 117

    packed_data[packed_offset] = v[IDX_TRANSPOSE]  # Pack Transpose
    packed_offset += 1  # 118

    # Voice Name (VCED Params 145-154 -> Packed 118-127)
    # Write all 10 name chars, including byte 127
    packed_data[packed_offset: packed_offset + 10] = v[IDX_NAME_START: IDX_NAME_START + 10]

    return bytes(packed_data)


# --- Database Functions ---

def connect_to_db(db_path: str) -> Optional[sqlite3.Connection]:
    """Connect to SQLite database and return connection."""
    try:
        conn = sqlite3.connect(db_path)
        return conn
    except sqlite3.Error as e:
        print(f"Error connecting to database: {e}")
        return None


def get_patch_from_db(conn: sqlite3.Connection, patch_id: int) -> Tuple[Optional[int], Optional[str], Optional[bytes]]:
    """Retrieve a patch from the database by ID."""
    try:
        cursor = conn.cursor()
        cursor.execute("SELECT id, patchname, sysex FROM patches WHERE id = ?", (int(patch_id),))
        result = cursor.fetchone()

        if result:
            patch_id, patch_name, sysex_blob = result
            return patch_id, patch_name, sysex_blob
        else:
            return None, None, None
    except sqlite3.Error as e:
        print(f"Database error: {e}")
        return None, None, None


def setup_database(db_path: str) -> Tuple[Optional[sqlite3.Connection], Optional[sqlite3.Cursor]]:
    """Connects to or creates the SQLite DB and ensures the patches table exists."""
    conn = None
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        # Use IF NOT EXISTS for table creation
        cursor.execute('''
        CREATE TABLE IF NOT EXISTS patches (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            patchname TEXT NOT NULL,
            category TEXT DEFAULT '',
            bankfile TEXT NOT NULL,
            comments TEXT DEFAULT '',
            origin TEXT DEFAULT '',
            rating INTEGER DEFAULT NULL,
            hash TEXT UNIQUE NOT NULL,
            sysex BLOB NOT NULL
        )
        ''')
        # Optional: Create an index on hash for faster lookups if needed
        cursor.execute('''
        CREATE INDEX IF NOT EXISTS idx_patch_hash ON patches (hash)
        ''')
        conn.commit()
        print(f"Database setup complete: {db_path}")
        return conn, cursor
    except sqlite3.Error as e:
        print(f"Database Error: {e}", file=sys.stderr)
        if conn:
            conn.rollback()  # Rollback changes if error occurred during setup
            conn.close()
        return None, None


def insert_patch_to_db(cursor: sqlite3.Cursor, patch_name: str, bank_filename: str,
                       patch_hash: str, patch_sysex: bytes, origin: str = '') -> bool:
    """Inserts a patch into the database. Returns True on success, False on failure."""
    try:
        cursor.execute('''
        INSERT INTO patches (patchname, bankfile, hash, sysex, category, origin)
        VALUES (?, ?, ?, ?, '', ?)
        ''', (patch_name, bank_filename, patch_hash, patch_sysex, origin))
        # If execute succeeds without error, the insert was successful
        return True
    except sqlite3.IntegrityError:
        # UNIQUE constraint failed, meaning the hash already exists.
        # Provide a more specific message here:
        print(f"Duplicate ignored: Patch '{patch_name}' (Hash: {patch_hash}) already exists in the database.")
        return False  # Indicate not inserted, but it's not a critical script error
    except sqlite3.Error as e:
        # Handle other potential database errors during insertion
        print(f"DB Insert Error for patch '{patch_name}' (Hash: {patch_hash}): {e}", file=sys.stderr)
        return False  # Indicate failure


# --- Bank Extractor Functions ---

def unpack_byte_11_params(packed_byte: int) -> Tuple[int, int]:
    right_curve = (packed_byte >> 2) & 0x03
    left_curve = packed_byte & 0x03
    return left_curve, right_curve


def unpack_byte_12_params(packed_byte: int) -> Tuple[int, int]:
    detune = (packed_byte >> 3) & 0x0F
    rate_scale = packed_byte & 0x07
    return detune, rate_scale


def unpack_operator_bytes(packed_bytes: bytes) -> bytes:
    if len(packed_bytes) != 17:
        raise ValueError(f"Expected 17 operator bytes, got {len(packed_bytes)}")
    params = bytearray(21)  # Use bytearray for direct assignment
    params[0:11] = packed_bytes[0:11]
    params[11], params[12] = unpack_byte_11_params(packed_bytes[11])  # LCurve, RCurve
    detune, rate_scale = unpack_byte_12_params(packed_bytes[12])
    params[13] = rate_scale  # Rate Scale
    params[14] = packed_bytes[13] & 0x03  # AMS (use 0-3 range)
    params[15] = (packed_bytes[13] >> 3) & 0x07  # KVS
    params[16] = packed_bytes[14]  # Output Level
    params[17] = packed_bytes[15] & 0x01  # Mode
    params[18] = (packed_bytes[15] >> 1) & 0x1F  # Freq Coarse
    params[19] = packed_bytes[16]  # Freq Fine
    params[20] = detune  # Detune
    return bytes(params)  # Return immutable bytes


def unpack_bank_voice_to_single(packed_data: bytes) -> bytes:
    """
    Converts 128-byte packed voice data from bank format to
    155-byte single voice format (VCED Params 0-154).
    """
    if len(packed_data) != 128:
        raise ValueError(f"Expected 128 bytes of packed data, got {len(packed_data)}")

    unpacked = bytearray(155)
    current_index = 0

    # Unpack 6 operators (OP6 down to OP1 -> VCED Params 0-125)
    for op_idx in range(6):
        start_packed = op_idx * 17
        op_bytes_packed = packed_data[start_packed: start_packed + 17]
        op_params_unpacked = unpack_operator_bytes(op_bytes_packed)  # Returns 21 bytes
        unpacked[current_index: current_index + 21] = op_params_unpacked
        current_index += 21  # 21, 42, ..., 126

    # Pitch EG: packed bytes 102-109 -> VCED Params 126-133
    packed_peg_start = 102
    unpacked[current_index: current_index + 8] = packed_data[packed_peg_start: packed_peg_start + 8]
    current_index += 8  # Now 134

    # Algorithm: packed byte 110 -> VCED Param 134
    unpacked[current_index] = packed_data[110] & 0x1F  # ALGO
    current_index += 1  # Now 135

    # Feedback & OSC Sync: packed byte 111 -> VCED Params 135 (FB), 136 (OSync)
    packed_byte_111 = packed_data[111]
    unpacked[current_index] = packed_byte_111 & 0x07  # FB
    current_index += 1  # Now 136
    unpacked[current_index] = (packed_byte_111 >> 3) & 0x01  # OSync
    current_index += 1  # Now 137

    # LFO params: packed bytes 112-116 -> VCED Params 137-143
    unpacked[current_index] = packed_data[112]  # Speed (137)
    current_index += 1
    unpacked[current_index] = packed_data[113]  # Delay (138)
    current_index += 1
    unpacked[current_index] = packed_data[114]  # PMD (139)
    current_index += 1
    unpacked[current_index] = packed_data[115]  # AMD (140)
    current_index += 1
    packed_byte_116 = packed_data[116]
    unpacked[current_index] = packed_byte_116 & 0x01  # Sync (141)
    current_index += 1
    unpacked[current_index] = (packed_byte_116 >> 1) & 0x07  # Wave (142)
    current_index += 1
    unpacked[current_index] = (packed_byte_116 >> 4) & 0x07  # PMS (143)
    current_index += 1  # Now 144

    # Transpose: packed byte 117 -> VCED Param 144
    unpacked[current_index] = packed_data[117] & 0x3F  # Transpose (ensure <= 48?) - mask with 3F just in case
    current_index += 1  # Now 145

    # Voice Name: packed bytes 118-127 -> VCED Params 145-154
    packed_name_bytes = packed_data[118:128]
    unpacked[current_index: current_index + 10] = packed_name_bytes
    current_index += 10  # Now 155

    if current_index != 155:
        raise RuntimeError(f"Internal error: unpack_bank_voice_to_single generated {current_index} bytes, expected 155.")

    return bytes(unpacked)  # Return immutable bytes


def create_single_patch_sysex(packed_data: bytes) -> bytes:
    """
    Creates a valid single-voice SysEx message (163 bytes) from 128-byte packed bank data.
    """
    # Header bytes for single voice dump (Yamaha, Device 0, Format 00, Byte count 155 = 01 1B)
    # F0 43 0n 00 01 1B (n=channel, use 0)
    header = bytes([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B])
    footer_byte = 0xF7

    # Unpack the bank data to 155-byte single voice format (VCED 0-154)
    voice_data_155 = unpack_bank_voice_to_single(packed_data)  # This is the core data

    # Calculate checksum on the 155 bytes of voice data
    checksum_byte = checksum(voice_data_155)

    # Assemble complete message: Header + 155 Data Bytes + Checksum + Footer
    sysex_message = header + voice_data_155 + bytes([checksum_byte, footer_byte])

    if len(sysex_message) != 163:
        raise RuntimeError(f"Internal error: Final SysEx message length is {len(sysex_message)}, expected 163")

    return sysex_message


def generate_patch_report(packed_data: bytes, patch_name_raw: str) -> str:
    # Basic report template - can be expanded as needed
    report = [f"Patch Report: {patch_name_raw}\n"]
    report.append("=" * 50 + "\n")
    try:
        algorithm = packed_data[110] & 0x1F
        feedback = packed_data[111] & 0x07

        report.append(f"Algorithm: {algorithm}")
        report.append(f"Feedback: {feedback}")

        # Output Level for each operator
        for op_idx in range(6):
            start_packed = op_idx * 17
            op_level = packed_data[start_packed + 14]
            report.append(f"OP{6 - op_idx} Level: {op_level}")

        return "\n".join(report)
    except Exception as e:
        report.append(f"\nERROR generating report: {e}")
        return "\n".join(report)


def extract_bank(bankfile: str, output_folder: Optional[str] = None,
                 db_path: Optional[str] = None, generate_report: bool = False,
                 origin: Optional[str] = None) -> bool:
    """Extracts individual patches from a DX7 bank file."""
    try:
        # Read bank file
        with open(bankfile, "rb") as f:
            file_content = f.read()

        print(f"Read {len(file_content)} bytes from '{bankfile}'")

        # Validate bank file
        is_valid, message = is_valid_dx7_bank(file_content)
        print(f"Bank Validation: {message}")
        if not is_valid:
            print("Input file is not a valid DX7 bank file. Aborting.")
            return False

        # Setup database connection if needed
        db_conn = None
        db_cursor = None
        if db_path:
            db_conn, db_cursor = setup_database(db_path)
            if not db_conn or not db_cursor:
                print("Failed to initialize database connection. Aborting DB operations.")
                db_cursor = None  # Ensure cursor is None if setup failed

        # Extract patches
        patch_start_offset = 6  # Data starts after 6 header bytes
        packed_patch_size = 128
        num_patches = 32

        errors = []
        success_count = 0
        db_inserted_count = 0
        files_written_count = 0
        bank_filename_base = os.path.basename(bankfile)  # For DB logging

        # Determine actions needed
        write_files = output_folder is not None
        write_db = db_cursor is not None

        if not write_files and not write_db:
            print("No output specified (--folder or --db). Only bank validation performed.")
            return True  # Validation passed

        if write_files:
            os.makedirs(output_folder, exist_ok=True)
            print(f"Output folder: {output_folder}")
        if write_db:
            print(f"Database mode active.")

        for i in range(num_patches):
            start = patch_start_offset + i * packed_patch_size
            end = start + packed_patch_size
            patch_num = i + 1  # 1-based index for messages

            try:
                # 1. Get Packed Data
                packed_data = file_content[start:end]
                if len(packed_data) != packed_patch_size:
                    errors.append(f"Patch {patch_num}: Incomplete packed data.")
                    continue

                # 2. Unpack to 155-byte voice data
                try:
                    voice_data_155 = unpack_bank_voice_to_single(packed_data)
                except Exception as e:
                    errors.append(f"Patch {patch_num}: Failed during unpacking - {e}")
                    continue

                # 3. Extract Raw Name (for DB)
                patch_name_raw = extract_patch_name_from_unpacked(voice_data_155)

                # 4. Calculate Hash (MD5 of first 145 bytes of unpacked data)
                hash_data = voice_data_155[0:145]
                patch_hash = hashlib.md5(hash_data).hexdigest()

                # 5. Create Full 163-byte SysEx Message (Needed for both DB and file)
                try:
                    patch_syx = create_single_patch_sysex(packed_data)
                    # Internal verification
                    is_valid_internal, msg_internal, _ = verify_single_voice_sysex(patch_syx, f"Internal Patch {patch_num}")
                    if not is_valid_internal:
                        errors.append(f"Patch {patch_num} ({patch_name_raw}): Internal SysEx validation failed - {msg_internal}")
                        continue
                except Exception as e:
                    errors.append(f"Patch {patch_num} ({patch_name_raw}): Failed during SysEx creation - {e}")
                    continue

                # --- Perform Actions ---
                action_success = False
                db_insert_attempted = False
                file_write_attempted = False

                # 6. Insert into Database (if requested)
                if write_db:
                    db_insert_attempted = True
                    patch_name_cooked = apply_db_char_mapping(patch_name_raw)
                    if insert_patch_to_db(db_cursor, patch_name_cooked, bank_filename_base, patch_hash, patch_syx,
                                          origin if origin else ''):
                        db_inserted_count += 1
                        action_success = True  # DB insert counts as success
                    else:
                        # Already logged by insert_patch_to_db
                        pass

                # 7. Write to File (if requested)
                if write_files:
                    file_write_attempted = True
                    # Sanitize name for filename
                    patch_name_for_file = sanitize_filename(patch_name_raw, fallback_name=f"Patch_{patch_num:02}")
                    patch_file_path = os.path.join(output_folder, f"{patch_name_for_file}.syx")
                    report_file_path = os.path.join(output_folder, f"{patch_name_for_file}_report.txt")

                    try:
                        with open(patch_file_path, "wb") as f:
                            f.write(patch_syx)

                        # Verify the written file
                        with open(patch_file_path, "rb") as f:
                            is_valid_file, message_file, _ = verify_single_voice_sysex(f.read(), patch_file_path)

                        if not is_valid_file:
                            errors.append(f"Patch {patch_num} ({patch_name_for_file}): Written file verification failed - {message_file}")
                            try:
                                os.remove(patch_file_path)  # Clean up invalid file
                            except OSError:
                                pass
                        else:
                            files_written_count += 1
                            action_success = True  # File write counts as success

                            # Write report if requested and file write succeeded
                            if generate_report:
                                try:
                                    report_content = generate_patch_report(packed_data, patch_name_raw)
                                    with open(report_file_path, "w", encoding='utf-8') as rf:
                                        rf.write(report_content)
                                except Exception as e:
                                    print(f"Warning: Patch {patch_num} ({patch_name_for_file}): Could not write report - {e}")

                    except IOError as e:
                        errors.append(f"Patch {patch_num} ({patch_name_for_file}): Failed to write file - {e}")
                    except Exception as e:
                        errors.append(f"Patch {patch_num} ({patch_name_for_file}): Error during file write/verify - {e}")

                # --- Update Success Count ---
                if action_success:
                    success_count += 1
                    status_msg = []
                    if db_insert_attempted and db_inserted_count > 0:
                        status_msg.append("DB OK")
                    if file_write_attempted and files_written_count > 0:
                        status_msg.append("File OK")
                    print(f"Processed Patch {patch_num:02} ('{patch_name_raw}'): {' / '.join(status_msg)}")
                elif not action_success and (db_insert_attempted or file_write_attempted):
                    if not any(f"Patch {patch_num}" in err for err in errors):
                        errors.append(f"Patch {patch_num} ({patch_name_raw}): Failed both DB insert and file write.")

            except Exception as e:
                errors.append(f"Patch {patch_num}: UNEXPECTED error during processing - {e}")
                traceback.print_exc()
                continue

        # --- Commit DB changes ---
        if db_conn:
            try:
                db_conn.commit()
            except sqlite3.Error as e:
                print(f"Error committing database changes: {e}")
                errors.append(f"Database commit error: {e}")
            finally:
                db_conn.close()
                print("Database connection closed.")

        # --- Final Summary ---
        print("-" * 50)
        print(f"Processing complete.")
        if write_files:
            print(f"Files written: {files_written_count} / {num_patches}")
        if write_db:
            print(f"Database records inserted: {db_inserted_count} / {num_patches} (Duplicates skipped)")

        print(f"Overall successful patches (DB insert or File write): {success_count} / {num_patches}")

        if errors:
            print("\nErrors/Warnings encountered:")
            for error in errors:
                print(f"- {error}")
        print("-" * 50)

        # Return success if at least one action worked per patch (if actions were requested)
        if not write_files and not write_db:
            return True  # Only validation was done
        else:
            return success_count > 0  # At least one success

    except Exception as e:
        print(f"Extraction failed: {e}")
        traceback.print_exc()
        return False


# --- Main Processing Functions ---

def create_bank(bankfile: str, patchfiles: Optional[str] = None,
                db_path: Optional[str] = None, patchids: Optional[str] = None) -> bool:
    """Creates a DX7 bank file from individual patch files and/or database patches."""
    output_filename = bankfile
    all_packed_patches = []

    # Counters for reporting
    file_patches_count = 0
    db_patches_count = 0

    print(f"Creating bank file: {output_filename}")

    # 1. Process patches from files if specified
    if patchfiles:
        file_patches = patchfiles.split(',')
        file_patches = [f.strip() for f in file_patches if f.strip()]  # Remove empty entries

        print(f"\nProcessing {len(file_patches)} patch file(s)...")
        for i, filename in enumerate(file_patches):
            print(f"  Processing file patch: {filename}")
            try:
                with open(filename, "rb") as f:
                    sysex_data = f.read()

                is_valid, message, voice_data_155 = verify_single_voice_sysex(sysex_data, filename)
                if not is_valid:
                    print(f"    ERROR: {message}. Skipping.")
                    continue

                patch_name = extract_patch_name_from_sysex(sysex_data)
                packed_data_128 = pack_single_to_bank_voice(voice_data_155)
                all_packed_patches.append(packed_data_128)
                file_patches_count += 1
                print(f"    Added patch '{patch_name}' from file")

            except FileNotFoundError:
                print(f"    ERROR: File not found: {filename}.")
            except ValueError as e:
                print(f"    ERROR: Invalid data processing {filename}: {e}")
            except Exception as e:
                print(f"    ERROR: Unexpected error processing {filename}: {e}")
                traceback.print_exc()

    # 2. Process patches from database if specified
    if db_path and patchids:
        patch_ids = patchids.split(',')
        patch_ids = [id.strip() for id in patch_ids if id.strip()]  # Remove empty entries

        conn = connect_to_db(db_path)
        if not conn:
            print("Failed to connect to database. Aborting database operations.")
        else:
            try:
                print(f"\nRetrieving {len(patch_ids)} patch(es) from database...")
                for patch_id in patch_ids:
                    try:
                        patch_id_int = int(patch_id)
                        db_id, patch_name, sysex_blob = get_patch_from_db(conn, patch_id_int)

                        if sysex_blob is None:
                            print(f"    ERROR: Patch ID {patch_id} not found in database.")
                            continue

                        # Convert BLOB to bytes if needed
                        if not isinstance(sysex_blob, bytes):
                            sysex_blob = bytes(sysex_blob)

                        is_valid, message, voice_data_155 = verify_single_voice_sysex(sysex_blob, f"DB ID:{patch_id}")
                        if not is_valid:
                            print(f"    ERROR: Database patch ID {patch_id}: {message}. Skipping.")
                            continue

                        packed_data_128 = pack_single_to_bank_voice(voice_data_155)
                        all_packed_patches.append(packed_data_128)
                        db_patches_count += 1
                        print(f"    Added patch '{patch_name}' (ID: {db_id}) from database")

                    except ValueError:
                        print(f"    ERROR: Invalid patch ID '{patch_id}'. Must be an integer.")
                    except Exception as e:
                        print(f"    ERROR: Failed to process database patch ID {patch_id}: {e}")
                        traceback.print_exc()
            finally:
                conn.close()

    # 3. Check if we have patches and if we're within the 32 patch limit
    total_patches = len(all_packed_patches)
    if total_patches == 0:
        print("\nERROR: No valid patches were processed. Bank file not created.")
        return False

    if total_patches > 32:
        print(f"\nERROR: Too many patches ({total_patches}). DX7 bank is limited to 32 patches.")
        print("Bank file creation aborted.")
        return False

    # 4. Add INIT patches to fill the bank if needed
    num_init_patches = 32 - total_patches
    if num_init_patches > 0:
        print(f"\nPreparing {num_init_patches} INIT patch(es) to complete the bank...")
        try:
            init_packed_128_template = bytearray(pack_single_to_bank_voice(DEFAULT_INIT_VOICE_155))

            for i in range(num_init_patches):
                patch_num = total_patches + i + 1
                init_name_str = f"INIT {patch_num:02d}".ljust(10)  # Ensure 10 chars, ends with space
                init_name_bytes = init_name_str.encode('ascii')

                current_init_packed = bytearray(init_packed_128_template)
                # Write all 10 name chars (118-127)
                current_init_packed[118:128] = init_name_bytes

                all_packed_patches.append(bytes(current_init_packed))
            print(f"Successfully added {num_init_patches} INIT patches.")
        except Exception as e:
            print(f"    ERROR: Failed to prepare INIT patches: {e}")
            traceback.print_exc()
            print(">>> Bank file creation aborted <<<")
            return False

    # 5. Assemble Bank File Data
    print("\nAssembling bank file data...")
    if len(all_packed_patches) != 32:
        print(f"ERROR: Expected 32 patches, found {len(all_packed_patches)}.")
        return False

    bank_data_4096 = b"".join(all_packed_patches)
    if len(bank_data_4096) != 4096:
        print(f"ERROR: Concatenated data length {len(bank_data_4096)} != 4096.")
        return False

    bank_checksum = checksum(bank_data_4096)
    print(f"Calculated checksum for 4096 data bytes: 0x{bank_checksum:02X}")

    bank_header = bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])
    bank_footer = bytes([bank_checksum, 0xF7])
    final_sysex_bank = bank_header + bank_data_4096 + bank_footer

    expected_bank_size = 4104
    if len(final_sysex_bank) != expected_bank_size:
        print(f"ERROR: Final bank size {len(final_sysex_bank)} != {expected_bank_size}.")
        return False

    print("Bank data assembled.")

    # 6. Write Output File
    print(f"\nWriting bank file to: {output_filename}")
    try:
        with open(output_filename, "wb") as f:
            f.write(final_sysex_bank)
        print("Bank file successfully written.")
    except IOError as e:
        print(f"ERROR: Failed to write file '{output_filename}': {e}")
        return False
    except Exception as e:
        print(f"ERROR: Unexpected error writing file: {e}")
        traceback.print_exc()
        return False

    # 7. Final Report
    print("\nBank creation summary:")
    print(f"  Total patches in bank: 32")
    print(f"  - Patches from files: {file_patches_count}")
    print(f"  - Patches from database: {db_patches_count}")
    print(f"  - INIT patches added: {num_init_patches}")
    print("\nBank creation process completed.")
    return True