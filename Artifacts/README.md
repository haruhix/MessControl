# Validation evidence and local media

`Artifacts/` keeps concise reports, validation metadata and source recipes. Game resources belong in `Content/`; necessary editable masters belong in their source folders.

## Local media

Recordings, screenshots, frame sequences, renders, encoder manifests and rollback package copies remain local on E:. New output should use `E:/CodexWork/MessControl/Validation/<task>/` or another explicit task folder on E:. Recording a preview does not automatically authorize adding the media to Git.

Historical documents marked **медиа локально** describe recordings from that checkpoint. Their report links remain available, but a clean clone does not include the associated media. Historical reports and encoder recipes may still name their original local input/output paths; those paths are provenance, not a promise of a bundled recording.

## Cleanup checkpoint — 2026-10-28

- Removed from the current Git tree: 10,493 generated media/encoder files and 145 explicitly named binary backups; total payload **9,057,029,806 bytes** (9.06 GB / 8.44 GiB).
- Preserved every existing local file byte-for-byte. Of those paths, 876 were already unhydrated Git LFS pointers; this cleanup does not download their missing payloads.
- Retained reports, JSON inputs used by project tools, source recipes, editable `.blend`/`.fbx` files, preview-runtime `.pyd` dependencies and an uncertain recovery map. The raft palette reference `NutVFXPolish_20261013/NutFireNutStart.png` is an explicit media exception.
- No runtime packages in `Content/` or masters in `ArtSource/` were removed. No history rewrite or remote LFS-object purge was performed.

The machine-local exact path/OID/hash manifest is `E:/CodexWork/MessControl/CloudCleanup_20261028/RemovalManifest.json`. A compact [report](CloudCleanup_20261028/Report.json) is versioned here.

## Recovering an old recording

The last commit before this cleanup is `cd5658f196a851a7a3a40660acb92bcb0ce3fbc0`. Existing local recordings remain at their original paths. On another checkout, inspect that commit and restore only the desired file, for example:

```powershell
git restore --source=cd5658f196a851a7a3a40660acb92bcb0ce3fbc0 --worktree -- Artifacts/<task>/<recording>.mp4
```

Replace the example path with a real historical path and check for an existing local file first. Restoration may download its LFS payload. Do not restore or force-add the whole media tree to make a single preview available.

## GitHub LFS storage

Removing paths from the current tree reduces the media downloaded by a normal checkout. Old commits still reference the old LFS objects, which continue to count toward GitHub storage. Reclaiming that historical storage is a separate, coordinated task; this commit does not reclaim it. See [GitHub's LFS removal documentation](https://docs.github.com/en/repositories/working-with-files/managing-large-files/removing-files-from-git-large-file-storage).
