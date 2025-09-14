# VMCLiveLink

**Real-time performance meets Unreal Engine.**

VMCLiveLink is an Unreal Engine project that brings the **VMC (Virtual Motion Capture) protocol** directly into Unreal. It’s designed for creators who want to animate digital characters live — from VTubers to XR performers to virtual production teams.

## What It Does

- **Turns motion into presence.** Stream body and facial capture data straight into Unreal in real time.  
- **Bridges tools and stages.** Connects external mocap apps (like Virtual Motion Capture, VSeeFace, etc.) with Unreal’s Live Link system.  
- **Empowers performers.** Built for interactive avatars, live shows, and experiments in virtual puppetry.  

## Why It Matters

VMCLiveLink is more than a plugin — it’s part of a larger movement: treating technology like an *instrument* that artists can play. By lowering technical barriers, it enables performers and storytellers to focus on the message, the character, and the moment.

Whether you’re crafting an XR dance performance, streaming a VTuber show, or building a virtual stage for a live audience, VMCLiveLink gives you the bridge between raw performance and expressive digital presence.

## Getting Started

1. Clone the repo (requires [Git LFS](https://git-lfs.github.com/)):
   ```bash
   git clone https://github.com/atgoldberg/VMCLiveLink.git
   cd VMCLiveLink
   git lfs pull

## 🚀 Fab Plugin CI/CD

This repository includes automated GitHub Actions to keep the plugin Fab-ready.

### 🔎 Verify Build & Package
[![Fab Plugin Builds](https://github.com/<your-org>/<your-repo>/actions/workflows/fab-plugin-build.yml/badge.svg)](https://github.com/<your-org>/<your-repo>/actions/workflows/fab-plugin-build.yml)

Runs on tag push (`release/*`) or manually via **Actions → Fab Plugin Builds**:
- Verifies all source files have a valid copyright header.
- Builds the plugin for the specified Unreal Engine roots/versions.
- Produces Fab-ready zips as downloadable artifacts.

**Usage:**
1. Go to **Actions → Fab Plugin Builds → Run workflow**.
2. Fill in:
   - `plugin_dir` → path to plugin folder (default: `Plugins/VMCLiveLink`)
   - `engine_roots` → comma-separated UE roots (e.g. `C:\UE\5.4,C:\UE\5.5,C:\UE\5.6`)
   - `engine_versions` → matching versions (e.g. `5.4.0,5.5.0,5.6.0`)

### 🛠️ Auto-fix Headers
[![Auto-fix Headers](https://github.com/<your-org>/<your-repo>/actions/workflows/header-autofix.yml/badge.svg)](https://github.com/<your-org>/<your-repo>/actions/workflows/header-autofix.yml)

Ensures every `.h/.cpp` file starts with:

```cpp
// Copyright (c) YYYY Lifelike & Believable Animation Design, Inc. All Rights Reserved.
