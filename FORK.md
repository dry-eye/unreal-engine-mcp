# Fork notes — dry-eye/unreal-engine-mcp

This is a personal fork of [flopperam/unreal-engine-mcp](https://github.com/flopperam/unreal-engine-mcp) with extensions for richer Blueprint introspection and read tools for PCG / AnimBlueprint / Control Rig.

## What's different from upstream

- **`HandleGetBlueprintVariableDetails`** now also walks `Blueprint->GeneratedClass` via `TFieldIterator<…, IncludeSuper>` and returns inherited C++/BP-parent properties, each tagged with `source` (`blueprint_own` / `cpp_parent` / `bp_parent`) and `parent_class`.
- **`analyze_blueprint_graph`** serializes pin defaults (`default_value`, `default_object`, `default_text_value`, `autogen_default`) and `sub_category`/`sub_category_object` for every pin; defaults are emitted for unconnected input pins only.
- **`read_blueprint_content`** lists inherited components from the CDO (`AActor::GetComponents()`), in addition to SCS nodes. Each component is tagged `inherited: true/false`.
- New **read-only** commands (and Python MCP tools):
  - `read_anim_blueprint(blueprint_path)`
  - `read_control_rig(blueprint_path)`
  - `read_pcg_graph(graph_path)`
- New `UnrealMCP.Build.cs` dependencies (editor-only): `AnimGraph`, `Persona`, `AnimationBlueprintEditor`, `ControlRig`, `ControlRigDeveloper`, `RigVM`, `RigVMDeveloper`, `PCG`, `PCGEditor`.

The Phase 1 BP-reading fixes are upstream-PR candidates; the new domain handlers (AnimBP/CR/PCG) live in their own files so merges stay clean.

## Sharing the plugin across multiple UE projects (NTFS junction)

Single master plugin directory: `M:\Projects\unreal-engine-mcp\UnrealMCP`. Linked into each project via NTFS junction so there is exactly one copy on disk.

### One-time setup per UE project

From PowerShell:

```powershell
& 'M:\Projects\unreal-engine-mcp\scripts\link-into-project.ps1'
# Enter UE project path (folder with .uproject): M:\UnrealProjects\Sim
```

The script:

1. Validates the project folder contains a `.uproject` and ensures `Plugins\` exists.
2. Creates `<Project>\Plugins\UnrealMCP` as a junction pointing to the master repo. If a junction already exists it asks before replacing. If a *real* directory exists at that path the script aborts to avoid data loss.
3. Idempotently appends `Plugins/UnrealMCP/` to both `.gitignore` and `.dvignore` so neither git nor Diversion ever commits the junction contents.

Then in the project: open the editor → Plugins → enable **UnrealMCP** → rebuild.

### Update workflow

```powershell
cd M:\Projects\unreal-engine-mcp ; git pull
```

Every linked project sees the new source automatically. Rebuild the UE project (live coding works) and restart Claude Code so the Python server reloads.

### Sync with upstream

```powershell
cd M:\Projects\unreal-engine-mcp
git fetch upstream
git merge upstream/main
# resolve conflicts (most fork changes are in separate files; only Build.cs / Bridge.cpp touch upstream)
git push origin main
```

## MCP server configuration

The Python server is shared across projects. In `~/.claude.json` (or per-project equivalent) point one entry at:

```
M:\Projects\unreal-engine-mcp\Python\unreal_mcp_server_advanced.py
```

The plugin listens on `127.0.0.1:55557` from whatever UE editor is currently open.
