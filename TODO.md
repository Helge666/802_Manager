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

- [~] DEFERRED — spec needs to be written more precisely before implementation.
      Behaviour notes so far:
      - TG LED is ON when TG is on; OFF (dark) when TG is off.
      - On Note On for a TG's receive channel: LED briefly goes OFF (dark), then returns ON.
        Duration of dark period: ~5–10 ms (needs calibration after implementation).
      - Note Off and other events do NOT trigger the flicker.
      - TG must be ON for the LED to react; off-TGs never flicker.
      - Chords / rapid Note Ons: device does not stack flickers — LED stays dark for one
        cooloff period, ignores further Note Ons until cooloff expires (cooloff is not extended).
      - Omni-mode TGs react to Note Ons on any channel.

## MIDI Aftertouch to Breath

- [?] Aftertouch to Breath (CC2) — implemented (Channel AT + Poly AT → CC2, checkbox in MIDI SETUP).
      Unverified: UB-Xa AT output and TX802 Breath response need device-level investigation.

## MIDI Timing

- [?] Chord note jitter during MidiThru forwarding — notes in a chord arrive at the TX802
      with indeterministic timing, subtly changing the phrasing. Investigated: not caused by
      our MidiThru filter changes (baseline passthrough test shows the same behaviour).
      Likely inherent to USB→Computer→USB routing (two USB frame hops + thread scheduling).
      Needs further investigation; may require message batching or a different approach.
- [ ] Reduce startup sequence wait times as much as safely possible
      — review WAIT=3 (device boot), inter-button delays (100ms), inter-chunk delays
      — goal: shortest reliable timings without corrupting device state
- [ ] Fine-tune inter-message pauses during bank send
      (review SysEx chunk size, inter-chunk delay, post-send sequences)

## Compiler deprecation warnings (clean up)

- [ ] `DragAndDropContainer::startDragging` — use the overload that takes an image scale factor
      (see `BankSlotComponent::mouseDrag` in MainComponent.cpp)
- [ ] `MidiOutput::getDevices()` — replace with `MidiOutput::getAvailableDevices()`
      (see `rebuildMidiOutputs()` in MainComponent.cpp)

## Known bugs (deferred)

- [x] Bug 3 — changing MIDI input while forwarding is ON doesn't restart forwarding
      (see `midiInputCombo.onChange` in MainComponent.cpp)
- [x] Bug 4 — `MidiThru::handleIncomingMidiMessage` forwards SysEx; should filter
      to notes and CC only (see MidiThru.h)
- [x] Bug 5 — changing "MIDI to TX802" (output port) incorrectly triggers the device
      startup/init sequence; it should only reopen the MIDI output port.
- [x] Bug 6 — changing "MIDI from Keyboard or DAW" (input port) does not activate the
      newly selected input; the previously selected port keeps receiving.
