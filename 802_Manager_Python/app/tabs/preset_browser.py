import gradio as gr
import threading
import tempfile
import random
import sqlite3
import math
import os

from app.state import midi_output, PRESET_BANK, update_preset_bank
from core.tx802_utils import send_preset_to_buffer, send_bank, edit_performance
from core.dx7_utils import get_preset_from_db, connect_to_db, create_bank

# Semaphore to prevent concurrent MIDI operations
midi_lock = threading.Semaphore(1)

# --- Configuration ---
DB_FILE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "config", "dx_preset_library.sqlite3"))
DEFAULT_PAGE_SIZE = 100
RATING_CHOICES = ["Any", "Unrated"] + list(range(1, 11))
COLUMNS_TO_FILTER_TEXT = ["presetname", "category", "bankfile", "comments", "origin"]
# --- Column Config: order & visibility ---

COLUMN_CONFIG = [
    {"id": "id",         "label": "ID",       "width": 40},
    {"id": "presetname",  "label": "Preset",    "width": 90},
    {"id": "category",   "label": "Category", "width": 70},
    # {"id": "bankfile",   "label": "File",     "width": 80},
    {"id": "comments",   "label": "Comments", "width": 160},
    # {"id": "origin",     "label": "Origin",   "width": 80},
    {"id": "rating",     "label": "❤️",       "width": 30},
]

COLUMNS_TO_DISPLAY = [col["id"] for col in COLUMN_CONFIG]
COLUMN_HEADERS = [col["label"] for col in COLUMN_CONFIG]
COLUMN_WIDTHS = [col["width"] for col in COLUMN_CONFIG]


def setup_tab():
    # --- Database Interaction Logic ---
    def connect_db():
        if not os.path.exists(DB_FILE):
            raise FileNotFoundError(f"Database file not found: {DB_FILE}")
        conn = sqlite3.connect(DB_FILE)
        return conn

    def fetch_data(filter_text, filter_rating, page_num, page_size):
        conn = None
        try:
            conn = connect_db()
            cursor = conn.cursor()

            base_query = f"SELECT {', '.join(COLUMNS_TO_DISPLAY)} FROM presets"
            count_query = "SELECT COUNT(*) FROM presets"
            where_clauses = []
            params = []

            if filter_text:
                text_filter_clauses = [f"{col} LIKE ?" for col in COLUMNS_TO_FILTER_TEXT]
                where_clauses.append(f"({' OR '.join(text_filter_clauses)})")
                params.extend([f"%{filter_text}%"] * len(COLUMNS_TO_FILTER_TEXT))

            if filter_rating != "Any":
                if filter_rating == "Unrated":
                    where_clauses.append("rating IS NULL")
                else:
                    try:
                        rating_val = int(filter_rating)
                        if 1 <= rating_val <= 10:
                            where_clauses.append("rating = ?")
                            params.append(rating_val)
                    except ValueError:
                        pass

            if where_clauses:
                query_suffix = " WHERE " + " AND ".join(where_clauses)
                base_query += query_suffix
                count_query += query_suffix

            cursor.execute(count_query, params)
            total_rows = cursor.fetchone()[0]
            total_pages = max(1, math.ceil(total_rows / page_size))

            page_num = max(1, min(page_num, total_pages))
            offset = (page_num - 1) * page_size

            base_query += " LIMIT ? OFFSET ?"
            params.extend([page_size, offset])

            cursor.execute(base_query, params)
            data = cursor.fetchall()

            return data, COLUMNS_TO_DISPLAY, page_num, total_pages, total_rows

        except (sqlite3.Error, FileNotFoundError) as e:
            print(f"Error during database fetch: {e}")
            return [], COLUMNS_TO_DISPLAY, 1, 1, 0
        finally:
            if conn:
                conn.close()

    # --- Gradio UI ---
    # keep your State objects up here
    current_page      = gr.State(1)
    total_pages       = gr.State(1)
    current_page_size = gr.State(DEFAULT_PAGE_SIZE)
    last_selected_preset = gr.State((None, None))
    preset_bank_internal = gr.State([(None, "Init")] * 32)

    # Row 1: Titles
    with gr.Row():
        with gr.Column(scale=6):
            gr.Markdown("# Preset Database Browser")
        with gr.Column(scale=4):
            gr.Markdown("# Construct Preset Bank")

    # Row 2: Filters  |  For Future Use
    with gr.Row():
        with gr.Column(scale=6):
            with gr.Row():
                filter_text_input   = gr.Textbox(label="Filter by Text", interactive=True)
                filter_rating_input = gr.Dropdown(label="Filter by Rating",
                                                 choices=RATING_CHOICES,
                                                 value="Any",
                                                 interactive=True)
                page_size_input     = gr.Number(label="Items per Page",
                                                value=DEFAULT_PAGE_SIZE,
                                                minimum=1,
                                                step=10,
                                                precision=0,
                                                interactive=True)
        with gr.Column(scale=4):
            gr.Text(value="This bank will be used in the Perform Edit tab.\n"
                          "- Click slot to place current preset\n"
                          "- Randomize populates bank with 32 presets from selection\n"
                          "- Send Bank to device directly",
                    container=False,
                    show_label=False,
                    lines=4)

    # Row 3: Navigation buttons  |  Add‑to‑Pos + slot dropdown
    with gr.Row():
        with gr.Column(scale=6):
            with gr.Row():
                first_btn = gr.Button("First", scale=1)
                prev_btn  = gr.Button("Prev",  scale=1)
                next_btn  = gr.Button("Next",  scale=1)
                last_btn  = gr.Button("Last",  scale=1)
        with gr.Column(scale=4):
            with gr.Row():
                init_bank_btn = gr.Button("Init Bank", scale=0)
                randomize_bank_btn = gr.Button("Randomize Bank", scale=0)

    # Row 4: Status text  |  Surprise Me!
    with gr.Row(equal_height=True):
        with gr.Column(scale=6):
            status_text = gr.Text(label="→ Page: 1 / 1",
                                  value="Loading...",
                                  interactive=False)
        with gr.Column(scale=4):
            with gr.Row():
                send_bank_btn = gr.Button("Send to TX802", scale=0)

    # Row 5: Main DataFrame  |  Preset‑bank DataFrame
    with gr.Row():
        with gr.Column(scale=6):
            initial_data, _, _, total_pages.value, initial_total_rows = fetch_data("", "Any", 1, DEFAULT_PAGE_SIZE)
            output_dataframe = gr.DataFrame(
                value=initial_data,
                headers=COLUMN_HEADERS,
                datatype="str",
                row_count=(10, "dynamic"),
                col_count=(len(COLUMNS_TO_DISPLAY), "fixed"),
                column_widths=COLUMN_WIDTHS,
                max_height=750,
                interactive=False,
            )
        with gr.Column(scale=4):
            preset_bank_display = gr.Dataframe(
                value=[[i + 1, "Init"] for i in range(32)],
                headers=["Slot", "Preset Name"],
                datatype=["number", "str"],
                col_count=(2, "fixed"),
                row_count=(32, "fixed"),
                max_height=750,
                column_widths=[15, 90],
                interactive=False,
                show_row_numbers=False
            )

    def update_view(text_filter, rating_filter, page_size, requested_page):
        page_size = int(page_size) if page_size and page_size >= 1 else DEFAULT_PAGE_SIZE
        data, headers, current_pg, total_pgs, total_rows = fetch_data(
            text_filter, rating_filter, requested_page, page_size
        )
        status_message = gr.Text(label=f"→ Page: {current_pg} / {total_pgs}", value=f"Found {total_rows} matching presets.", interactive=False)
        can_go_prev = current_pg > 1
        can_go_next = current_pg < total_pgs

        return (
            data, status_message,
            current_pg, total_pgs, page_size,
            gr.Button(interactive=can_go_prev),
            gr.Button(interactive=can_go_prev),
            gr.Button(interactive=can_go_next),
            gr.Button(interactive=can_go_next)
        )

    def handle_filter_or_size_change(text_filter, rating_filter, page_size):
        return update_view(text_filter, rating_filter, page_size, 1)

    def handle_navigation(text_filter, rating_filter, page_size, current_pg, total_pgs, action):
        if action == "first":
            requested_page = 1
        elif action == "prev":
            requested_page = max(1, current_pg - 1)
        elif action == "next":
            requested_page = min(total_pgs, current_pg + 1)
        elif action == "last":
            requested_page = total_pgs
        else:
            requested_page = current_pg
        return update_view(text_filter, rating_filter, page_size, requested_page)

    def handle_row_select(evt: gr.SelectData, current_data):
        if evt.index is None or current_data is None or current_data.empty:
            return "Status: Select a row from the data above.", (None, None)
        selected_row_index = evt.index[0]
        try:
            selected_row = current_data.iloc[selected_row_index]
            preset_id = selected_row.iloc[0]
            preset_name = selected_row.iloc[1]
            conn = connect_to_db(DB_FILE)
            _, _, sysex_data = get_preset_from_db(conn, preset_id)
            send_preset_to_buffer(sysex_data, 1, midi_output)
            return f"Selected: ID {preset_id} - {preset_name}", (preset_id, preset_name)
        except Exception as e:
            return f"Status: Error processing selection ({type(e).__name__}).", (None, None)

    def handle_bank_click(evt: gr.SelectData, last_preset, bank_internal):
        if evt.index is None or last_preset[0] is None:
            return gr.update(), bank_internal
        row_idx = evt.index[0]
        preset_id, preset_name = last_preset

        # Update internal list
        bank_internal[row_idx] = (preset_id, preset_name)
        update_preset_bank(row_idx, preset_name)

        # Prepare visible table (slot number + preset names)
        display_data = [[i + 1, name] for i, (_, name) in enumerate(bank_internal)]

        # ✅ Important: wrap in gr.update to apply to Dataframe
        return gr.update(value=display_data), bank_internal

    def handle_init_bank():
        initial_bank = [(None, "Init")] * 32
        visible = [[i + 1, "Init"] for i in range(32)]
        for i in range(32):
            update_preset_bank(i, "Init")
        return gr.update(value=visible), initial_bank

    def handle_randomize_bank(filter_text, filter_rating):
        conn = None
        try:
            conn = connect_db()
            cursor = conn.cursor()

            base_query = f"SELECT id, presetname FROM presets"
            where_clauses = []
            params = []

            if filter_text:
                text_filter_clauses = [f"{col} LIKE ?" for col in COLUMNS_TO_FILTER_TEXT]
                where_clauses.append(f"({' OR '.join(text_filter_clauses)})")
                params.extend([f"%{filter_text}%"] * len(COLUMNS_TO_FILTER_TEXT))

            if filter_rating != "Any":
                if filter_rating == "Unrated":
                    where_clauses.append("rating IS NULL")
                else:
                    try:
                        rating_val = int(filter_rating)
                        if 1 <= rating_val <= 10:
                            where_clauses.append("rating = ?")
                            params.append(rating_val)
                    except ValueError:
                        pass

            if where_clauses:
                base_query += " WHERE " + " AND ".join(where_clauses)

            cursor.execute(base_query, params)
            all_matches = cursor.fetchall()

            if not all_matches:
                return (
                    gr.skip(),  # Leave preset_bank_display unchanged
                    gr.skip(),  # Leave preset_bank_internal unchanged
                    "No presets available to randomize from."
                )

            selected = random.sample(all_matches, min(32, len(all_matches)))
            init_slots = 32 - len(selected)
            bank_entries = selected + [(None, "Init")] * init_slots
            for i, (preset_id, preset_name) in enumerate(bank_entries):
                update_preset_bank(i, preset_name)
            display_rows = [[i + 1, name] for i, (_, name) in enumerate(bank_entries)]

            return (
                gr.update(value=display_rows),
                bank_entries,
                f"Bank randomized: {len(selected)} preset(s), {init_slots} INIT."
            )

        except Exception as e:
            return (
                gr.skip(),
                gr.skip(),
                f"Error during randomization: {type(e).__name__}: {str(e)}"
            )
        finally:
            if conn:
                conn.close()

    def handle_send_to_tx802(bank_internal):
        try:
            # ✅ bank_internal is a plain list of (id, name) tuples
            preset_ids = [str(pid) for pid, _ in bank_internal if pid is not None]

            if not preset_ids:
                return "⚠️ No valid presets in bank. Nothing sent."

            with tempfile.NamedTemporaryFile(delete=False, suffix=".syx") as temp:
                temp_path = temp.name

            success = create_bank(temp_path, presetfiles=None, db_path=DB_FILE, presetids=",".join(preset_ids))

            if not success:
                os.remove(temp_path)
                return "❌ Failed to create preset bank."

            send_success = send_bank(temp_path, device_id=1, output_port=midi_output)

            # os.remove(temp_path)

            return "✅ Sent preset bank to TX802." if send_success else "❌ Failed to send bank."

        except Exception as e:
            return f"❌ Error: {type(e).__name__}: {str(e)}"

    outputs_list = [
        output_dataframe, status_text,
        current_page, total_pages, current_page_size,
        first_btn, prev_btn, next_btn, last_btn
    ]

    filter_text_input.submit(handle_filter_or_size_change, [filter_text_input, filter_rating_input, page_size_input], outputs_list)
    filter_rating_input.change(handle_filter_or_size_change, [filter_text_input, filter_rating_input, page_size_input], outputs_list)
    page_size_input.change(handle_filter_or_size_change, [filter_text_input, filter_rating_input, page_size_input], outputs_list)

    first_btn.click(handle_navigation, [filter_text_input, filter_rating_input, current_page_size, current_page, total_pages, gr.State("first")], outputs_list)
    prev_btn.click(handle_navigation, [filter_text_input, filter_rating_input, current_page_size, current_page, total_pages, gr.State("prev")], outputs_list)
    next_btn.click(handle_navigation, [filter_text_input, filter_rating_input, current_page_size, current_page, total_pages, gr.State("next")], outputs_list)
    last_btn.click(handle_navigation, [filter_text_input, filter_rating_input, current_page_size, current_page, total_pages, gr.State("last")], outputs_list)

    init_bank_btn.click(handle_init_bank, inputs=[], outputs=[preset_bank_display, preset_bank_internal])
    randomize_bank_btn.click(handle_randomize_bank, inputs=[filter_text_input, filter_rating_input], outputs=[preset_bank_display, preset_bank_internal, status_text])
    send_bank_btn.click(
        handle_send_to_tx802,
        inputs=[preset_bank_internal],
        outputs=[status_text]
    )

    output_dataframe.select(handle_row_select, inputs=[output_dataframe], outputs=[status_text, last_selected_preset])
    preset_bank_display.select(handle_bank_click, inputs=[last_selected_preset, preset_bank_internal], outputs=[preset_bank_display, preset_bank_internal])

    status_text.value = f"Found {initial_total_rows} matching presets."


def refresh_tab():
    import app.state as state

    # Set the current tab
    state.set_current_tab("Preset Browser")

    # Initialize button_commands dictionary BEFORE using it
    button_commands = {}

    # Only proceed if we have a valid MIDI output
    if not state.midi_output:
        print("  • No MIDI output configured - skipping TG setup")
        return None

    # 1. First, turn OFF TGs 2-8 if they are ON
    for tg_num in range(2, 9):  # TGs 2-8
        current_state = state.tg_states[tg_num]["TG"]
        if current_state == "On":
            # Don't update state yet - we'll restore it later
            button_commands[f"TG{tg_num}"] = "Off"

    # 2. Reset TG1 to DEFAULT_TG_STATE while preserving TG and PRESET
    current_tg1 = state.tg_states[1]
    # current_tg1_preset = current_tg1["PRESET"]

    for param, default_value in state.DEFAULT_TG_STATE.items():
        # Skip TG (always ON) and PRESET (preserve current value)
        if param in ["TG", "PRESET"]:
            continue

        current_value = current_tg1[param]
        if current_value != default_value:
            button_commands[f"{param}1"] = default_value

    # Send all commands in a single edit_performance call
    if button_commands:
        try:
            edit_performance(
                port=state.midi_output,
                device_id=1,
                delay_after=0.02,
                play_notes=False,
                **button_commands
            )
            # print(f"  • Sent {len(button_commands)} commands to set up preset auditioning mode")
        except Exception as e:
            print(f"  • Error sending commands: {e}")

    return None