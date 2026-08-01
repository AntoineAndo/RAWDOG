# RAWDOG — Claude instructions

## Comment style

- Only comment the WHY, not the WHAT — skip comments that just restate what the
  code obviously does (e.g. spacing/sizing tweaks like "a touch taller so it has
  breathing room").
- Describe current behavior and reasoning only. Don't write comments that
  compare to a previous implementation, mention what something is "rather than"
  or "instead of" doing, or reference other components purely for historical/
  stylistic justification (e.g. "same treatment as X, for consistency") unless
  that relationship is load-bearing for correctness.
- If a comment wouldn't confuse a future reader when removed, don't write it.
