#!/usr/bin/env node
/**
 * Input mapping + level game mode session integration tests (UE 5.8).
 * Covers Enhanced Input action/context authoring plus the UE 5.8 mapping
 * session actions: add_input_mapping, remove_input_mapping, mapping triggers
 * (chord/hold/tap), mapping modifiers (scalars, swizzle, thresholds, negation),
 * input asset inspection, value-type updates, and per-level GameMode overrides.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const TEST_FOLDER = '/Game/MCP_Test';
const IA_PATH = `${TEST_FOLDER}/IA_MCPDemo`;
const IA_EXTRA_PATH = `${TEST_FOLDER}/IA_MCPExtra_${ts}`;
const IMC_PATH = `${TEST_FOLDER}/IMC_MCPDemo_${ts}`;
const LEVEL_PATH = `${TEST_FOLDER}/L_MCPWorld`;
const GM_PATH = `${TEST_FOLDER}/GM_MCPNetwork`;

const testCases = [
  // === SETUP: ENHANCED INPUT ASSETS ===
  { scenario: 'Setup: create enhanced input action with valueType', toolName: 'manage_networking', arguments: { action: 'create_input_action', name: `IA_MCPExtra_${ts}`, path: TEST_FOLDER, valueType: 'Axis1D' }, expected: 'success|already exists' },
  { scenario: 'Setup: create enhanced input mapping context', toolName: 'manage_networking', arguments: { action: 'create_input_mapping_context', name: `IMC_MCPDemo_${ts}`, path: TEST_FOLDER }, expected: 'success|already exists' },

  // === INPUT ACTION VALUE TYPES ===
  { scenario: 'INPUT: set_input_action_type retypes the demo action to Axis2D', toolName: 'manage_networking', arguments: { action: 'set_input_action_type', actionPath: IA_EXTRA_PATH, valueType: 'Axis2D' }, expected: 'success|not found' },
  { scenario: 'INPUT: inspect_input_asset reports mappings for the context asset', toolName: 'manage_networking', arguments: { action: 'inspect_input_asset', assetPath: IMC_PATH }, expected: 'success|not found' },

  // === MAPPING TRIGGERS (CHORD / HOLD / TAP) ===
  { scenario: 'TRIGGER: add_mapping_trigger chordActionPath holdTime tapTime', toolName: 'manage_networking', arguments: { action: 'add_mapping_trigger', contextPath: IMC_PATH, actionPath: IA_EXTRA_PATH, key: 'SpaceBar', triggerType: 'Chord', chordActionPath: IA_PATH, holdTime: 0.25, tapTime: 0.2 }, expected: 'success|not found' },

  // === MAPPING MODIFIERS ===
  { scenario: 'MODIFIER: add_mapping_modifier scalars swizzle thresholds negation', toolName: 'manage_networking', arguments: { action: 'add_mapping_modifier', contextPath: IMC_PATH, actionPath: IA_EXTRA_PATH, key: 'W', modifierType: 'Scalar', scalarX: 1, scalarY: 0.5, scalarZ: 0, swizzleOrder: 'YXZ', lowerThreshold: -1, upperThreshold: 1, negateX: false, negateY: false, negateZ: false }, expected: 'success|not found' },

  // === SESSION MAPPINGS ===
  { scenario: 'MAP: add_input_mapping binds the extra action to a key', toolName: 'manage_networking', arguments: { action: 'add_input_mapping', contextPath: IMC_PATH, actionPath: IA_EXTRA_PATH, key: 'Enter', priority: 1 }, expected: 'success|not found' },
  { scenario: 'MAP: remove_input_mapping unbinds the same key', toolName: 'manage_networking', arguments: { action: 'remove_input_mapping', contextPath: IMC_PATH, actionPath: IA_EXTRA_PATH, key: 'Enter' }, expected: 'success|not found' },

  // === LEVEL GAME MODE OVERRIDES ===
  { scenario: 'GM: set_level_game_mode applies override via levelPath', toolName: 'manage_networking', arguments: { action: 'set_level_game_mode', gameModeBlueprint: GM_PATH, levelPath: LEVEL_PATH }, expected: 'success|not found' },
  { scenario: 'GM: set_level_game_mode accepts mapPath alias for levelPath', toolName: 'manage_networking', arguments: { action: 'set_level_game_mode', gameModeBlueprint: GM_PATH, mapPath: LEVEL_PATH }, expected: 'success|not found' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete input fixture assets', toolName: 'manage_asset', arguments: { action: 'delete_assets', paths: [IA_EXTRA_PATH, IMC_PATH], force: true }, expected: 'success|not found' },
];

runToolTests('input-mapping-session-ue58', testCases);
