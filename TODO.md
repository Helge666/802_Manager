# 802 Manager — TODO (next session)

## GUI Polish

- [ ] Nicer JUCE buttons (replace plain TextButton style throughout)
- [ ] Right-hand vertical patch bank view → horizontal strip above the preset browser
- [ ] Move other UI elements around as needed (review overall layout)
- [ ] Clean up the Settings section in the Right Panel tab (visual tidying)

## Features

- [ ] "Send immediately" option in Settings: automatically send the bank to the device
      as soon as a new patch is selected for a TG (no manual Send button needed)

## MIDI Timing

- [ ] Fine-tune inter-message pauses, particularly during bank send
      (review delays between SysEx chunks and post-send button sequences)

## Known bugs (deferred)

- [ ] Bug 3 — changing MIDI input while forwarding is ON doesn't restart forwarding
      (see `midiInputCombo.onChange` in MainComponent.cpp)
- [ ] Bug 4 — `MidiThru::handleIncomingMidiMessage` forwards SysEx; should filter
      to notes and CC only (see MidiThru.h)
