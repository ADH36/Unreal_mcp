#!/usr/bin/env node
/**
 * UE 5.8 regression: a MCP-created NavMeshBoundsVolume must own real brush
 * polygons and retain nonzero bounds after a save/reload cycle.
 */
import { runToolTests } from '../../test-runner.mjs';

const stamp = Date.now();
const volume = `MCP_NavVolumeReload_${stamp}`;
const level = '/Game/AstraVale/Maps/AstraVale_OpenWorld/AstraVale_OpenWorld';

runToolTests('nav-volume-reload-regression-ue58', [
  { scenario: 'Create volume with editor brush', toolName: 'manage_level_structure', arguments: { action: 'create_nav_mesh_bounds_volume', volumeName: volume, location: { x: 0, y: 0, z: 250 }, extent: { x: 2500, y: 2500, z: 500 } }, expected: 'success', assertions: [{ path: 'structuredContent.boundsExtentX', predicate: value => Number(value) > 0, label: 'created X extent is nonzero' }, { path: 'structuredContent.boundsExtentY', predicate: value => Number(value) > 0, label: 'created Y extent is nonzero' }, { path: 'structuredContent.boundsExtentZ', predicate: value => Number(value) > 0, label: 'created Z extent is nonzero' }] },
  { scenario: 'Save level', toolName: 'manage_level', arguments: { action: 'save', levelPath: level }, expected: 'success' },
  { scenario: 'Reload level', toolName: 'manage_level', arguments: { action: 'load', levelPath: level }, expected: 'success' },
  { scenario: 'Read nonzero bounds after reload', toolName: 'inspect', arguments: { action: 'get_bounding_box', actorName: volume }, expected: 'success', assertions: [{ path: 'structuredContent.data.extent.0', predicate: value => Number(value) > 0, label: 'reloaded X bounds are nonzero' }, { path: 'structuredContent.data.extent.1', predicate: value => Number(value) > 0, label: 'reloaded Y bounds are nonzero' }, { path: 'structuredContent.data.extent.2', predicate: value => Number(value) > 0, label: 'reloaded Z bounds are nonzero' }] },
  { scenario: 'Cleanup: remove volume', toolName: 'control_actor', arguments: { action: 'delete', actorName: volume }, expected: 'success|not found' },
]);
