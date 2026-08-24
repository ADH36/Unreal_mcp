#!/usr/bin/env node

import { runToolTests } from '../../test-runner.mjs';

// This suite intentionally uses a throwaway map and never saves while PIE is active.
const stamp = Date.now();
const folder = `/Game/MCPTest/PIEPlaytest_${stamp}`;
const mapPath = `${folder}/PIEPlaytestMap`;
const playerStart = `MCP_PlayerStart_${stamp}`;
const pawn = `MCP_PlaytestPawn_${stamp}`;

const testCases = [
  { scenario: 'Setup: create throwaway PIE test folder', toolName: 'manage_asset', arguments: { action: 'create_folder', path: folder }, expected: 'success|already exists' },
  { scenario: 'Setup: create and open throwaway map', toolName: 'manage_level', arguments: { action: 'create_level', levelPath: mapPath }, expected: 'success|already exists' },
  { scenario: 'Setup: open throwaway map', toolName: 'control_editor', arguments: { action: 'open_level', levelPath: mapPath }, expected: 'success' },
  { scenario: 'Setup: add PlayerStart', toolName: 'control_actor', arguments: { action: 'spawn', classPath: '/Script/Engine.PlayerStart', actorName: playerStart, location: { x: 0, y: 0, z: 120 } }, expected: 'success' },
  { scenario: 'Setup: add playable pawn', toolName: 'control_actor', arguments: { action: 'spawn', classPath: '/Script/Engine.DefaultPawn', actorName: pawn, location: { x: 100, y: 0, z: 120 } }, expected: 'success' },
  { scenario: 'PIE: start viewport PIE', toolName: 'control_editor', arguments: { action: 'start_pie', pieMode: 'viewport', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: verify distinct PIE and editor worlds', toolName: 'control_editor', arguments: { action: 'get_pie_state', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: possess the PIE pawn', toolName: 'control_editor', arguments: { action: 'possess', actorName: pawn, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: capture before screenshot', toolName: 'control_editor', arguments: { action: 'capture_pie_screenshot', filename: `before_${stamp}.png`, returnBase64: true, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: query pawn baseline', toolName: 'control_editor', arguments: { action: 'query_pie_actor', actorName: pawn, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: movement input', toolName: 'control_editor', arguments: { action: 'move', actorName: pawn, axisY: 1, durationMs: 250, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: look input', toolName: 'control_editor', arguments: { action: 'look', x: 32, y: 16, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: jump input', toolName: 'control_editor', arguments: { action: 'jump', durationMs: 100, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: sprint input', toolName: 'control_editor', arguments: { action: 'sprint', durationMs: 250, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: interaction input', toolName: 'control_editor', arguments: { action: 'interact', durationMs: 100, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: capture after screenshot', toolName: 'control_editor', arguments: { action: 'capture_pie_screenshot', filename: `after_${stamp}.png`, returnBase64: true, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: detect deliberately blocked movement', toolName: 'control_editor', arguments: { action: 'detect_pie_issues', actorName: pawn, expectedMovement: true, previousLocation: { x: 100, y: 0, z: 120 }, minMovementCm: 1000000, timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: record metrics', toolName: 'control_editor', arguments: { action: 'get_pie_metrics', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: read redacted runtime warnings', toolName: 'control_editor', arguments: { action: 'read_pie_logs', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: stop cleanly', toolName: 'control_editor', arguments: { action: 'stop_pie', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: verify no PIE world remains', toolName: 'control_editor', arguments: { action: 'get_pie_state', timeoutMs: 30000 }, expected: 'success' },
  { scenario: 'PIE: forced sequence failure performs cleanup', toolName: 'control_editor', arguments: { action: 'run_playtest_sequence', timeoutMs: 100, sequence: [{ action: 'get_pie_state' }, { action: 'unknown_playtest_action' }] }, expected: 'error|PLAYTEST_FAILED' },
  { scenario: 'Cleanup: delete throwaway test content', toolName: 'manage_asset', arguments: { action: 'delete', path: folder, force: true }, expected: 'success|not found' }
];

runToolTests('ue58-playtest', testCases);
