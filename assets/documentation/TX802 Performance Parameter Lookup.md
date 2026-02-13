# TX802 Performance Parameters Lookup Table

## Overview
This lookup table maps the TX802 Performance Editor UI elements to their corresponding MIDI System Exclusive message parameters. For each parameter, you'll find its location in the PCED (Performance Edit Buffer) and PMEM (Performance Memory) formats, valid data ranges, and a description of its function.

## Performance Global Parameters

| UI Element | SysEx Parameter | PCED Position | PMEM Format | Data Range | Description |
|------------|----------------|---------------|-------------|------------|-------------|
| Performance Name | PNAM | 96-115 | 64-83 | ASCII | 20-character name of the performance |

## Tone Generator (TG) Parameters
Each of the 8 tone generators (TG1-8) has the following parameters. Replace # with the TG number (1-8).

| UI Element | SysEx Parameter | PCED Position | PMEM Bit Position | Data Range | Description |
|------------|----------------|---------------|-------------------|------------|-------------|
| Voice | VNUM | 16-23 | 8-15 | 0-255 | Voice number (0-63: Internal, 64-127: Cartridge, 128-191: Preset A, 192-255: Preset B) |
| Receive | RXCH | 8-15 | 0-7 | 0-16 | MIDI receive channel (0-15, 16: OMNI) |
| Low | NTMTL | 48-55 | 40-47 | 0-127 | Note limit low (C-2 to G8) |
| High | NTMTH | 56-63 | 48-55 | 0-127 | Note limit high (C-2 to G8) |
| Detune | DETUNE | 32-39 | 32-39 (bits 5-3) | 0-14 | Detune value (7: Center) |
| Shift | NSHFT | 64-71 | 56-63 | 0-48 | Note shift (24: Center, +/- 2 octaves) |
| Volume | OUTVOL | 40-47 | 24-31 | 0-99 | Output volume |
| Output | OUTCH | 32-39 | 32-39 (bits 1-0) | 0-3 | Output assign (0: off, 1: L, 2: R, 3: L+R) |
| Damp | FDAMP | 72-79 | 56-63 | 0-1 | EG forced damp (0: off, 1: on) |
| Link | KASG | 80-87 | 32-39 (bit 2) | 0-1 | Key assign group |
| Alternate | Not documented | Not documented | Not documented | On/Off | Controls voice alternation; likely part of the TX802's voice allocation system, but not directly documented in the SysEx specification |
| MT Key | Not documented | Not documented | Not documented | - | Based on the screenshot showing "Preset 1 Equal Temperament", this appears to display the current microtuning configuration rather than being a directly settable parameter |
| Microtuning Table | MTTNUM | 88-95 | 16-23 | 0-254 | Micro tuning table number |

## SysEx Format Details

### PCED (Performance Edit Buffer) Message Format
```
F0H, 43H, 0nH, 7EH, 01H, 68H, LM--8952PE <PCED data>, sum, F7H
```
- n = device number (0-15)
- Data size = 116 × 2 + 10 = 242 bytes
- Data format = ASCII hexadecimal

### PMEM (Performance Memory) Message Format
```
F0H, 43H, 0nH, 7EH, 01H, 28H, LM--8952PM, <PMEM data>, sum, F7H
```
- n = device number (0-15)
- Data size per performance = 10 + 84 × 2 + 10 = 178 bytes
- Data format = ASCII hexadecimal

## Programming Notes

1. When sending parameter changes to the TX802:
   - Use the group/parameter (g/h/p) values from Tables 3 and 5 in the documentation
   - Group (g) = 6, Subgroup (h) = 2 for all PCED parameters

2. For parameter change messages:
   ```
   F0H, 43H, 0nH, 6nH, ppppppp, ddddddd, F7H
   ```

3. Bit field parameters (like OUTCH and KASG) are packed together in the PMEM format. For example:
   - DETUNE/KASG/OUTCH are all in positions 32-39
   - FDAMP/NSHFT are in positions 56-63

4. For all parameters, data must be sent as 7-bit values (0-127)

5. Performance data is stored in ASCII hexadecimal format, with each byte split into two 4-bit values and converted to ASCII codes '0'-'F'
