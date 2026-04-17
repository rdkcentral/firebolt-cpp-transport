---
name: openspec-coverage
description: Analyze code-to-spec coverage in an Openspec project and generate a compliance score and report.
license: MIT
compatibility: Requires a codebase using Openspec with specs in the `openspec/specs/` directory and code referencing specs via comments.
metadata:
  author: user
  version: "1.0"
  generatedBy: "copilot"
---

### Architecture HLA Specification (10%) Breakdown

1. **Presence of HLA Spec (3%)**
  - Is there a high-level architecture (HLA) specification document or section?
2. **Clarity of Architecture Diagrams (3%)**
  - Are architecture diagrams present and clear?
3. **Component/Module Mapping (2%)**
  - Are all major components/modules mapped in the HLA spec?
4. **Traceability to Code (2%)**
  - Is there traceability from HLA components to code modules?

Each sub-criterion is scored individually and summed for the total 10%.

### External Interface Specification (10%) Breakdown

1. **Presence of Interface Spec (3%)**
  - Is there a dedicated external interface/API specification?
2. **Defined Inputs/Outputs (3%)**
  - Are all inputs, outputs, and data types clearly defined?
3. **Documentation Completeness (2%)**
  - Is the interface documentation complete and up to date?
4. **Validation/Examples (2%)**
  - Are there usage examples or validation tests for the interfaces?

Each sub-criterion is scored individually and summed for the total 10%.

### Security Specification (10%) Breakdown

1. **Presence of Security Spec (3%)**
  - Is there a dedicated security specification or section?
2. **Threat Model/Analysis (3%)**
  - Is a threat model or security analysis included?
3. **Security Requirements (2%)**
  - Are security requirements and mitigations clearly specified?
4. **Validation/Testing (2%)**
  - Are there tests or evidence of security validation?

Each sub-criterion is scored individually and summed for the total 10%.

### Versioning & Compatibility Specification (10%) Breakdown

1. **Presence of Versioning Spec (3%)**
  - Is there a versioning and compatibility specification or section?
2. **Versioning Scheme Defined (3%)**
  - Is the versioning scheme (e.g., semver) clearly defined?
3. **Backward/Forward Compatibility (2%)**
  - Are compatibility requirements and guarantees documented?
4. **Migration/Upgrade Path (2%)**
  - Are migration or upgrade paths described?

Each sub-criterion is scored individually and summed for the total 10%.

### Conformance Testing Automation and Validation (10%) Breakdown

1. **Presence of Conformance Tests (3%)**
  - Are there automated/manual conformance tests?
2. **Test Coverage (3%)**
  - Is the test coverage for conformance requirements adequate?
3. **Test Documentation (2%)**
  - Is there documentation for running and interpreting conformance tests?
4. **Validation Results (2%)**
  - Are test results tracked and do they meet requirements?

Each sub-criterion is scored individually and summed for the total 10%.
### Performance Specification (10%) Breakdown

1. **Presence of Performance Spec (3%)**
  - Is there a dedicated performance specification document or section for the relevant modules/features?
2. **Defined Performance Metrics (3%)**
  - Are clear, measurable performance metrics (e.g., latency, throughput, resource usage) specified?
3. **Test Coverage for Performance (2%)**
  - Are there automated or manual tests that validate the defined performance metrics?
4. **Results & Validation (2%)**
  - Are actual performance results documented and do they meet the specified targets?

Each sub-criterion is scored individually and summed for the total 10%.
### Code to Spec Coverage (40%) Breakdown

1. **Reference Coverage (20%)**
  - Percentage of code modules/classes/functions that reference a spec (using the standard comment format).
2. **Spec Existence (10%)**
  - Percentage of referenced specs that actually exist in the `openspec/specs/` directory.
3. **Spec Completeness (5%)**
  - Percentage of referenced specs that have all required sections (title, description, requirements).
4. **No Orphaned Code (5%)**
  - Percentage of code modules with no missing or outdated spec references.

Each sub-criterion is scored individually and summed for the total 40%.
# openspec-coverage Skill

## Purpose
Automate the process of checking code-to-spec coverage in projects using Openspec. Calculates a compliance score and outputs a report of gaps and strengths.

## When to Use
- Auditing a repository for Openspec compliance
- Ensuring all code modules are covered by specs
- Generating a score/report for code-spec traceability



## Scoring Breakdown (0-100)

- **Code to Spec Coverage:** 40%
- **Architecture HLA Specification:** 10%
- **Performance Specification:** 10%
- **External Interface Specification:** 10%
- **Security Specification:** 10%
- **Versioning & Compatibility Specification:** 10%
- **Conformance Testing Automation and Validation:** 10%

## Workflow
1. Scan codebase for modules/classes/functions.
2. Detect references to Openspec specs (e.g., via comments like `// Spec: <spec_name>`).
3. Verify referenced specs exist in the `openspec/specs/` directory.
4. Identify code without spec references and specs not referenced by code.
5. Calculate a coverage score for each category above based on guidelines.
6. Output a report with:
  - Total score and per-category breakdown
  - Coverage percentage
  - Missing/mismatched specs
  - Orphaned code/specs
  - Suggestions for improvement

## Conventions
- Code should reference specs using a standard comment format: `// Spec: <spec_name>`
- All specs must be in `openspec/specs/`
- Specs should have required sections: Title, Description, Requirements

## Example Usage
- Run the skill to generate a compliance report:
  - "Check Openspec coverage for this repo."
  - "Show me the code-spec coverage score."

## Implementation Notes
- Can be implemented as a script or integrated with Copilot workflows.
- Extendable to support custom spec locations or comment formats.
