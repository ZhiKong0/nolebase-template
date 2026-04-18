# Coordination System Layer

This folder contains the reusable part of the multi-AI coordination package.

## Structure

- `../bin/`: portable command entrypoints
- `../tooling/`: package implementation
- `prompts/`: reusable prompts for briefing new AIs
- `guides/`: operating guides for humans and AIs
- `templates/`: scaffolding used to create runtime project folders
- `state/`: machine-readable package registry

## Boundary

Project-specific live context does not belong here.

Live runtime state belongs under `coordination/runtime/projects/<project-id>/`.
