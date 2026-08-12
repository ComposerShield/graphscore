---
description: Find the next unchecked actionable phase or sub-phase in a milestone checklist.
agent: general
---

Determine the next available checklist item for milestone `$1`.

1. Normalize the argument: accept `m5`, `M5`, `5`, or `05`, and extract the
   numeric milestone value.
2. Read the matching detailed checklist file `docs/plan/<NN>-*.md`, where `<NN>`
   is the milestone number padded to two digits. If the argument is missing,
   invalid, or no matching file exists, say so and show the expected form.
3. Inspect every Markdown checkbox in document order. Find the first unchecked
   actionable item. An unchecked parent checkbox with indented checklist
   children is an aggregate item, so skip it while any child work remains and
   select the first unchecked child instead. A parent with no checklist children
   is actionable.
4. Report the item's complete stable ID, heading/context, and checklist text.
   IDs use the repository convention `M<milestone>-phase-<number>`, with a
   letter suffix for a sub-phase and a Roman-numeral suffix for a deeper
   sub-sub-phase, such as `M2-phase-1a-ii`.
5. If all actionable checklist items are checked, report that the milestone has
   no remaining actionable phases.

Do not modify files, check boxes, or infer work from prose outside checklist
entries. Keep the response concise.
