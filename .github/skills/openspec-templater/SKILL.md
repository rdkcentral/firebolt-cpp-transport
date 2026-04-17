---
name: openspec-templater
description: Update existing Openspec spec files to match the standard spec template format. Ensures consistency and completeness across all specs in a project.
license: MIT
compatibility: Requires a codebase using Openspec with specs in the `openspec/specs/` directory and code referencing specs via comments.
metadata:
  author: user
  version: "1.0"
  generatedBy: "copilot"
---

## Purpose
Help users quickly update existing Openspec spec files to match the standard spec template (`.github/skills/openspec-templater/spec_template.md`). Ensures consistency, completeness, and traceability across all specs in a project.

## When to Use
- When an existing spec does not follow the standard template format.
- When onboarding new specs written without a template.
- When the spec template has been updated and existing specs need to be brought in line.
- When running the `openspec-coverage` skill and spec completeness scores are low.

---

## Workflow

When asked to apply the template to one or more spec files, follow these steps:

### Step 1: Read the Template
Read `.github/skills/openspec-templater/spec_template.md` to understand the required sections:
1. Title (H1)
2. Overview
3. Description
4. Requirements
5. Architecture / Design _(if applicable)_
6. External Interfaces _(if applicable)_
7. Performance _(if applicable)_
8. Security _(if applicable)_
9. Versioning & Compatibility _(if applicable)_
10. Conformance Testing & Validation _(if applicable)_
11. Covered Code
12. Open Queries
13. References
14. Change History

### Step 2: Read the Existing Spec
Read the full content of the existing spec file. Identify which sections are present, missing, or need reorganization.

### Step 3: Map Existing Content to Template Sections
For each section in the template:
- **If the content exists** in the spec (even under a different heading), map and reorganize it into the correct template section.
- **If the section is not applicable** for this spec, add the section heading with the note: `_Not applicable — [brief reason]._`
- **If the content is unclear, incomplete, or ambiguous**, add an entry under `## Open Queries` describing what needs clarification.
- **Never delete existing content** — always preserve and reorganize it.

### Step 4: Ensure Covered Code Section
- If a `## Covered Code` section exists, preserve it as-is.
- If it is missing, add a placeholder:
  ```markdown
  ## Covered Code
  _Not yet mapped. Add file and method references here._
  ```

### Step 5: Add Open Queries
- Surface any ambiguities, incomplete sections, or questions as bullet points under `## Open Queries`.
- If there are none, write: `_No open queries._`

### Step 6: Add Change History Entry
- Append an entry to `## Change History`:
  ```
  - [YYYY-MM-DD] - openspec-templater - Restructured to match spec template.
  ```

### Step 7: Write the Updated Spec
- Rewrite the spec file with all sections in the correct template order.
- Preserve all original content, reorganized as needed.

---

## Rules

- **Preserve all existing content** — do not discard any information from the original spec.
- **Never invent requirements or content** — only reorganize what exists, and flag gaps as open queries.
- **Mark non-applicable sections clearly** — use `_Not applicable — [reason]._` rather than omitting the section.
- **Covered Code is mandatory** — always ensure this section is present.
- **Open Queries is mandatory** — always ensure this section is present, even if empty.
- **Change History is mandatory** — always append an entry.

---

## Example Usage

- "Apply the spec template to `openspec/specs/transport_layer_spec.md`."
- "Update all specs in `openspec/specs/` to match the new template."
- "Reformat `my_feature_spec.md` using the openspec-templater skill."

---

## Template Reference


The canonical template is located at:
```
.github/skills/openspec-templater/spec_template.md
```

Always read the latest version of this file before applying the template to any spec.

---

## Notes
- This skill is complementary to `openspec-coverage` — applying the template improves spec completeness scores.
- When applying to multiple specs, process each spec file individually and sequentially.
- If unsure whether content belongs in a section, err on the side of including it and add an open query.
