
**Yamaha TX802: Tone Generator (TG) Linking and Unlinking Behavior**

    Sysex:
    F0 43 10 1A 07 <TG> <Value> F7

    Where:
    TG = Tone Generator 0-7
    Value = 0 or <self>

    Example:
    F0 43 10 1A 02 02 F7    - Unlinks/Switches on TG2 
    F0 43 10 1A 02 00 F7    - Links/Switches off TG2
    ...
    F0 43 10 1A 07 07 F7    - Unlinks/Switches on TG8 
    F0 43 10 1A 07 00 F7    - Links/Switches off TG8

    Caveats:
    - TG1 values are accepted, but have no function, since TG1 is the anchor TG and can't be linked.
    - Setting a VNUM (a Patch) for a linked TG will also unlink the TG.
