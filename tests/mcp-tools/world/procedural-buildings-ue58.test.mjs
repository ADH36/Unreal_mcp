#!/usr/bin/env node
/**
 * UE 5.8 procedural building regression: optimized HISM facade generation,
 * deterministic regeneration, road clearance, persistence and cleanup.
 * Run against a UE 5.8.1 editor: node tests/mcp-tools/world/procedural-buildings-ue58.test.mjs
 */
import { runToolTests } from '../../test-runner.mjs';

const stamp = Date.now();
const folder = `/Game/MCPTest/ProceduralBuildings_${stamp}`;
const road = `MCP_BuildingRoad_${stamp}`;
const house = `MCP_House_${stamp}`;
const shop = `MCP_Shop_${stamp}`;
const apartment = `MCP_Apartment_${stamp}`;
const skyscraper = `MCP_Skyscraper_${stamp}`;
const block = `MCP_Block_${stamp}`;
const materials = {
  wallMaterial: `${folder}/M_Wall_${stamp}`,
  windowMaterial: `${folder}/M_Window_${stamp}`,
  roofMaterial: `${folder}/M_Roof_${stamp}`,
  trimMaterial: `${folder}/M_Trim_${stamp}`,
  interiorMaterial: `${folder}/M_Interior_${stamp}`
};

const base = {
  action: 'generate_procedural_building', floorHeight: 330, wallThickness: 24,
  generateDoors: true, generateWindows: true, generateInterior: true,
  useHISM: true, generateLODs: true, enableNanite: true, ...materials
};

runToolTests('procedural-buildings-ue58', [
  { scenario: 'Setup: create temporary building content folder', toolName: 'manage_asset', arguments: { action: 'create_folder', path: folder }, expected: 'success|already exists' },
  ...Object.entries(materials).map(([, path]) => ({ scenario: `Setup: create ${path.split('/').pop()}`, toolName: 'manage_asset', arguments: { action: 'create_material', name: path.split('/').pop(), path: folder }, expected: 'success|already exists' })),
  { scenario: 'Setup: create temporary road boundary spline', toolName: 'build_environment', arguments: { action: 'create_road_spline', actorName: road, coordinateSpace: 'World', points: [{ x: 0, y: 5000, z: 0 }, { x: 10000, y: 5000, z: 0 }], width: 400 }, expected: 'success' },

  { scenario: 'Generate: house with footprint points and gable roof', toolName: 'build_environment', arguments: { ...base, buildingType: 'house', buildingName: house, roofType: 'gable', seed: 1101, footprintPoints: [{ x: 0, y: 0, z: 0 }, { x: 900, y: 0, z: 0 }, { x: 900, y: 700, z: 0 }, { x: 0, y: 700, z: 0 }] }, expected: 'success', assertions: [{ path: 'structuredContent.result.entranceClear', equals: true, label: 'house entrance is clear' }, { path: 'structuredContent.result.usesHISM', equals: true, label: 'house facade uses HISM' }, { path: 'structuredContent.result.collisionComponentCount', greaterThan: 0, label: 'house has collision' }] },
  { scenario: 'Generate: shop with storefront and flat roof', toolName: 'build_environment', arguments: { ...base, buildingType: 'shop', buildingName: shop, location: { x: 1800, y: 0, z: 0 }, width: 1100, depth: 700, floors: 2, roofType: 'flat', generateStorefront: true, seed: 1102 }, expected: 'success' },
  { scenario: 'Generate: apartment with balconies and hip roof', toolName: 'build_environment', arguments: { ...base, buildingType: 'apartment', buildingName: apartment, location: { x: 3800, y: 0, z: 0 }, width: 1300, depth: 850, floors: 5, roofType: 'hip', generateBalconies: true, seed: 1103 }, expected: 'success' },
  { scenario: 'Generate: skyscraper with many floors', toolName: 'build_environment', arguments: { ...base, buildingType: 'skyscraper', buildingName: skyscraper, location: { x: 6200, y: 0, z: 0 }, width: 1500, depth: 1100, floors: 20, roofType: 'flat', seed: 1104 }, expected: 'success' },

  { scenario: 'Inspect: skyscraper verifies HISM, material slots, collision and dimensions', toolName: 'build_environment', arguments: { action: 'inspect_procedural_building', buildingActor: skyscraper }, expected: 'success', assertions: [{ path: 'structuredContent.result.hismComponentCount', greaterThan: 1, label: 'repeated elements are consolidated' }, { path: 'structuredContent.result.repeatedElementInstances', greaterThan: 20, label: 'facade instances created' }, { path: 'structuredContent.result.collisionComponentCount', greaterThan: 0, label: 'collision components present' }, { path: 'structuredContent.result.entranceClear', equals: true, label: 'entrance remains unobstructed' }] },
  { scenario: 'Regenerate: house retains deterministic seed output', toolName: 'build_environment', arguments: { ...base, action: 'regenerate_procedural_building', buildingActor: house, buildingName: house, seed: 1101 }, expected: 'success', assertions: [{ path: 'structuredContent.result.seed', equals: 1101, label: 'seed preserved on regeneration' }, { path: 'structuredContent.result.usesHISM', equals: true, label: 'regenerated building stays optimized' }] },
  { scenario: 'Save: building blueprint survives asset save', toolName: 'build_environment', arguments: { action: 'save_procedural_building_blueprint', buildingActor: house, blueprintPath: folder }, expected: 'success', assertions: [{ path: 'structuredContent.result.saved', equals: true, label: 'building Blueprint saved' }, { path: 'structuredContent.result.assetType', equals: 'Blueprint', label: 'Blueprint conversion used' }] },
  { scenario: 'Reload: save editor state then re-inspect deterministic house', toolName: 'control_editor', arguments: { action: 'save_all' }, expected: 'success' },
  { scenario: 'Reload: inspect saved house after save', toolName: 'build_environment', arguments: { action: 'inspect_procedural_building', buildingActor: house }, expected: 'success', assertions: [{ path: 'structuredContent.result.usesHISM', equals: true, label: 'saved building retained HISM components' }, { path: 'structuredContent.result.entranceClear', equals: true, label: 'saved building entrance remains clear' }] },

  { scenario: 'Generate: city block alongside road with clearance and non-overlapping footprints', toolName: 'build_environment', arguments: { ...base, action: 'generate_city_block', buildingType: 'office', buildingName: block, roadSplineActor: road, roadClearance: 400, width: 700, depth: 600, floors: 4, maxBuildings: 6, seed: 1200 }, expected: 'success', assertions: [{ path: 'structuredContent.result.buildingCount', greaterThan: 0, label: 'city block generated' }, { path: 'structuredContent.result.zeroRoadOverlap', equals: true, label: 'road boundary is clear' }, { path: 'structuredContent.result.zeroOverlappingFootprints', equals: true, label: 'no city footprints overlap' }] },
  { scenario: 'PIE: short collision/navigation smoke', toolName: 'control_editor', arguments: { action: 'play' }, expected: 'success' },
  { scenario: 'PIE: stop collision/navigation smoke', toolName: 'control_editor', arguments: { action: 'stop_pie' }, expected: 'success' },

  { scenario: 'Cleanup: delete generated building actors', toolName: 'build_environment', arguments: { action: 'delete', names: [house, shop, apartment, skyscraper, road, `${block}_000`, `${block}_001`, `${block}_002`, `${block}_003`, `${block}_004`, `${block}_005`] }, expected: 'success' },
  { scenario: 'Cleanup: delete all temporary building content', toolName: 'manage_asset', arguments: { action: 'delete', path: folder, force: true }, expected: 'success|not found' }
]);
