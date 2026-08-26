#!/usr/bin/env node
/**
 * Runtime AI inspection and query integration tests (UE 5.8).
 * Covers runtime AI inspect/query/debug with world targeting, EQS queries with
 * querierName, runtime behavior tree execution, runtime AI spawning via class
 * or blueprint paths, navigation path queries, and nav bounds volumeName alias.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const TEST_FOLDER = '/Game/MCP_Test';
const BT_PATH = `${TEST_FOLDER}/BT_MCPRuntime`;
const EQS_PATH = `${TEST_FOLDER}/EQE_MCPDemo`;

const testCases = [
  // === SETUP ===
  { scenario: 'Setup: create runtime behavior tree asset', toolName: 'manage_ai', arguments: { action: 'create_behavior_tree', name: 'BT_MCPRuntime', path: TEST_FOLDER }, expected: 'success|already exists' },
  { scenario: 'Setup: create EQS query asset for runtime execution', toolName: 'manage_ai', arguments: { action: 'create_eqs_query', name: 'EQE_MCPDemo', path: TEST_FOLDER }, expected: 'success|already exists' },
  { scenario: 'Setup: create pawn blueprint used as spawn_ai_runtime target', toolName: 'manage_blueprint', arguments: { action: 'ensure_exists', name: 'BP_MCPAIPawn', path: TEST_FOLDER, blueprintType: 'Character' }, expected: 'success|already exists' },

  // === RUNTIME AI INSPECTION ===
  { scenario: 'INSPECT: inspect_runtime_ai in PIE world filters controllerName stuckSpeedThreshold', toolName: 'manage_ai', arguments: { action: 'inspect_runtime_ai', world: 'PIE', controllerName: `MCP_AIController_${ts}`, stuckSpeedThreshold: 150 }, expected: 'success|not found' },
  { scenario: 'QUERY: query_runtime_ai summarizes runtime AI state in PIE', toolName: 'manage_ai', arguments: { action: 'query_runtime_ai', world: 'PIE' }, expected: 'success|not found' },
  { scenario: 'DEBUG: debug_runtime_ai reports diagnostics in PIE', toolName: 'manage_ai', arguments: { action: 'debug_runtime_ai', world: 'PIE' }, expected: 'success|not found' },

  // === RUNTIME EQS / ENV QUERY ===
  { scenario: 'EQS: run_env_query uses querierName actor label', toolName: 'manage_ai', arguments: { action: 'run_env_query', queryPath: EQS_PATH, querierName: `MCP_AIQuerier_${ts}` }, expected: 'success|not found' },
  { scenario: 'EQS: run_runtime_eqs executes the same query against the editor world', toolName: 'manage_ai', arguments: { action: 'run_runtime_eqs', queryPath: EQS_PATH }, expected: 'success|not found' },

  // === RUNTIME BEHAVIOR TREES ===
  { scenario: 'BT: start_runtime_behavior_tree drives a tree on the runtime controller', toolName: 'manage_ai', arguments: { action: 'start_runtime_behavior_tree', behaviorTreePath: BT_PATH }, expected: 'success|not found' },
  { scenario: 'BT: run_behavior_tree_runtime mirrors the runtime start alias', toolName: 'manage_ai', arguments: { action: 'run_behavior_tree_runtime', behaviorTreePath: BT_PATH }, expected: 'success|not found' },

  // === RUNTIME SPAWNING ===
  { scenario: 'SPAWN: spawn_ai_runtime from native class paths', toolName: 'manage_ai', arguments: { action: 'spawn_ai_runtime', pawnClassPath: '/Script/Engine.DefaultPawn', controllerClassPath: '/Script/AIModule.AIController', spawnCount: 1 }, expected: 'success|not found' },
  { scenario: 'SPAWN: spawn_runtime_ai from pawnBlueprintPath with controllerPath fallback', toolName: 'manage_ai', arguments: { action: 'spawn_runtime_ai', pawnBlueprintPath: `${TEST_FOLDER}/BP_MCPAIPawn`, controllerPath: '/Script/AIModule.AIController', spawnCount: 1 }, expected: 'success|not found' },

  // === NAVIGATION QUERIES ===
  { scenario: 'NAV: query_navigation_path between explicit start and end points', toolName: 'manage_ai', arguments: { action: 'query_navigation_path', start: { x: 0, y: 0, z: 92 }, end: { x: 800, y: 0, z: 92 } }, expected: 'success|not found' },
  { scenario: 'NAV: create_nav_mesh_bounds resolves volumeName alias for boundsActorName', toolName: 'manage_ai', arguments: { action: 'create_nav_mesh_bounds', volumeName: `MCPSandboxNavBounds_${ts}`, extent: { x: 2500, y: 2500, z: 500 } }, expected: 'success|already exists' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete runtime AI fixture assets', toolName: 'manage_asset', arguments: { action: 'delete_assets', paths: [BT_PATH, EQS_PATH, `${TEST_FOLDER}/BP_MCPAIPawn`], force: true }, expected: 'success|not found' },
];

runToolTests('runtime-ai-inspection-ue58', testCases);
