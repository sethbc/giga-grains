# sst-voicemanager evaluation (read-only; clone at research/svm, HEAD 01b617c, 2026-10-13)
Paths below are include/sst/voicemanager/{voicemanager.h (VM.h), voicemanager_impl.h (impl), voicemanager_constraints.h (C.h), midi1_to_voicemanager.h}.

## 1 API
- VoiceManager<Cfg,Responder,MonoResponder> (VM.h:140). Cfg gives voice_t and constexpr maxVoiceCount (C.h:45). Host supplies Responder: beginVoiceCreationTransaction, initializeMultipleVoices, endVoiceCreationTransaction, terminateVoice, releaseVoice, retriggerVoiceWithNewNoteID, moveVoice, moveAndRetriggerVoice, discardHostVoice, setNoteExpression, poly/mono param modulation, setPolyphonicAftertouch, setVoiceMIDIMPEChannelPitchBend/Pressure/Timbre, setVoiceEndCallback (C.h:49-83). MonoResponder: setMIDIPitchBend, setMIDI1CC, setMIDIChannelPressure (C.h:85-87).
- Inputs: processNoteOnEvent/OffEvent(port,channel,key,noteid,velocity[,retune]), updateSustainPedal, routeMIDIPitchBend, routeMIDI1CC, routeChannelPressure, routePolyphonicAftertouch, routeNoteExpression (VM.h:229-257).
- TIMING: no sample offsets anywhere in the API. Calls are synchronous, "now". Feeding at our grid boundary is possible: call it at each 32-sample boundary for the events quantized there, in group order. Responder gets no time either; we would stamp the boundary ourselves.
- Voice end is async: terminateVoice is a request; the responder must later call the end callback (impl:743-745, set once at construction). Steal counting handles "pending end" (impl:~915-935).

## 2 Real-time safety
- Voice arrays fixed: std::array<VoiceInfo, maxVoiceCount> (impl:117), working buffers std::array (impl:~262-265); scratch uses SmallHashMap/Set inline (impl:~229-233).
- BUT per-port key state is std::unordered_map<int32_t, keyState_t> (impl:236), keyState_t = 16x128 SmallHashMap cells, ~560KB/port (impl:225-228). Groups are std::unordered_map<uint64_t,GroupState> (impl:163). Hash lookups on every event; allocation only in guaranteePort/guaranteeGroup (off-RT, required: VM.h:272-279). Key cell spills to heap if one key is in >4 groups (impl:225-228, "costs one allocation") - for us (4 layers = 4 groups) that is at the inline limit, not over, but a layer-per-group design is exactly 4.
- std::function: setVoiceEndCallback (constructed once, off-RT); allSoundsOffMatching takes std::function by value (VM.h:263, impl:1648) - a capturing lambda can allocate; avoid it on RT.
- Loops: bounded by maxVoiceCount except stealing loops guarded by progress check (impl:450, 953); noteIdStack 256 ring (impl:~58).
- Commit "Make the realtime voice-manager API allocation-free and verify with RTSan (#54)" (2026-06-16); CI has linux clang21 rtsan leg. Needs sst-cpputils (CMake fetches `GIT_TAG main`, unpinned; CMakeLists.txt:18-20). C++20 required (VM.h:313); we are C++23, fine. We would pin both by SHA.
- RTSan-cleanliness of OUR responder is on us.

## 3 Stealing / polyphony
- StealingPriorityMode OLDEST/HIGHEST/LOWEST (VM.h:199). findNextStealableVoiceInfo (impl:318-420): oldest NON-gated voice first, else oldest gated, by voiceCounter. This is our order, EXCEPT sustain-held voices (gatedDueToSustain) are lumped with gated (impl:352) - we would treat sustained-released as released. A mismatch on pedal use; would need a patch or our own selection.
- Steal = responder.terminateVoice(voice) (impl:~895); fade is entirely the responder's (voice may keep sounding while fading, and it later calls end callback). A free slot is only reused after end callback (pendingEnd accounting) - so a fading voice occupies a slot; with a hard cap of 16 our "steal then new voice" needs 17 physical voices or the fade must be done before... Actually the manager counts pending-end voices as freed (impl:~914-935), and placeVoiceFromIndex needs a free voiceInfo slot (impl:~1205-1230) so physical maxVoiceCount must exceed 16 to hold the fading one. Our cap-16 design would set maxVoiceCount=17+ and group limit 16; per-group limits are supported (setPolyphonyGroupLimit VM.h:280, hierarchical groups VM.h:291).
- Multi-voice transactions steal together (impl:~470-480).
- voiceCounter / "ordinal" not exposed; we'd keep our own ordinal in the responder.

## 4 MPE, pedal, note IDs
- MPE: only a dialect flag + one mpeGlobalChannel (VM.h:144-148, 220). No zones, no MCM, no RPN, no per-channel bend ranges (grep: none). Member-channel bend/pressure/CC74 routed per voice by channel (impl:274-300, 1494-1625); bend range scaling is the responder's job. Master channel: bend/pressure go to monoResponder.
- Sustain: CC64 > 64 per channel (MPE: on global channel) (impl:1318, 1391-1400). Pedal-up releases all gatedDueToSustain voices (impl:1425-1450). No sostenuto (grep: none).
- Note IDs: processNoteOff matches by port/channel/key and, if both have an id, by the id stack (impl:63-75). Two notes on one key are two voices in MULTI_VOICE (default) mode; PIANO/MONO_PER_KEY modes optional (VM.h:154-160). With noteid -1 on both, an off matches ALL voices on that (channel,key) (matches() is a loop with no count) - differs from our "(channel,key) with a count" rule: one off would release both. Needs our own pre-resolution into unique ids (we could mint synthetic ids).
- CLAP note expressions: routeNoteExpression (VM.h:249) -> setNoteExpression.

## 5 Usage by SC-XT / Surge
Local clones ~/src/shortcircuit-xt and ~/src/surge do NOT exist on this machine (checked); OB-Xf grep found nothing. Not verified. (Upstream docs: Surge XT and shortcircuit-xt are the known consumers; unverified here.)

## 6 License / history
LICENSE.md is MIT ("Copyright 2023, Paul Walker and others"). HEAD 01b617c, 2026-10-13 ("per-group repeated key mode, mono per key #58"); active (#54-#58 Jun-Oct 2026). Only tag: last_cpp_17 (488bd2f); no releases; pin by SHA. Depends on sst-cpputils (license not checked here).

## Recommendation
Do not adopt it as our voice scheduler; build in-house. Reasons:
- Sample-offset-free, immediate-mode API fits our grid, but the manager's decisions are made inside hash-map-based state keyed by port/channel/key; behavior is deterministic but we cannot change its tie-breaks without patching (sustain voices ranked as held; two notes on (ch,key) without ids both released by one off).
- Our fixed 4x16 voices with ordinal counting, count-based (channel,key) identity, fade-while-sounding steals and MPE-zone/RPN tracking are mostly NOT provided (no zones/RPN/sostenuto), so we write most of the state machine anyway.
- Overhead we dislike: ~560KB/port unordered_map state, std::function surface, `main`-tracking dependency, 17+ physical voices needed to hold fading steals.
- What is reusable as design reference (not code): the oldest-non-gated-then-oldest-gated rule, transaction-grouped steals, pending-end accounting, noteId stack. Revisit only if we want mono/legato/piano modes later.
