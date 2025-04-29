
**Yamaha TX802: Tone Generator (TG) Linking and Unlinking Behavior**

On the Yamaha TX802, managing the active Tone Generators (TGs) is not a simple matter of turning them on or off via muting or volume controls. Instead, deactivating a TG (indicated by its front panel LED turning off) is handled through a specific **TG Linking** mechanism, which also involves **voice reallocation**.

**Mechanism:**

1.  **Control via SysEx:** The actual linking and unlinking is controlled by a specific, largely undocumented System Exclusive (SysEx) message:
    ```
    F0 43 10 1A 07 <TG#> <LinkData> F7
    ```
2.  **Parameters:**
    * `<TG#>`: Specifies the Tone Generator being configured (0 for TG1, 1 for TG2, ..., 7 for TG8).
    * `<LinkData>`: This is a **link index**, not a simple on/off flag. It specifies *which other TG* the current TG (`<TG#>`) will link its voices to when it is deactivated (linked off).
3.  **Linking Direction:** TGs link **leftward**. When a TG is linked off, its voices are reallocated to the next active TG to its left in the chain.
4.  **TG1 Anchor:** TG1 is always active and serves as the base anchor. It **cannot be linked off** or deactivated via this mechanism.
5.  **Behavior:** Deactivating a TG (e.g., TG5) using this SysEx command involves setting its `<LinkData>` to point to the index of the desired active TG to its left (e.g., TG4, or TG1 if TGs 2, 3, and 4 are also linked off). Its LED will turn off, and its polyphony resources are added to the target TG it is linked to. Unlinking involves configuring the TG so it doesn't point its resources to another TG when active.

**Summary Table:**

| Feature          | Description                                                                 |
| :--------------- | :-------------------------------------------------------------------------- |
| **Control** | Specific SysEx command (`F0 43 10 1A 07 <TG#> <LinkData> F7`)              |
| **Action** | Links a TG's resources to another TG when deactivated (LED Off).            |
| **`<LinkData>`** | Specifies the *index* of the TG to link to (leftward).                      |
| **Direction** | TGs link resources to the next active TG to their left.                     |
| **TG1** | Always active, cannot be linked off.                                        |
| **Effect** | Voice reallocation: Linked TG's polyphony is pooled with the target TG.     |
| **Not** | A simple mute, volume control, or on/off toggle per TG.                   |

This mechanism requires careful management of the linkage relationships between TGs rather than treating them as independent units that can be simply switched on or off.
