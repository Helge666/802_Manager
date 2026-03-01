# 802 Manager — TODO

## Known bugs (fix now)

- [x] Version number in logfile and App Window Title are off by 1. Evaluate and correct.

## Patch browser improvements

- [x] Default display of browser columns: 1=ID, 2=Patch, 3=Rating, 4=Category, 5=Bank File, 6=Origin, 7=Comments
      - default for first app start w/o config. will be written to new config as start values.

## Setting tabs improvement

- [x] In the settings, the path browser config, change from 2-column to 3-column; there's enough space.
      - This will free up some vertical space.
- [x] Add a black gap between each  of the sections, like the one after MACROS.
- [x] Add a checkbox for logging - logfile will be written when checked (True), otherwise (False) not. Default=False.
	  
## Left Panel incoming MIDI LED signal

- [ ] On the device, the LEDs in the TG buttons flicker briefly when a Note On on the Receive Channel of the respective TG comes in.
      - Note Off or other events do not do not trigger the flicker.
	  - Flickering does not last for the duration of the note, only for a very brief moment after Note On (~5ms-15ms.).
	  - Only on TGs that are on.

## MIDI Aftertouch to Breath

- [?] Aftertouch to Breath (CC2) — implemented (Channel AT + Poly AT → CC2, checkbox in MIDI SETUP).
      Unverified: UB-Xa AT output and TX802 Breath response need device-level investigation.

## MIDI Timing

- [ ] Reduce startup sequence wait times as much as safely possible
      — review WAIT=3 (device boot), inter-button delays (100ms), inter-chunk delays
      — goal: shortest reliable timings without corrupting device state
- [ ] Fine-tune inter-message pauses during bank send
      (review SysEx chunk size, inter-chunk delay, post-send sequences)

## Known bugs (deferred)

- [ ] Bug 3 — changing MIDI input while forwarding is ON doesn't restart forwarding
      (see `midiInputCombo.onChange` in MainComponent.cpp)
- [x] Bug 4 — `MidiThru::handleIncomingMidiMessage` forwards SysEx; should filter
      to notes and CC only (see MidiThru.h)
