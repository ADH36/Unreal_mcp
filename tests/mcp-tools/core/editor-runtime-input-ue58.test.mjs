#!/usr/bin/env node
/**
 * control_editor runtime input & capture integration tests (UE 5.8).
 * Covers send_input (axis + playerIndex), send_enhanced_input, the axisX move
 * variant, new screenshot capture knobs, playtest autoStop, standalone log
 * tailing, interface-scoped interact, and the reserved saveRuntimeChanges flag.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const SCREENSHOT_NAME = `MCP_EditorInput_${ts}`;

const testCases = [
  // === ENHANCED / RAW INPUT DISPATCH ===
  { scenario: 'INPUT: send_enhanced_input triggers an enhanced action with value', toolName: 'control_editor', arguments: { action: 'send_enhanced_input', key: 'SpaceBar', enhancedAction: 'IA_Jump', value: 1, playerIndex: 0 }, expected: 'success|not found' },
  { scenario: 'INPUT: send_input axis variant with relative axis value and playerIndex', toolName: 'control_editor', arguments: { action: 'send_input', type: 'axis', axisName: 'MoveForward', axisValue: 0.5, relative: false, playerIndex: 0 }, expected: 'success|not in pie' },
  { scenario: 'INPUT: move uses axisX analog displacement', toolName: 'control_editor', arguments: { action: 'move', key: 'W', axisX: 1, durationMs: 120, playerIndex: 0 }, expected: 'success|not in pie' },
  { scenario: 'INPUT: interact checks interfaceName on hit actor', toolName: 'control_editor', arguments: { action: 'interact', key: 'E', durationMs: 100, playerIndex: 0, interfaceName: 'MCPInteractionInterface' }, expected: 'success|not in pie' },

  // === CAPTURE / DIAGNOSTICS ===
  { scenario: 'CAPTURE: screenshot honors captureMode warmupFrames screenshotDelayMs and outputPath', toolName: 'control_editor', arguments: { action: 'screenshot', filename: SCREENSHOT_NAME, captureMode: 'game_viewport', warmupFrames: 2, screenshotDelayMs: 100, outputPath: `tmp/unreal-mcp/${SCREENSHOT_NAME}`, returnBase64: false }, expected: 'success' },
  { scenario: 'INFO: read_pie_logs tails standalone process log via standalone flag', toolName: 'control_editor', arguments: { action: 'read_pie_logs', standalone: true }, expected: 'success' },

  // === BOUNDED PLAYTEST SEQUENCE ===
  { scenario: 'PLAYTEST: run_playtest_sequence with empty probe steps and autoStop', toolName: 'control_editor', arguments: { action: 'run_playtest_sequence', sequence: [{ action: 'get_pie_state' }], autoStop: true }, expected: 'success|not found' },

  // === STANDALONE PLAY SESSION ===
  { scenario: 'PIE: play launches standalone session targeting pawnName and playerStart', toolName: 'control_editor', arguments: { action: 'play', pieMode: 'standalone', pawnName: `MCP_InputPawn_${ts}`, playerStart: 'PlayerStart' }, expected: 'success' },

  // === CLEANUP ===
  { scenario: 'CLEANUP: stop_pie leaves runtime state unsaved via saveRuntimeChanges opt-out', toolName: 'control_editor', arguments: { action: 'stop_pie', saveRuntimeChanges: false }, expected: 'success' },
];

runToolTests('editor-runtime-input-ue58', testCases);
