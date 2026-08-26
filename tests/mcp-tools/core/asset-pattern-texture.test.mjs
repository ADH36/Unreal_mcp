#!/usr/bin/env node
/**
 * Procedural pattern texture integration tests.
 * Covers every create_pattern_texture tuning parameter: patternType, color
 * pair, tile counts, line width, and brick ratio, written under /Game/MCP_Test.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const TEST_FOLDER = '/Game/MCP_Test';
const PATTERN_NAME = `T_MCPBrick_${ts}`;

const testCases = [
  // === AUTHORING ===
  { scenario: 'TEXTURE: create_pattern_texture brick variant with full tuning set', toolName: 'manage_asset', arguments: { action: 'create_pattern_texture', name: PATTERN_NAME, path: TEST_FOLDER, patternType: 'Brick', width: 512, height: 512, primaryColor: { r: 0.72, g: 0.32, b: 0.2, a: 1 }, secondaryColor: { r: 0.85, g: 0.82, b: 0.78, a: 1 }, tilesX: 8, tilesY: 8, lineWidth: 0.03, brickRatio: 2 }, expected: 'success|already exists' },
  { scenario: 'TEXTURE: create_pattern_texture grid variant reuses color and tiling parameters', toolName: 'manage_asset', arguments: { action: 'create_pattern_texture', name: `T_MCPGrid_${ts}`, path: TEST_FOLDER, patternType: 'Grid', primaryColor: { r: 0.1, g: 0.4, b: 0.7, a: 1 }, secondaryColor: { r: 1, g: 1, b: 1, a: 1 }, tilesX: 16, tilesY: 16, lineWidth: 0.02, brickRatio: 1 }, expected: 'success|already exists' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete generated pattern textures', toolName: 'manage_asset', arguments: { action: 'delete_assets', paths: [`${TEST_FOLDER}/${PATTERN_NAME}`, `${TEST_FOLDER}/T_MCPGrid_${ts}`], force: true }, expected: 'success|not found' },
];

runToolTests('asset-pattern-texture', testCases);
