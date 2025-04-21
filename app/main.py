import gradio as gr
import importlib
import os
import sys

# Ensure the project root is in the import path
project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if project_root not in sys.path:
    sys.path.insert(0, project_root)


# --- Configuration for Tabs ---
# Module Name should be 'tabs.filename' (without .py)
TABS_CONFIG = [
    ("Settings", "app.tabs.settings"),
    ("Patch Browser", "app.tabs.patch_browser"),
    ("Perform Edit", "app.tabs.performance_editor"),
    ("TX802 Panel", "app.tabs.front_panel"),
]

# --- Build the Gradio Interface ---
with gr.Blocks() as tx802_manager:
    gr.Markdown("# YAMAHA TX-802 Performance and Patch Manager")

    # Dynamically create tabs based on the configuration
    for tab_title, module_name in TABS_CONFIG:
        with gr.Tab(tab_title) as tab:
            try:
                # Dynamically import the module for the current tab
                tab_module = importlib.import_module(module_name) # <--- Uses package path

                # Check if the required setup function exists in the module
                if hasattr(tab_module, 'setup_tab'):
                    # Call the setup function to build this tab's interface
                    tab_module.setup_tab()

                    # get_refresh_output() generalizes the Gradio compponent to maintain separation
                    # of concern in tx802_manager.py which is a thin orchestration layer
                    if hasattr(tab_module, 'refresh_tab') and hasattr(tab_module, 'get_refresh_outputs'):
                        refresh_outputs = tab_module.get_refresh_outputs()
                        tab.select(fn=tab_module.refresh_tab, inputs=[], outputs=refresh_outputs)
                        print(f"Added refresh trigger for tab: {tab_title}")

                    print(f"Successfully loaded UI for tab: {tab_title} (from {module_name})")
                else:
                    module_filename = f"{module_name.split('.')[-1]}.py"
                    gr.Markdown(
                        f"**Error:** Could not find the `setup_tab()` function "
                        f"in `{module_filename}` (expected in module `{module_name}`). "
                        f"Please define it."
                    )
                    print(f"Error: setup_tab function missing in {module_name}")

            except ImportError as e:
                # More specific error checking for module not found vs. import errors within the module
                if f"No module named '{module_name}'" in str(e):
                     gr.Markdown(
                         f"**Error:** Could not find the module `{module_name}`. "
                         f"Ensure the file `tabs/{module_name.split('.')[-1]}.py` exists "
                         f"and the `tabs` directory contains an `__init__.py` file."
                     )
                else:
                    gr.Markdown(
                        f"**Error:** An error occurred while importing `{module_name}`. "
                        f"Check the code within `{module_name.split('.')[-1]}.py`. Details: {e}"
                    )
                print(f"Error importing module {module_name}: {e}")

            except Exception as e:
                gr.Markdown(
                    f"**Error:** An unexpected error occurred while setting up the tab "
                    f"'{tab_title}' from `{module_name}`. Details: {e}"
                )
                print(f"Error setting up tab {tab_title} from {module_name}: {e}")

# --- Launch the Application ---
if __name__ == "__main__":
    print("Launching Gradio App...")
    # Ensure the working directory is correct if running from a different location
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    tx802_manager.launch()