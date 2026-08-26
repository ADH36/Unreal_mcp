#!/usr/bin/env node
/**
 * Landscape + spline environment authoring integration tests (UE 5.8).
 * Covers heightmap generation with cancel opt-out, erosion, rule-based paint,
 * landscape resize exclusions, region sculpting, path splines from route
 * points, spline discovery, segment rebuilds, and building storefront material.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const LANDSCAPE_PATH = '/Game/MCP_Test/L_MCPArena';
const PATH_SPLINE_ACTOR = `MCP_PathSpline_${ts}`;

const testCases = [
  // === LANDSCAPE HEIGHTMAP / SIMULATION ===
  { scenario: 'HEIGHTMAP: generate_landscape_heightmap with resolution and cancel opt-out', toolName: 'build_environment', arguments: { action: 'generate_landscape_heightmap', landscapePath: LANDSCAPE_PATH, resolutionX: 505, resolutionY: 505, terrainFeature: 'hills', cancel: false }, expected: 'success|not found' },
  { scenario: 'EROSION: apply_landscape_erosion iterations and frequency', toolName: 'build_environment', arguments: { action: 'apply_landscape_erosion', landscapePath: LANDSCAPE_PATH, iterations: 12, frequency: 0.05 }, expected: 'success|not found' },

  // === LANDSCAPE PAINT / EDIT ===
  { scenario: 'PAINT: paint_landscape_by_rule maskType blend targetHeight terrainFeature', toolName: 'build_environment', arguments: { action: 'paint_landscape_by_rule', landscapePath: LANDSCAPE_PATH, layerInfoPath: '/Game/MCP_Test/LI_MCPRule', maskType: 'height', blendType: 'AlphaBlend', targetHeight: 250, terrainFeature: 'hills' }, expected: 'success|not found' },
  { scenario: 'RESIZE: resize_landscape excluding clearance actors via excludedActors', toolName: 'build_environment', arguments: { action: 'resize_landscape', landscapePath: LANDSCAPE_PATH, excludedActors: [`MCP_RoadClearance_${ts}`] }, expected: 'success|not found' },
  { scenario: 'SCULPT: sculpt_landscape_region raises surfaceOffset inside region bounds', toolName: 'build_environment', arguments: { action: 'sculpt_landscape_region', landscapePath: LANDSCAPE_PATH, surfaceOffset: 50, region: { minX: 0, minY: 0, maxX: 2000, maxY: 2000 } }, expected: 'success|not found' },

  // === SPLINE AUTHORING / DISCOVERY ===
  { scenario: 'SPLINE: create_path_spline from ordered routePoints', toolName: 'build_environment', arguments: { action: 'create_path_spline', actorName: PATH_SPLINE_ACTOR, routePoints: [{ x: -500, y: 0, z: 100 }, { x: 0, y: 150, z: 100 }, { x: 500, y: 0, z: 100 }], coordinateSpace: 'World' }, expected: 'success|already exists' },
  { scenario: 'SPLINE: find_spline_actors lists spline actors in the level', toolName: 'build_environment', arguments: { action: 'find_spline_actors' }, expected: 'success' },
  { scenario: 'SPLINE: find_spline_components lists spline components', toolName: 'build_environment', arguments: { action: 'find_spline_components' }, expected: 'success' },
  { scenario: 'SPLINE: rebuild_spline_mesh_segments regenerates segment meshes', toolName: 'build_environment', arguments: { action: 'rebuild_spline_mesh_segments', actorName: PATH_SPLINE_ACTOR }, expected: 'success|not found' },

  // === PROCEDURAL BUILDING STOREFRONT ===
  { scenario: 'BUILDING: generate_procedural_building shop uses storefrontMaterial', toolName: 'build_environment', arguments: { action: 'generate_procedural_building', name: `MCP_StorefrontBuilding_${ts}`, location: { x: 3000, y: 3000, z: 0 }, buildingType: 'shop', floors: 1, storefrontMaterial: '/Engine/BasicShapes/BasicShapeMaterial' }, expected: 'success|not found' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete demo path spline actor', toolName: 'control_actor', arguments: { action: 'delete', actorName: PATH_SPLINE_ACTOR }, expected: 'success|not found' },
];

runToolTests('landscape-spline-authoring', testCases);
