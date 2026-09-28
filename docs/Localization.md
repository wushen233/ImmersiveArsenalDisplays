# IAD UI Localization

UI translations are external JSON resources under:

`Data/F4SE/Plugins/ImmersiveArsenalDisplays/Localization/`

The file name without `.json` is the language ID saved in
`ImmersiveArsenalDisplays.ini`. The top-level `name` is shown in the language
menu. Translatable UI text is stored under the `strings` object. Menu/window
text uses named keys such as `menu.file`; the rest of the UI uses
`literal.<source text>` entries resolved by `TextLiteral(...)`. The source
literal remains the final fallback, so an older or partial language file is
still usable.

To add a language, copy an existing JSON file, rename it to the new locale ID,
translate its `name` and `strings` values, and place it in the same directory.
The top-bar language menu discovers valid JSON files automatically. Keep the
`###IAD...` suffixes on window titles because they preserve ImGui window IDs and
layout when the displayed language changes.

Missing named keys fall back to `en_US.json`; missing literal keys fall back to
the source text. Keep `##` widget ID suffixes unchanged when translating a
label, and keep NUL separators in Combo item lists.
