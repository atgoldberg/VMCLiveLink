# VRM Interchange Plugin

**Import VRM avatars into Unreal Engine with full support for Spring Bones, IK Rigs, and Live Link.**

The VRM Interchange plugin is a comprehensive VRM (.vrm) importer for Unreal Engine 5.6+ that leverages the modern Interchange framework. It imports VRM avatars with complete support for skeletal meshes, textures, blend shapes (morph targets), and advanced features like physics-based spring bones, IK rigs, and Live Link integration.

## Features

### Core Import Capabilities
- **VRM Format Support**: Imports VRM 0.x / 1.0 files (glTF 2.0-based avatar format)
- **Skeletal Mesh**: Full skeleton hierarchy with proper bone orientations
- **Textures**: Automatic texture import with proper material setup
- **Blend Shapes**: Morph target support for facial expressions
- **Materials**: Basic material setup with texture assignments

### Spring Bones (Physics Hair/Clothing)
- **Automatic Spring Data Generation**: Parses VRM spring bone configurations into data assets
- **Real-time Physics Simulation**: Verlet integration-based spring bone system
- **Collider Support**: Sphere and capsule colliders for realistic physics interactions
- **Animation Blueprint Integration**: Optional post-process AnimBP for automatic spring simulation
- **Customizable Parameters**: Stiffness, gravity, damping, and more

### IK Rig Integration
- **Automatic IK Rig Setup**: Generates IK Rig assets from templates
- **Preview Mesh Assignment**: Automatically configures preview meshes
- **VRM-to-UE5 Bone Mapping**: Proper bone chain setup for animation retargeting

### Live Link Support
- **Character Scaffold Generation**: Creates ready-to-use Actor and AnimBP blueprints
- **VMC Protocol Ready**: Compatible with VMC Live Link for real-time performance capture
- **Retargeting Support**: Includes retargeting actor setup for animation transfer

## Installation

### As a Plugin in Your Project

1. Copy the `VRMInterchange` plugin folder to your project's `Plugins` directory:
   ```
   YourProject/Plugins/VRMInterchange/
   ```

2. Right-click your `.uproject` file and select **Generate Visual Studio project files**

3. Open your project in Unreal Engine. The plugin will be enabled automatically.

4. If prompted, allow Unreal to rebuild the plugin modules.

### Requirements

- **Unreal Engine**: 5.6 or later
- **Dependencies**: 
  - Interchange (built-in)
  - InterchangeEditor (built-in)
  - IKRig (built-in)
  - LiveLink (built-in)

## Quick Start

### Importing a VRM File

1. **Drag and Drop**: Drag a `.vrm` file into the Content Browser
2. **Import Dialog**: The Interchange import dialog will appear with VRM-specific pipelines
3. **Configure Options**: 
   - Keep default settings for standard import
   - Enable/disable Spring Bones, IK Rig, or Live Link features as needed
4. **Import**: Click **Import** to complete the process

### What Gets Created

After import, you'll find:

```
Content/
└── YourCharacterName/
    ├── YourCharacterName_SkeletalMesh (Skeletal Mesh)
    ├── YourCharacterName_Skeleton (Skeleton asset)
    ├── YourCharacterName_PhysicsAsset (Physics Asset)
    ├── Materials/ (Generated materials)
    ├── Textures/ (Imported textures)
    ├── SpringBones/
    │   ├── YourCharacterName_SpringData (Spring configuration)
    │   └── ABP_SpringBones_YourCharacterName (Optional AnimBP)
    ├── IKRigDefinition/
    │   └── IK_Rig_VRM_YourCharacterName (IK Rig asset)
    └── LiveLink/
        ├── BP_LL_YourCharacterName (Character Actor)
        ├── Animation/
        │   └── ABP_LL_YourCharacterName (Live Link AnimBP)
        └── RetargetActor_YourCharacterName (Retargeting actor)
```

## Configuration

### Project Settings

Configure default import behavior in **Edit → Project Settings → Plugins → VRM Interchange**:

#### Spring Bones Settings
- **Generate Spring Bone Data**: Parse and create spring bone data assets during import (default: enabled)
- **Generate Post Process AnimBP**: Create and assign an AnimBP for spring simulation (default: disabled)
- **Assign Post Process ABP**: Automatically assign the generated AnimBP to the skeletal mesh (default: disabled)
- **Overwrite Existing Spring Assets**: Replace existing assets on re-import (default: disabled)
- **Overwrite Existing Post Process ABP**: Replace existing AnimBPs on re-import (default: disabled)
- **Reuse Post Process ABP On Reimport**: Reuse existing AnimBP when re-importing (default: enabled)

#### IK Rig Settings
- **Generate IK Rig Assets**: Create IK Rig definitions from templates (default: enabled)

#### Live Link Settings
- **Generate Live Link Actor Scaffold**: Create Actor and AnimBP scaffolds for Live Link (default: enabled)

### Per-Import Settings

During each import, you can override project settings in the Interchange import dialog:

1. Expand the pipeline sections in the import dialog
2. Adjust settings for **VRM Spring Bones**, **VRM IK Rig**, and **VRM Live Link** pipelines
3. These override the project defaults for this import only

## Using Spring Bones

### Automatic Setup (Recommended)

1. Enable **Generate Spring Bone Data** in project settings
2. Optionally enable **Generate Post Process AnimBP** and **Assign Post Process ABP**
3. Import your VRM file
4. The spring bone system will be automatically configured and applied

### Manual Setup

If you prefer manual control:

1. Import with only **Generate Spring Bone Data** enabled
2. Open your character's Animation Blueprint
3. Add a **VRM Spring Bones** node in the AnimGraph
4. Connect it to your pose chain
5. In the node details, set:
   - **Spring Data**: Select the generated `_SpringData` asset
   - **Enable**: Check to activate simulation

### Spring Bone Parameters

The `VRMSpringBoneData` asset contains:
- **Spring Config**: Per-spring stiffness, damping, gravity, and drag force
- **Node Hierarchy**: Bone parent-child relationships
- **Colliders**: Sphere and capsule collision volumes
- **Joint Settings**: Per-joint radius and hit radius

Edit these values in the Data Asset to fine-tune spring behavior.

## Using IK Rigs

The generated IK Rig assets enable:
- **Animation Retargeting**: Transfer animations between different skeletons
- **IK Chains**: Inverse kinematics for procedural animation
- **Runtime Poses**: Control character poses programmatically

### Basic Usage

1. Open the generated `IK_Rig_VRM_YourCharacterName` asset
2. The rig is pre-configured with VRM bone chains
3. Use with IK Retargeter assets to transfer animations
4. Reference in your Animation Blueprints for IK control

## Using Live Link

### Automatic Scaffold

When **Generate Live Link Actor Scaffold** is enabled, the plugin creates:
- **Character Actor BP**: Ready-to-place character with skeletal mesh
- **Animation BP**: Live Link-enabled animation blueprint
- **Retarget Actor**: For animation retargeting workflows

### Setting Up VMC Protocol

1. Place the generated `BP_LL_YourCharacterName` actor in your level
2. Open **Window → Live Link**
3. Add a **VMC Live Link Source** (requires VMCLiveLink plugin)
4. Configure your external VMC application to send to Unreal's IP and port
5. The character will animate in real-time with incoming motion data

### Customizing the Live Link Setup

The generated blueprints are templates you can extend:
- Add custom animation layers in the AnimBP
- Modify the character actor for gameplay logic
- Adjust Live Link subject selection and bone mapping

## Advanced Topics

### Re-importing VRM Files

When re-importing:
1. Existing assets are updated based on overwrite settings
2. Spring bone data is regenerated from the VRM file
3. Manual edits to generated blueprints are preserved
4. Texture and material updates are applied

### Spring Bone Simulation Details

The spring bone solver uses:
- **Verlet Integration**: Stable physics simulation
- **Deterministic Sub-stepping**: Consistent results regardless of frame rate
- **Hierarchical Propagation**: Parent-child bone chain resolution
- **Collision Detection**: Per-joint sphere and capsule collision

Debug visualization is available via console commands:

*To access the console in Unreal Engine, press the tilde key (`~`) in the editor, or open the Output Log window and enter commands there.*
```
vrm.SpringBones.DrawColliders 1    // Enable collider debug draw
vrm.SpringBones.DrawColliders 0    // Disable collider debug draw
vrm.SpringBones.DrawSprings 1      // Enable spring debug draw
vrm.SpringBones.DrawSprings 0      // Disable spring debug draw
```

## Troubleshooting

### Import Dialog Doesn't Appear
- Ensure the Interchange and InterchangeEditor plugins are enabled
- Check that the file has a `.vrm` extension
- Verify the VRM file is valid (not corrupted)

### Spring Bones Not Animating
- Check that the VRM Spring Bones AnimGraph node is enabled
- Verify the Spring Data asset is assigned
- Ensure the Spring Data asset is not empty (check SpringConfig)
- Confirm bone names in Spring Data match your skeleton

### IK Rig Not Generated
- Enable **Generate IK Rig Assets** in project settings
- Ensure IKRig plugin is enabled in your project
- Check that the template IK Rig asset exists in the plugin content

### Live Link Actor Missing Components
- Verify LiveLink plugin is enabled
- Check that template blueprints exist in plugin Content folder
- Review the Output Log for any blueprint compilation errors

### Textures Import as Black
- VRM files use embedded or referenced textures
- Check that the VRM file contains valid image data
- Ensure textures are not in an unsupported format

### Mesh Orientation Wrong
- VRM uses a different coordinate system than UE5
- The importer automatically applies necessary transforms
- If orientation is still incorrect, check the VRM file's scene root transforms

## Technical Details

### Architecture

The plugin consists of four modules:

1. **VRMInterchange** (Runtime)
   - `UVRMTranslator`: Interchange translator for .vrm files
   - Parses VRM/glTF data using cgltf library
   - Generates Interchange node graphs

2. **VRMInterchangeEditor** (Editor)
   - Post-import pipelines for Spring Bones, IK Rig, and Live Link
   - Project settings integration
   - Asset generation and wiring

3. **VRMSpringBonesRuntime** (Runtime)
   - `FAnimNode_VRMSpringBones`: Animation node for spring simulation
   - `UVRMSpringBoneData`: Data asset for spring configuration
   - Physics solver implementation

4. **VRMSpringBonesEditor** (Editor)
   - `UAnimGraphNode_VRMSpringBones`: AnimGraph node wrapper
   - Editor customizations and debugging tools

### File Format

VRM is a 3D avatar format based on glTF 2.0, specifically designed for VR applications. It includes:
- Standard glTF structure (meshes, materials, textures)
- VRM-specific extensions for humanoid avatars
- Spring bone physics metadata
- Blend shape clip definitions

### Coordinate System

VRM uses Y-up, right-handed coordinates. The importer converts to UE5's Z-up, left-handed system:
- Mirrors across the Y axis
- Applies 90° rotation around Z
- Adjusts bone transforms accordingly

## Known Limitations

- **VRM Version**: Supports VRM 0.x and VRM 1.0
- **Material Complexity**: Complex shader graphs may require manual adjustment
- **Expression Mapping**: Blend shape to morph target mapping is direct (no presets)
- **Texture Formats**: Embedded textures must be PNG or JPEG

## Support and Contribution

This plugin is part of the VMCLiveLink project. For issues, feature requests, or contributions:
- GitHub: [atgoldberg/VMCLiveLink](https://github.com/atgoldberg/VMCLiveLink)
- Issues: Use the GitHub issue tracker

## License

Copyright (c) 2024 Lifelike & Believable Animation Design, Inc. All Rights Reserved.

## Credits

- **Created by**: Lifelike & Believable Animation Design, Inc.
- **VRM Format**: Developed by VRM Consortium
- **glTF Parsing**: Uses cgltf library by Johannes Kuhlmann

## Additional Resources

- [VRM Format Specification](https://github.com/vrm-c/vrm-specification)
- [Unreal Engine Interchange Documentation](https://docs.unrealengine.com/5.6/en-US/interchange-framework-in-unreal-engine/)
- [IK Rig Documentation](https://docs.unrealengine.com/5.6/en-US/ik-rig-in-unreal-engine/)
- [Live Link Documentation](https://docs.unrealengine.com/5.6/en-US/live-link-in-unreal-engine/)
