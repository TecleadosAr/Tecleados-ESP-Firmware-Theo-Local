# Tecleados ESP Firmware Rules

## 1. Documentation Strategy for Agents (CRITICAL FOR CONTEXT EFFICIENCY)
This project has two distinct layers of documentation. To avoid wasting context window tokens and confusing yourself with duplicated information, you **MUST** strictly adhere to the following reading strategy:

*   **For High-Level Overviews & Architecture:** ONLY read the files in the `universe/modules/` directory (start at `../universe/Home.md`). Use these to understand how modules relate and depend on each other before proposing architectural changes.
*   **For Deep Debugging & Implementation Details:** ONLY read the specific `*_MODULE.md` file located inside the actual module folder in the `components/` directory. Use these before modifying, refactoring, or touching the internal code of a specific module.

**DO NOT** read both the `universe/` overview and the `components/` deep-dive for the same module simultaneously unless strictly necessary. Choose the level of detail appropriate for your current task.

## User-facing change explanations

For significant changes, add a dated Spanish explanation under `explicaciones/` using `explicaciones/PLANTILLA.md`, and update its index. Explain the observed problem, mechanism, before/after, scope and authorization of executed actions, validation, remaining limits, and recovery. Reference evidence identifiers without publishing raw logs, flash backups or device identifiers.

Keep firmware changes, local tooling and Windows configuration changes distinct. A request for an explanation is not authorization to operate hardware. Preserve existing documentation layers: `universe/` remains the architectural overview and component-local documentation remains the implementation reference. `explicaciones/` records user learning and change rationale.
