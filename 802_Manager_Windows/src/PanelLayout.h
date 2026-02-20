#pragma once

// Pixel positions and dimensions for the TX802 panel GUI images.
// All coordinates are in image-space pixels (origin = top-left).
// Edit here if panel images are updated or positions need fine-tuning.

namespace PanelLayout
{
    struct Rect  { int x, y, w, h; };
    struct Point { int x, y; };

    constexpr int kPanelWidth  = 1050;
    constexpr int kPanelHeight = 350;
    constexpr int kLedWidth    = 37;
    constexpr int kLedHeight   = 16;

    // ── Left Panel (TX802-Panel-Left.png) ────────────────────────────────────

    namespace Left
    {
        constexpr Rect displayArea { 316,  54, 671, 72 };
        constexpr Rect buttonPower {  28, 244,  68, 68 };

        // TG1–TG8 buttons (index 0 = TG1)
        constexpr Rect tgButton[8] =
        {
            { 336, 244, 68, 68 }, // TG1
            { 419, 244, 68, 68 }, // TG2
            { 498, 244, 68, 68 }, // TG3
            { 578, 244, 68, 68 }, // TG4
            { 660, 244, 68, 68 }, // TG5
            { 740, 244, 68, 68 }, // TG6
            { 820, 244, 68, 68 }, // TG7
            { 903, 244, 68, 68 }, // TG8
        };

        // LED-On.png position per TG button (index 0 = TG1), size = kLedWidth x kLedHeight
        constexpr Point tgLed[8] =
        {
            { 351, 244 }, // TG1
            { 434, 244 }, // TG2
            { 513, 244 }, // TG3
            { 593, 244 }, // TG4
            { 675, 244 }, // TG5
            { 755, 244 }, // TG6
            { 835, 244 }, // TG7
            { 918, 244 }, // TG8
        };
    }

    // ── Right Panel (TX802-Panel-Right.png) ──────────────────────────────────

    namespace Right
    {
        // Top row — mode select
        constexpr Rect performSelect { 126,  35, 68, 68 };
        constexpr Rect voiceSelect   { 207,  35, 68, 68 };
        constexpr Rect systemSetup   { 288,  35, 68, 68 };
        constexpr Rect utility       { 371,  35, 68, 68 };

        // Second row — edit
        constexpr Rect performEdit   { 126, 107, 68, 68 };
        constexpr Rect voiceEditI    { 207, 107, 68, 68 };
        constexpr Rect voiceEditII   { 288, 107, 68, 68 };
        constexpr Rect storeCompare  { 371, 107, 68, 68 };

        // Cartridge slot
        constexpr Rect cartridgeArea { 135, 237, 290, 68 };

        // Numeric keypad (row 1)
        constexpr Rect num7          { 555,  35, 68, 68 };
        constexpr Rect num8          { 638,  35, 68, 68 };
        constexpr Rect num9          { 719,  35, 68, 68 };
        constexpr Rect minus         { 800,  35, 68, 68 };

        // Numeric keypad (row 2)
        constexpr Rect num4          { 555, 107, 68, 68 };
        constexpr Rect num5          { 638, 107, 68, 68 };
        constexpr Rect num6          { 719, 107, 68, 68 };
        constexpr Rect onYes         { 800, 107, 68, 68 };

        // Numeric keypad (row 3)
        constexpr Rect num1          { 555, 179, 68, 68 };
        constexpr Rect num2          { 638, 179, 68, 68 };
        constexpr Rect num3          { 719, 179, 68, 68 };
        constexpr Rect offNo         { 800, 179, 68, 68 };

        // Numeric keypad (row 4)
        constexpr Rect num0          { 555, 250, 68, 68 };
        constexpr Rect intBtn        { 638, 250, 68, 68 };
        constexpr Rect crtBtn        { 719, 250, 68, 68 };
        constexpr Rect enterSpace    { 800, 250, 68, 68 };

        // Convenience arrays for iteration
        constexpr Rect modeButtons[8] = {
            performSelect, voiceSelect, systemSetup, utility,
            performEdit,   voiceEditI,  voiceEditII, storeCompare
        };
        constexpr Rect numPad[10] = {
            num0, num1, num2, num3, num4, num5, num6, num7, num8, num9
        };
    }

} // namespace PanelLayout
