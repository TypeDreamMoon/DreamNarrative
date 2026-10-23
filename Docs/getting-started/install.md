# Installation

1. Put the plugin in your project's `Plugins/DreamNarrative` directory (UE 5.8; the stock engine is sufficient).
   Required engine plugins are EnhancedInput, Niagara and GameplayTags. GameplayAbilities is optional;
   only `DreamNarrativeGAS` uses it.
2. Open Project Settings → Plugins → Dream Narrative:
   - `Source Directory`: the directory for `.dna` files relative to the project root; default `DNarrative`.
   - `Asset Directory`: where compiled script assets go; default `/Game/Narrative`.
   - Stage settings: asset roots for name lookup, the post-process material for masks, the default
     brightness for `ramp dark`, and the watchdog grace period in seconds.
3. Saving a `.dna` file while the editor is open automatically rebuilds it. If several editors are open,
   only the first writes assets; failed compilation does not write them. With the editor closed, run
   `Tools/dnc.ps1 build`. Use `dnc.ps1 lint` to check without writing assets.
