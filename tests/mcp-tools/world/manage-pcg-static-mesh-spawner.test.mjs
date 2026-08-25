#!/usr/bin/env node

import { runToolTests } from '../../test-runner.mjs';

// Discover assets through the bridge. This suite must never guess a cube or
// silently replace a requested mesh.
const searchPaths = (process.env.MCP_PCG_SEARCH_PATHS ?? '/Engine/EditorMeshes|/Engine/EngineVolumetrics/FogEnvironment/Mesh|/Game')
  .split('|').map((value) => value.trim()).filter(Boolean);
const folder = '/Game/MCPRegression/PCGRealMesh';
const graphPath = `${folder}/PCG_RealMeshTest`;
const mapPath = `${folder}/PCG_RealMeshValidationMap`;
const volumeActor = 'MCP_PCG_RealMeshValidationVolume';
const volumeComponent = 'MCP_PCG_RealMeshValidationComponent';
const keepContent = process.env.MCP_PCG_KEEP_CONTENT === '1';
const volumeNode = 'MCP_PCG_RealMesh_PointGrid';
const transformNode = 'MCP_PCG_RealMesh_TransformPoints';
const spawnerNode = 'MCP_PCG_RealMesh_StaticMeshSpawner';
const expected = { successPattern: 'PCG' };
const assetExpected = { successPattern: 'success' };
const cubePath = '/Engine/BasicShapes/Cube.Cube';
const meshEntry = (meshPath, weight) => ({
  meshPath,
  weight,
  descriptorSettings: { ComponentClass: '/Script/Engine.HierarchicalInstancedStaticMeshComponent' },
  collision: 'NoCollision'
});

await runToolTests('manage-pcg-static-mesh-spawner', [
  {
    scenario: 'DISCOVER: first suitable non-cube environment mesh', toolName: 'manage_pcg',
    arguments: { action: 'search_static_mesh_assets', searchPaths, packagePaths: searchPaths, query: '', suitableOnly: true, allowFallbackMesh: false, limit: 1, offset: 0 },
    expected: assetExpected,
    assertions: [
      { path: 'structuredContent.result.count', greaterThan: 0, label: 'first real environment mesh found' },
      { path: 'structuredContent.result.assets.0.fallback', equals: false, label: 'first candidate is not fallback' },
      { path: 'structuredContent.result.assets.0.environmentCandidate', equals: true, label: 'first candidate is environment-like' }
    ],
    captureResult: { key: 'mesh0', fromField: 'result.assets.0.meshPath' }
  },
  {
    scenario: 'DISCOVER: second suitable non-cube environment mesh', toolName: 'manage_pcg',
    arguments: { action: 'search_static_mesh_assets', searchPaths, suitableOnly: true, allowFallbackMesh: false, limit: 1, offset: 1 },
    expected: assetExpected,
    assertions: [{ path: 'structuredContent.result.count', greaterThan: 0, label: 'second real environment mesh found' }],
    captureResult: { key: 'mesh1', fromField: 'result.assets.0.meshPath' }
  },
  {
    scenario: 'DISCOVER: third suitable non-cube environment mesh', toolName: 'manage_pcg',
    arguments: { action: 'search_static_mesh_assets', searchPaths, suitableOnly: true, allowFallbackMesh: false, limit: 1, offset: 2 },
    expected: assetExpected,
    assertions: [{ path: 'structuredContent.result.count', greaterThan: 0, label: 'third real environment mesh found' }],
    captureResult: { key: 'mesh2', fromField: 'result.assets.0.meshPath' }
  },
  {
    scenario: 'VALIDATE: reject cube fallback by default', toolName: 'manage_pcg',
    arguments: { action: 'validate_static_mesh_assets', meshPaths: [cubePath], allowFallbackMesh: false, allowCube: false },
    expected: assetExpected,
    assertions: [
      { path: 'structuredContent.result.valid', equals: false, label: 'cube rejected' },
      { path: 'structuredContent.result.invalidCount', greaterThan: 0, label: 'cube rejection reported' }
    ]
  },
  {
    scenario: 'SETUP: create exact regression folder', toolName: 'manage_asset',
    arguments: { action: 'create_folder', path: folder }, expected: 'success|already exists'
  },
  {
    scenario: 'SETUP: create and save exact validation map', toolName: 'manage_level',
    arguments: { action: 'create_level', levelPath: mapPath, useWorldPartition: false, saveDirtyPackages: true }, expected: 'success|already exists'
  },
  {
    scenario: 'SETUP: load exact validation map', toolName: 'control_editor',
    arguments: { action: 'open_level', levelPath: mapPath }, expected: 'success'
  },
  {
    scenario: 'SETUP: create exact PCG graph', toolName: 'manage_pcg',
    arguments: { action: 'create_pcg_graph', graphPath, overwrite: true, save: true }, expected,
    assertions: [{ path: 'structuredContent.result.assetPath', equals: graphPath, label: 'exact graph path' }]
  },
  {
    scenario: 'SETUP: spawn bounded PCG volume', toolName: 'control_actor',
    arguments: { action: 'spawn', classPath: '/Script/PCG.PCGVolume', actorName: volumeActor, location: { x: 0, y: 0, z: 0 }, scale: { x: 12, y: 12, z: 4 } },
    expected: 'success|already exists'
  },
  {
    scenario: 'CREATE: add deterministic multi-point PCG grid', toolName: 'manage_pcg',
    arguments: {
      action: 'add_pcg_node', graphPath, nodeName: volumeNode, settingsClass: 'PCGCreatePointsGridSettings',
      settings: { GridExtents: { x: 500, y: 500, z: 50 }, CellSize: { x: 100, y: 100, z: 100 } },
      x: 160, y: 160, save: false
    }, expected,
    captureResult: { key: 'volumeNodeId', fromField: 'result.nodeId' }
  },
  {
    scenario: 'CREATE: add seeded randomized scale rotation and offset', toolName: 'manage_pcg',
    arguments: {
      action: 'add_transform_points', graphPath, nodeName: transformNode, seed: 24681357, deterministic: true,
      scaleMin: [0.75, 0.75, 0.75], scaleMax: [1.25, 1.25, 1.25], rotationMin: [0, 0, 0], rotationMax: [0, 0, 360],
      offsetMin: [-50, -50, 0], offsetMax: [50, 50, 20], x: 360, y: 160, save: false
    }, expected
  },
  {
    scenario: 'CREATE: add Static Mesh Spawner node', toolName: 'manage_pcg',
    arguments: { action: 'add_static_mesh_spawner', graphPath, nodeName: spawnerNode, x: 560, y: 160, save: false }, expected,
    captureResult: { key: 'spawnerNodeId', fromField: 'result.nodeId' }
  },
  {
    scenario: 'CONFIGURE: assign three weighted real meshes and HISM descriptors', toolName: 'manage_pcg',
    arguments: {
      action: 'configure_static_mesh_spawner', graphPath, nodeId: '${captured:spawnerNodeId}', meshSelectorType: 'weighted',
      meshEntries: [meshEntry('${captured:mesh0}', 1), meshEntry('${captured:mesh1}', 2), meshEntry('${captured:mesh2}', 4)],
      allowFallbackMesh: false, save: true
    }, expected,
    assertions: [
      { path: 'structuredContent.result.meshEntries', length: 3, label: 'three configured entries' },
      { path: 'structuredContent.result.meshEntries.0.descriptorSettings.ComponentClass', equals: '/Script/Engine.HierarchicalInstancedStaticMeshComponent', label: 'HISM descriptor persisted' },
      { path: 'structuredContent.result.saved', equals: true, label: 'graph saved after configuration' }
    ]
  },
  {
    scenario: 'VERIFY: find configured Static Mesh Spawner', toolName: 'manage_pcg',
    arguments: { action: 'find_static_mesh_spawner', graphPath, nodeId: '${captured:spawnerNodeId}' }, expected,
    assertions: [{ path: 'structuredContent.result.meshEntries', length: 3, label: 'find returns configured entries' }]
  },
  {
    scenario: 'CONFIGURE: add temporary weighted mesh entry', toolName: 'manage_pcg',
    arguments: {
      action: 'add_static_mesh_entry', graphPath, nodeId: '${captured:spawnerNodeId}',
      entry: meshEntry('${captured:mesh0}', 8), staticMesh: '${captured:mesh0}', weight: 8,
      descriptorSettings: { ComponentClass: '/Script/Engine.HierarchicalInstancedStaticMeshComponent' },
      collision: 'NoCollision', save: false
    }, expected,
    captureResult: { key: 'temporaryEntryIndex', fromField: 'result.entryIndex' },
    assertions: [{ path: 'structuredContent.result.meshEntries', length: 4, label: 'temporary entry added' }]
  },
  {
    scenario: 'CONFIGURE: update temporary weighted mesh entry', toolName: 'manage_pcg',
    arguments: {
      action: 'update_static_mesh_entry', graphPath, nodeId: '${captured:spawnerNodeId}',
      entryIndex: '${captured:temporaryEntryIndex}', meshPath: '${captured:mesh0}', weight: 9,
      descriptorSettings: { ComponentClass: '/Script/Engine.HierarchicalInstancedStaticMeshComponent' },
      collision: 'NoCollision', save: false
    }, expected,
    assertions: [{ path: 'structuredContent.result.meshEntries', length: 4, label: 'temporary entry updated' }]
  },
  {
    scenario: 'CONFIGURE: remove temporary weighted mesh entry', toolName: 'manage_pcg',
    arguments: { action: 'remove_static_mesh_entry', graphPath, nodeId: '${captured:spawnerNodeId}', entryIndex: '${captured:temporaryEntryIndex}', save: true }, expected,
    assertions: [{ path: 'structuredContent.result.meshEntries', length: 3, label: 'configured entries restored after removal' }]
  },
  {
    scenario: 'CONNECT: volume sampler to transform points', toolName: 'manage_pcg',
    arguments: { action: 'connect_pcg_pins', graphPath, sourceNodeId: '${captured:volumeNodeId}', targetNodeId: transformNode, sourcePin: 'Out', targetPin: 'In', save: false }, expected
  },
  {
    scenario: 'CONNECT: transform points to spawner', toolName: 'manage_pcg',
    arguments: { action: 'connect_pcg_pins', graphPath, sourceNodeId: transformNode, targetNodeId: '${captured:spawnerNodeId}', sourcePin: 'Out', targetPin: 'In', save: false }, expected
  },
  {
    scenario: 'CONNECT: spawner to output and save graph connections', toolName: 'manage_pcg',
    arguments: { action: 'connect_pcg_pins', graphPath, sourceNodeId: '${captured:spawnerNodeId}', targetNodeId: 'output', sourcePin: 'Out', targetPin: 'Out', save: true }, expected,
    assertions: [{ path: 'structuredContent.result.saved', equals: true, label: 'connections saved' }]
  },
  {
    scenario: 'EXECUTE: generate and wait for materialized output', toolName: 'manage_pcg',
    arguments: { action: 'execute_pcg_graph', graphPath, actorName: volumeActor, componentName: volumeComponent, createComponent: true, force: true, wait: true, save: true, timeoutMs: 120000 }, expected,
    captureResult: { key: 'componentPath', fromField: 'result.componentPath' },
    assertions: [
      { path: 'structuredContent.result.waited', equals: true, label: 'generation awaited' },
      { path: 'structuredContent.result.materialized', equals: true, label: 'output materialized' },
      { path: 'structuredContent.result.instanceCount', greaterThan: 0, label: 'instances materialized' },
      { path: 'structuredContent.result.hismInstanceCount', greaterThan: 0, label: 'HISM instances materialized' },
      { path: 'structuredContent.result.transformsWithinBounds', equals: true, label: 'instance transforms are within component bounds' },
      { path: 'structuredContent.result.hasFallbackMesh', equals: false, label: 'no fallback materialized' },
      { path: 'structuredContent.result.actualMeshPackagePaths', includesCaptured: 'mesh0', label: 'first requested mesh materialized' },
      { path: 'structuredContent.result.actualMeshPackagePaths', includesCaptured: 'mesh1', label: 'second requested mesh materialized' },
      { path: 'structuredContent.result.actualMeshPackagePaths', includesCaptured: 'mesh2', label: 'third requested mesh materialized' },
      { path: 'structuredContent.result.actualMeshPaths', notIncludes: cubePath, label: 'actual meshes exclude cube' }
    ]
  },
  {
    scenario: 'VERIFY: read actual HISM meshes and transforms', toolName: 'manage_pcg',
    arguments: { action: 'read_pcg_generated_instances', actorName: volumeActor, componentPath: '${captured:componentPath}' }, expected,
    captureResult: { key: 'firstTransformHash', fromField: 'result.transformHash' },
    assertions: [
      { path: 'structuredContent.result.materialized', equals: true, label: 'readback materialized' },
      { path: 'structuredContent.result.instances.0.componentType', equals: 'HISM', label: 'actual component HISM' },
      { path: 'structuredContent.result.instances.0.transforms', minLength: 1, label: 'component reports actual transforms' }
    ]
  },
  {
    scenario: 'VERIFY: inspect entries and preserved graph connections', toolName: 'manage_pcg',
    arguments: { action: 'inspect_static_mesh_spawner', graphPath, nodeId: '${captured:spawnerNodeId}' }, expected,
    assertions: [
      { path: 'structuredContent.result.meshEntries', length: 3, label: 'entries persist' },
      { path: 'structuredContent.result.connectionCount', greaterThan: 0, label: 'connections persist after entry updates' }
    ]
  },
  {
    scenario: 'REGENERATE: same seed produces identical hash', toolName: 'manage_pcg',
    arguments: { action: 'regenerate_pcg_component', actorName: volumeActor, componentPath: '${captured:componentPath}', force: true, wait: true, save: false, timeoutMs: 120000 }, expected,
    assertions: [{ path: 'structuredContent.result.transformHash', equalsCaptured: 'firstTransformHash', label: 'deterministic transform hash' }]
  },
  {
    scenario: 'CLEAR: remove generated output', toolName: 'manage_pcg',
    arguments: { action: 'clear_pcg_generated_output', actorName: volumeActor, componentPath: '${captured:componentPath}', save: true }, expected,
    assertions: [
      { path: 'structuredContent.result.instanceCount', equals: 0, label: 'zero instances after clear' },
      { path: 'structuredContent.result.materialized', equals: false, label: 'no materialized output after clear' }
    ]
  },
  {
    scenario: 'REGENERATE: change seed setting', toolName: 'manage_pcg',
    arguments: { action: 'set_pcg_node_settings', graphPath, nodeId: transformNode, settings: { Seed: 975318642 }, save: true }, expected
  },
  {
    scenario: 'REGENERATE: different seed changes transform hash', toolName: 'manage_pcg',
    arguments: { action: 'regenerate_pcg_component', actorName: volumeActor, componentPath: '${captured:componentPath}', force: true, wait: true, save: true, timeoutMs: 120000 }, expected,
    assertions: [
      { path: 'structuredContent.result.materialized', equals: true, label: 'different-seed output materialized' },
      { path: 'structuredContent.result.transformHash', notEqualsCaptured: 'firstTransformHash', label: 'different seed changes transforms' }
    ]
  },
  {
    scenario: 'PERSIST: reload saved validation map', toolName: 'manage_level',
    arguments: { action: 'load_level', levelPath: mapPath, saveDirtyPackages: true }, expected: 'success'
  },
  {
    scenario: 'PERSIST: graph entries survive map reload', toolName: 'manage_pcg',
    arguments: { action: 'inspect_static_mesh_spawner', graphPath, nodeId: '${captured:spawnerNodeId}' }, expected,
    assertions: [{ path: 'structuredContent.result.meshEntries', length: 3, label: 'entries persist after reload' }]
  },
  {
    scenario: 'PERSIST: materialized HISM output survives map reload', toolName: 'manage_pcg',
    arguments: { action: 'read_pcg_generated_instances', actorName: volumeActor, componentName: volumeComponent }, expected,
    assertions: [
      { path: 'structuredContent.result.hismInstanceCount', greaterThan: 0, label: 'reloaded HISM output' },
      { path: 'structuredContent.result.materialized', equals: true, label: 'reloaded output materialized' },
      { path: 'structuredContent.result.hasFallbackMesh', equals: false, label: 'reloaded output no fallback' }
    ]
  },
  {
    scenario: 'CLEANUP: delete temporary validation actor', toolName: 'control_actor',
    arguments: { action: 'delete', actorName: keepContent ? `${volumeActor}_PRESERVE_FOR_RESTART` : volumeActor }, expected: 'success|not found'
  },
  {
    scenario: 'CLEANUP: delete temporary content after reporting', toolName: 'manage_asset',
    arguments: { action: 'delete', path: keepContent ? `${folder}_PRESERVE_FOR_RESTART` : folder, force: true }, expected: 'success|not found'
  }
]);
