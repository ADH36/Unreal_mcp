#!/usr/bin/env node
/**
 * HLOD lifecycle integration tests (UE 5.8 World Partition).
 * Covers HLOD layer inspection, generated HLOD inspection, build cancellation,
 * commandlet rebuild scope/timeout controls, assignment auto-configure flags,
 * and focused output deletion via generatedHlodActor.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const HLOD_LAYER_NAME = `MCP_HLOD_Demo_${ts}`;
const SOURCE_ACTOR = `MCP_HLODSource_${ts}`;

const testCases = [
  // === SETUP ===
  { scenario: 'Setup: spawn a static mesh source actor for HLOD assignment', toolName: 'control_actor', arguments: { action: 'spawn', classPath: '/Engine/BasicShapes/Cube', actorName: SOURCE_ACTOR, location: { x: 0, y: 0, z: 120 } }, expected: 'success|already exists' },
  { scenario: 'Setup: create dedicated HLOD layer asset', toolName: 'manage_level_structure', arguments: { action: 'create_hlod_layer', hlodLayerName: HLOD_LAYER_NAME }, expected: 'success|already exists' },

  // === LAYER INSPECTION / ASSIGNMENT ===
  { scenario: 'INSPECT: inspect_hlod_layer reports layer details by name', toolName: 'manage_level_structure', arguments: { action: 'inspect_hlod_layer', hlodLayerName: HLOD_LAYER_NAME }, expected: 'success|not found' },
  { scenario: 'ASSIGN: assign_hlod_layer with autoConfigure safe flag setup', toolName: 'manage_level_structure', arguments: { action: 'assign_hlod_layer', actorName: SOURCE_ACTOR, hlodLayerName: HLOD_LAYER_NAME, autoConfigure: true }, expected: 'success|not found' },

  // === BUILD ORCHESTRATION ===
  { scenario: 'REBUILD: rebuild_hlods runs the commandlet with bounded timeoutSeconds', toolName: 'manage_level_structure', arguments: { action: 'rebuild_hlods', timeoutSeconds: 1800 }, expected: 'success' },
  { scenario: 'REBUILD: rebuild_hlods rejects cellIds dataLayerNames scoped rebuilds', toolName: 'manage_level_structure', arguments: { action: 'rebuild_hlods', cellIds: ['MCP_CELL_0_0'], dataLayerNames: ['MCPRuntimeLayer'] }, expected: 'error|not supported' },
  { scenario: 'STATUS: cancel_hlod_build is a safe no-op without an active commandlet', toolName: 'manage_level_structure', arguments: { action: 'cancel_hlod_build' }, expected: 'success' },

  // === OUTPUT INSPECTION / DELETION ===
  { scenario: 'INSPECT: inspect_generated_hlods lists generated HLOD actors', toolName: 'manage_level_structure', arguments: { action: 'inspect_generated_hlods' }, expected: 'success' },
  { scenario: 'DELETE: delete_hlod_output targets focused generatedHlodActor after confirm', toolName: 'manage_level_structure', arguments: { action: 'delete_hlod_output', confirm: true, generatedHlodActor: `MCP_WP_HLOD_${ts}` }, expected: 'success|not found' },

  // === CLEANUP ===
  { scenario: 'Cleanup: remove demo source actor', toolName: 'control_actor', arguments: { action: 'delete', actorName: SOURCE_ACTOR }, expected: 'success|not found' },
];

runToolTests('hlod-lifecycle-ue58', testCases);
