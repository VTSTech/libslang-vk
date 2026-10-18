Drop-in sprite art for the test card (optional).

Name your files after the character:

    goku.png   mario.png   luigi.png

Notes:
- Transparent-background PNG works best (alpha is respected).
- Any size; each file is nearest-neighbor scaled to fit a ~112x100 px
  slot on the card (integer factor when upscaling, so pixel art stays
  crisp), bottom-aligned above the name label.
- Remove a file to fall back to the built-in 32x32 pixel map for that
  character.

Regenerate the card after adding files:

    python3 tools/make_testcard.py
