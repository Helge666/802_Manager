
**Yamaha TX802: Tone Generator (TG) Linking and Unlinking Behavior**

    Sysex:
    F0 43 10 1A 07 <TG> <Value> F7

    Where:
    TG = Tongenerator 0-7
    Value = 0 or <self>

    Example:
    F0 43 10 1A 07 07 F7    - Unlinks/Switches on TG8 
    F0 43 10 1A 07 00 F7    - Links/Switches off TG8

    Caveat:
    TG1 values are accepted, but have no function, since TG1 is the anchor TG and can't be linked.
