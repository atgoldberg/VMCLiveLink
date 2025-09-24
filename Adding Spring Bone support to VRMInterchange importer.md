Analysis of the current VRMTranslator.cpp

* Scope and maturity  
  * Uses Interchange UE 5.6 APIs correctly. Clean separation between parsing (cgltf) and Interchange graph construction.  
  * Robust resource handling (RAII wrapper for cgltf), centralized validation, axis/ref-pose corrections, morph target support, materials/textures creation, and mesh payloads (base and morph targets).  
  * Produces scene joint nodes and a skinned mesh node referencing the skeleton root scene node. No explicit Skeleton or SkeletalMesh factory nodes are authored; it delegates to the Interchange pipeline/factories.  
* Geometry and skinning  
  * Primitives are merged into a single skeletal mesh. Weights are mapped via NodeIndex-\>BoneIndex for the first skin. Fallback binds to root if absent.  
  * Morph targets: parsed and exposed as Interchange morph target mesh nodes \+ payloads.  
* Materials and textures  
  * Texture nodes/factory nodes created, material shader graphs created per material with PBRMR wiring and subpath control. Texture naming convention applied (diffuse, normal, ORM, etc.).  
* Scene and skeleton  
  * Scene root and joint nodes created with bind pose attributes and proper specialized type flags.  
  * Reference-pose “fix” applied to bones and vertices (mirror \+ \+90° yaw) and pure-translation local binds computed.  
* What’s missing for VRM “secondary animation”/spring bones  
  * No parsing of any VRM-specific extensions (VRM 0.x “extensions.VRM.secondaryAnimation” or VRM 1.0 “extensions.VRMC\_springBone”).  
  * No creation of a data asset encapsulating spring bone chains/colliders/settings.  
  * No runtime animation solution (custom AnimGraph node or mapping to built-in nodes) and no post-process Anim Blueprint authoring/assignment.  
  * No Interchange pipeline hook to generate auxiliary assets and set USkeletalMesh PostProcess AnimBP.

Design goals for Spring Bones

* Zero-touch after import: imported SkeletalMesh should already simulate spring bones via a post-process AnimBP assigned on the SkeletalMesh.  
* Still customizable: users can edit/replace the generated data asset and AnimBP, or opt-out/regenerate via import options.  
* Incremental, low-risk: avoid regressions; add features in small, testable steps without perturbing existing mesh/material/morph import.

Step-by-step plan (incremental, buildable at each stage)

Phase 0 — Safety rails and observability

* Add a verbose log category for VRM spring bones.  
* Add project settings/import options (disabled by default initially) for “Generate Spring Bones”.  
* Unit/integration logging: clearly report whether VRM spring bone data is detected and counts.

Phase 1 — Parse VRM Spring Bone data (no authoring yet)

* Add an internal data model to hold spring-bone config:  
  * Chains/springs: list of joints/bones, per-spring parameters (stiffness, drag, gravity dir/power, center, hitRadius).  
  * Colliders and collider groups (sphere/capsule), associations to springs.  
  * Bone names resolved to FName (via cgltf node-\>name); store also original node indices for robustness.  
* Implement parsers:  
  * VRM 0.x: parse JSON in glTF “extensions.VRM.secondaryAnimation”. Important keys: boneGroups/bones, center, stiffness, dragForce, gravityDir/gravityPower, hitRadius, colliderGroups, colliders (sphere: offset+radius).  
  * VRM 1.0: parse “extensions.VRMC\_springBone” (specVersion, colliders, colliderGroups, joints, springs).  
* Keep this purely in memory and log summary. Don’t alter import outputs yet.  
* Testing: import a few VRMs (0.x and 1.0), verify logs and counts.

Risk: None to the existing pipeline. This phase can ship independently.

Phase 2 — Author a Spring Bone DataAsset (post-import via custom Interchange pipeline)

* Add a new editor-only DataAsset type (e.g., UVMRSpringBoneData) capturing the parsed config:  
  * TArray\<FSpring\>, TArray\<FColliderGroup\>, each referencing bones by FName.  
  * Store both FName and original node index (for diagnostics).  
  * Include a “SourceHash/Signature” to detect staleness (from UInterchangeSourceData hash).  
* Implement a custom Interchange Pipeline (e.g., UVRMInterchangeSpringPipeline : UInterchangePipelineBase):  
  * In ExecutePostImport, find the created USkeletalMesh and USkeleton from the NodeContainer/Results.  
  * Re-parse the source file using cgltf (to avoid coupling with Translator state) and extract spring bones (reuse Phase 1 parser).  
  * Create a UVMRSpringBoneData asset in “\<ImportedPackage\>/\<MeshName\>/SpringBones” (use FAssetTools). Use a predictable name. Save package.  
* Option: also write a raw JSON blob copy into the asset for troubleshooting.  
* Testing: ensure asset is created and references valid bone names for the imported skeleton. No changes yet to the mesh/ABP.

Risk: Minimal; everything is additive and Editor-only.

Phase 3 — Generate a Post-Process AnimBP (template-based, does nothing yet)

* Ship a template Post-Process AnimBP in the plugin content (e.g., ABP\_VRMSpringBones\_Template) whose AnimGraph simply passes through component pose (placeholder).  
* Pipeline: duplicate this template into the same package folder as the skeletal mesh, retarget its TargetSkeleton to the mesh’s USkeleton, and set a public variable “SpringConfig” (type UVMRSpringBoneData) default to the generated asset. Save package.  
* Assign the duplicated ABP as the SkeletalMesh’s PostProcess AnimBP (USkeletalMesh::PostProcessAnimBlueprint).  
* Testing: mesh imports with a post-process ABP assigned (still no simulation). User can open/edit it, and the reference to SpringConfig is visible.

Risk: None to runtime behavior; AnimGraph is pass-through.

Phase 4 — Minimal working simulation (built-in nodes)

* Without a custom node yet, produce a minimal motion using built-in nodes as a proof:  
  * Extend the template ABP with a small, fixed number of AnimDynamics or SpringController nodes driven by a subset of the parsed springs (e.g., hair bones).  
  * This can be a conservative, opt-in option; we do not attempt to auto-generate many nodes.  
* Pipeline: Optionally post-edit the duplicated ABP graph to initialize those nodes (or provide a second template with “generic spring behavior” and just hook its SpringConfig variable).  
* Testing: verify motion exists and works for simple cases. Keep scope small.

Risk: Limited fidelity vs VRM spec; acceptable as an intermediate milestone.

Phase 5 — Custom AnimGraph node FAnimNode\_VRMSpringBone

* Add a new runtime module (and editor module) that implements:  
  * FAnimNode\_VRMSpringBone (runtime) \+ UAnimGraphNode\_VRMSpringBone (editor node).  
  * The node takes a UVMRSpringBoneData (soft reference), per-spring overrides, gravity scale, debug draw, and collision settings.  
  * Simulation algorithm matching VRM semantics: per-joint length preservation, stiffness, drag, gravity, center constraint; collision with collider spheres/capsules; per-spring radius.  
  * Multi-threading safe, deterministic with a fixed sub-step and reset behavior.  
* Update the template ABP to use this single node (connect ComponentPose → VRMSpringBone → OutputPose).  
* Pipeline: the duplicated ABP already has the node in the template; the pipeline only sets the SpringConfig variable default.  
* Testing:  
  * Unit tests for math/collisions.  
  * Compare against known VRM viewers.  
  * Performance sanity (tick cost on typical desktop; no GC churn).

Risk: Medium. Self-contained and does not alter mesh import flow.

Phase 6 — Full VRM features and parity

* Support both VRM 0.x and 1.0 semantics (center bone, collider groups, drag, gravity dir/power).  
* Handle bone scaling and world/mesh space conversions robustly (respect UE units and the importer RefFix).  
* Correctly handle LOD/retarget scenarios and Morph targets active simultaneously.  
* Editor UX: debug preview toggles in the node (draw springs and colliders).

Phase 7 — Regeneration and customization safeguards

* Add import dialog/project settings toggles:  
  * Generate Spring Data  
  * Generate Post-Process ABP  
  * Assign Post-Process ABP to mesh  
  * Overwrite existing or create with suffix  
* If asset(s) already exist:  
  * By default, do not overwrite user edits; write a new asset with “\_Auto” suffix and optionally switch the mesh to it.  
  * Provide a context menu action “Rebuild VRM Spring Assets” (re-run Phase 2 and rebind ABP).  
* Store a “GeneratedBy=VRMImporter” metadata tag on generated assets.

Phase 8 — Documentation and samples

* Short “How it works” doc page (where assets are created, how to customize).  
* Sample VRMs to validate the pipeline end-to-end.

Validation plan per phase

* Phase 1: Unit tests that parse embedded JSON snippets (0.x and 1.0). Log counts match expectations.  
* Phase 2: Import a VRM; check that a SpringBones data asset is created, bone names all map to USkeleton.  
* Phase 3: Import a VRM; check PostProcess AnimBP is duplicated, retargeted to skeleton, referenced on the SkeletalMesh.  
* Phase 4: Verify basic motion on selected examples.  
* Phase 5/6: Cross-check behavior vs reference viewers; add perf counters and debug draws.  
* Regression: Ensure meshes, morphs, materials continue to import identically when spring generation is disabled.

Notes on integration points and file changes

* VRMTranslator.cpp: no changes needed until later; optional future addition is to store a tiny “has spring data” boolean as a custom attribute on the mesh node for the pipeline to read. Keeping translator stable reduces regression risk.  
* New modules:  
  * VRMInterchangeSpringPipeline (Editor-only): post-import authoring of the data asset and ABP, and assignment to SkeletalMesh.  
  * VRMSpringBonesRuntime (Runtime) and VRMSpringBonesEditor (Editor): custom AnimGraph node and editor node, template ABP in plugin content.  
* Asset locations:  
  * Place generated assets under “\<ImportedAssetName\>/SpringBones/” or “\<ImportedAssetName\>/Animation/”.  
  * Use predictable names with collision-safe suffixing.

Why this approach

* It keeps existing import behavior untouched until you explicitly enable spring-bone generation.  
* It makes the solution turnkey (post-process ABP assigned) while leaving assets editable by users.  
* It avoids heavy Interchange customization upfront by doing the authoring in a dedicated pipeline step.  
* It provides incremental milestones that are testable and low risk.

