In the code you shared, the “these are blind toggles so we cannot track them” claim is already refuted by the program’s own design, because it explicitly establishes a known baseline and then conditionally emits toggles only when a state change is required.

Where your code establishes a known baseline (so tracking is valid)

core::getStartupSequence() in Tx802Utils.cpp builds an indivisible reset-to-known-state macro:

It starts with "RESET" (remote switch code 64).

It waits for boot ("WAIT=3").

It performs deterministic navigation and initialization.

It ends with "TG1" specifically because “Init Performance leaves TG1 always On; toggle Off for known state.”

That last step is important: you are not “guessing” TG1’s state; you are forcing it to a known value as part of the baseline procedure.

Also note the explicit safety comment in that function:

The sequence is only safe/correct when sent to a device that has just been powered on or hardware-reset … must be treated as an indivisible unit starting from RESET.

That is exactly the rule that makes the rest deterministic.

Where your code avoids “blind toggling” in normal operation

MainComponent::sendPerfParam() in MainComponent.cpp has special handling for TG1 because TG1 is a toggle-only control on the device:

For tg1to8 == 1 and paramName == "TG" it reads the current intended state from your config (cfg.tg[0].tgOnOff).

It sends the physical TG1 toggle (sendButtonByName(..., "TG1", ...)) only if the desired state differs from the stored state.

That is the correct pattern for toggle-only hardware: you maintain your own state and only emit a toggle when you need to change that state, which prevents “wrong direction” flips.

Your code also handles TX802-specific side effects deterministically

In Tx802HighLevel::restorePerformanceParams() you already account for a particularly tricky TX802 behavior:

Sending VNUM (“preset select”) turns TG on as a side effect, lighting the LED even if TG should be off.

You then send a second pass of “LINK OFF” to re-silence TGs that should be off.

TG1 is handled last because linking TG2–TG8 can force TG1 on; you then toggle TG1 after the other operations.

This is precisely the kind of “local deterministic state machine” logic that contradicts the idea that state cannot be tracked.

The real constraint (and the only place “blind” becomes true)

Your system stops being deterministic only if this assumption is violated:

The TX802 is not in the exact post-reset baseline state when you start sending navigation/button macros, or

Something else changes state (human button presses, another MIDI source, dropped/duplicated messages).

Your code already documents this risk in getStartupSequence(); that documentation is more accurate than Claude’s conclusion.

One concrete hardening step that matches your existing architecture

Add a concept of “sync validity” to avoid using toggle operations unless you have established baseline:

After sendStartupSequence(), set a flag like tx802Synced = true in your app state (or persist a timestamp in config).

Before any operation that relies on toggle direction (TG1 in particular), require tx802Synced == true; otherwise force running the startup sequence first (or disable TG1 toggling in UI until sync).

This keeps the logic honest: toggles are fully trackable when synced, and treated as unsafe when not synced.

The code you posted already implements the correct deterministic approach; the only remaining engineering risk is enforcing the “only after RESET baseline” precondition everywhere you rely on toggle direction.

Yes, and this is what Claude Code said to me, verbatim:

> ● You're right, I'll be brief. The problem is simple: code 89 is a blind toggle with no state, and we keep sending it on every On click, losing track of the
   actual device state.

  Simplest honest fix: revert the code 89 send entirely. Accept that when TG2–8 is turned On, TG1 will be On too — it's a device constraint. Update TG1's
  config and LED to On to reflect reality. Shall I do that?
Nachgedacht für 9s

Claude’s diagnosis mixes one correct observation with an incorrect conclusion.

What is correct in that quote

If you are sending code 89 (TG1) “on every On click” (i.e., without checking whether TG1 actually needs to change), then yes: you can desynchronize your internal model from the device even in a deterministic setup, because you are applying a toggle event repeatedly instead of only on transitions.

That is an application bug (“toggle spam”), not an epistemic impossibility (“cannot track state”).

What is incorrect in that quote

“code 89 is a blind toggle with no state … simplest honest fix: revert the code 89 send entirely … accept TG1 will be On too … update config/LED to On”

This is wrong under your stated operating model (baseline + deterministic transitions), and it also throws away functionality you already need (you explicitly handle TX802 side effects and restore TG states afterward).

A blind toggle is only “untrackable” if starting state is unknown or external actors can change it without you knowing. Your approach establishes a known baseline and then drives the UI deterministically, so it is trackable.

The correct fix pattern

Stop sending TG1 toggle unless a state change is required.
For a toggle-only control, the invariant should be:

Only send toggle when desired != predicted.

After sending, set predicted = desired (or flip predicted if you model at the event level).

Model device side effects explicitly, rather than giving up.
If TX802 actions force TG1 on (e.g., linking TG2–TG8, preset selection side effects), then your state model must include:

After performing an action known to force TG1 on, set predictedTG1 = On immediately.

If the user’s desired TG1 is Off, then send TG1 toggle once at the end (when it’s safe) to restore Off.

Encode the “TG1 depends on TG2–TG8” constraint as a rule, not a surrender.
If hardware reality is: any(TG2..TG8)==On => TG1==On, then the UI/app can remain honest and still deterministic:

When turning any of TG2–TG8 On, automatically set desiredTG1 = On (and reflect that in UI/config), because TG1 cannot remain Off in that state anyway.

When turning all of TG2–TG8 Off, allow desiredTG1 to be Off or On and enforce it.

Optionally disable TG1-Off control while any TG2–TG8 are On, to prevent impossible targets.

This is “honest” because you are representing the constraint, not pretending you can’t track state.

Concrete logic sketch (language-agnostic)
onUserSetTGk(k>=2, desiredOn):
    set desiredTG[k] = desiredOn
    if desiredOn == On:
        desiredTG1 = On   // hardware constraint
    applyPlan()

onUserSetTG1(desiredOn):
    if any(desiredTG[2..8] == On) and desiredOn == Off:
        reject/disable (or coerce to On)
    else:
        desiredTG1 = desiredOn
    applyPlan()

applyPlan():
    // perform operations that may force TG1 On
    doLinkingOrPresetOps()
    predictedTG1 = On if those ops force it

    // restore TG2..TG8 to desired
    for k in 2..8:
        if desiredTG[k] != predictedTG[k]:
            sendToggle(k)
            predictedTG[k] = desiredTG[k]

    // restore TG1 last
    if desiredTG1 != predictedTG1:
        sendToggle(1)
        predictedTG1 = desiredTG1