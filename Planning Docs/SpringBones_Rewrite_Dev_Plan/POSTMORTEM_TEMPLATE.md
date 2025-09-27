# Postmortem — Task <TASK-NUMBER>: <TASK-TITLE>

## Summary
- **Goal:** <One-sentence statement of what the task delivers>
- **Scope Implemented:** <Bulleted list of concrete features / code paths implemented>
- **Out of Scope (Deferred):** <Bulleted list of items intentionally left for future tasks>

## Implementation Details
- **Files Added/Modified:**  
  - <path/to/file1> — <what/why>
  - <path/to/file2> — <what/why>
- **Key Types & Functions:**  
  - `<TypeOrFuncName>` — <purpose and inputs/outputs>
- **Important Decisions:**  
  - <decision + rationale, with links to VRM spec section or data asset fields as applicable>

## Spec & Data Conformance
- **VRM Spec References:**  
  - <link or section names used to guide implementation>
- **SpringBoneData Fields Used:**  
  - <list actual field names from UVRMSpringBoneData that were consumed>
- **Assumptions Avoided:**  
  - <confirm no invented fields; note any ambiguities encountered and how they were resolved>

## Tests Performed
- **Unit/Automation:**  
  - <describe math helper tests or low-level asserts>
- **Manual Scenes (from Global Test Plan):**  
  - Pendulum — <result>
  - Hair — <result>
  - Tail/Scarf — <result>
  - Stress — <result>
- **Builds:**  
  - Editor / Standalone / Shipping — <result>

## Acceptance Criteria Mapping
- **Criteria 1:** <how implementation satisfies it>
- **Criteria 2:** <...>
- **Criteria N:** <...>

## Known Limitations & TODOs
- <each with brief mitigation or follow-up suggestion>

## Handoff Notes
- **Next Task Impacted:** <Task #> — <what this task enables or constrains>
- **Suggested Follow-ups:** <short list>