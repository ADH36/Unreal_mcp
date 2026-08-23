#!/usr/bin/env node

import { runToolTests } from '../../test-runner.mjs';

// Supply three existing tree meshes as a pipe-separated list. Asset paths are
// intentionally not guessed: the handler must reject invalid meshes and never
// substitute a cube.
const treeMeshes = (process.env.MCP_PCG_TREE_MESHES ?? '')
  .split('|')
  .map((value) => value.trim())
  .filter(Boolean);

if (treeMeshes.length < 3) {
  console.warn('SKIP: set MCP_PCG_TREE_MESHES to three existing UStaticMesh paths separated by | to run the UE 5.8 PCG Static Mesh Spawner integration test.');
  process.exit(0);
}

const folder = '/Game/MCPTests/PCG';
const graphPath = `${folder}/PCG_MeshSpawnerTest`;
const volumeActor = 'MCP_PCG_MeshSpawnerTestVolume';
const surfaceNode = 'MCP_PCG_SurfaceSampler';
const transformNode = 'MCP_PCG_TransformPoints';
const spawnerNode = 'MCP_PCG_StaticMeshSpawner';
const weightedEntries = treeMeshes.slice(0, 3).map((meshPath, index) => ({
  meshPath,
  weight: index + 1,
  descriptorSettings: {
    ComponentClass: '/Script/Engine.HierarchicalInstancedStaticMeshComponent'
  },
  collision: 'NoCollision'
}));

const expected = { successPattern: 'PCG' };

await runToolTests('manage-pcg-static-mesh-spawner', [
  {
    scenario: 'SETUP: create exact PCG test folder',
    toolName: 'manage_asset',
    arguments: { action: 'create_folder', path: folder },
    expected: 'success|already exists'
  },
  {
    scenario: 'SETUP: create exact PCG mesh spawner graph',
    toolName: 'manage_pcg',
    arguments: { action: 'create_pcg_graph', graphPath, overwrite: true, save: true },
    expected,
    assertions: [{ path: 'structuredContent.result.assetPath', equals: graphPath, label: 'exact graph asset path' }]
  },
  {
    scenario: 'SETUP: spawn temporary PCG volume',
    toolName: 'control_actor',
    arguments: {
      action: 'spawn',
      classPath: '/Script/PCG.PCGVolume',
      actorName: volumeActor,
      location: { x: 0, y: 0, z: 0 },
      scale: { x: 8, y: 8, z: 8 }
    },
    expected: 'success|already exists'
  },
  {
    scenario: 'CREATE: add surface sampler node',
    toolName: 'manage_pcg',
    arguments: { action: 'add_surface_sampler', graphPath, nodeName: surfaceNode, x: 160, y: 160, save: false },
    expected,
    captureResult: { key: 'surfaceNodeId', fromField: 'result.nodeId' }
  },
  {
    scenario: 'CREATE: add seeded transform points node with variation',
    toolName: 'manage_pcg',
    arguments: {
      action: 'add_transform_points',
      graphPath,
      nodeName: transformNode,
      seed: 24681357,
      deterministic: true,
      scaleMin: [0.75, 0.75, 0.75],
      scaleMax: [1.25, 1.25, 1.25],
      rotationMin: [0, 0, 0],
      rotationMax: [0, 0, 360],
      offsetMin: [0, 0, 0],
      offsetMax: [0, 0, 0],
      x: 360,
      y: 160,
      save: false
    },
    expected
  },
  {
    scenario: 'CREATE: add Static Mesh Spawner node',
    toolName: 'manage_pcg',
    arguments: { action: 'add_static_mesh_spawner', graphPath, nodeName: spawnerNode, x: 560, y: 160, save: false },
    expected,
    captureResult: { key: 'spawnerNodeId', fromField: 'result.nodeId' }
  },
  {
    scenario: 'ERROR: reject invalid mesh without cube substitution',
    toolName: 'manage_pcg',
    arguments: {
      action: 'configure_static_mesh_spawner',
      graphPath,
      nodeId: '${captured:spawnerNodeId}',
      meshEntries: [{ meshPath: '/Game/MCPTests/PCG/DefinitelyMissingTreeMesh.DefinitelyMissingTreeMesh', weight: 1 }],
      save: false
    },
    expected: { errorPattern: 'INVALID_SETTINGS' }
  },
  {
    scenario: 'CONFIGURE: assign three reflected weighted tree entries and HISM descriptor',
    toolName: 'manage_pcg',
    arguments: {
      action: 'configure_static_mesh_spawner',
      graphPath,
      nodeId: '${captured:spawnerNodeId}',
      meshSelectorType: 'weighted',
      meshEntries: weightedEntries,
      save: true
    },
    expected,
    assertions: [
      { path: 'structuredContent.result.meshEntries', length: 3, label: 'three assigned mesh entries' },
      { path: 'structuredContent.result.meshEntries', includesObject: { meshPath: treeMeshes[0], weight: 1 }, label: 'first exact mesh path and weight' },
      { path: 'structuredContent.result.saved', equals: true, label: 'graph saved after configuration' }
    ]
  },
  {
    scenario: 'VERIFY: inspect exact reflected selector entries',
    toolName: 'manage_pcg',
    arguments: { action: 'inspect_static_mesh_spawner', graphPath, nodeId: '${captured:spawnerNodeId}' },
    expected,
    assertions: [
      { path: 'structuredContent.result.meshEntries', length: 3, label: 'three entries survive inspection' },
      { path: 'structuredContent.result.meshEntries', includesObject: { meshPath: treeMeshes[1], weight: 2 }, label: 'second exact mesh path and weight' },
      { path: 'structuredContent.result.meshEntries', includesObject: { meshPath: treeMeshes[2], weight: 3 }, label: 'third exact mesh path and weight' }
    ]
  },
  {
    scenario: 'CONNECT: graph input to surface sampler using reflected In pins',
    toolName: 'manage_pcg',
    arguments: {
      action: 'connect_pcg_pins',
      graphPath,
      sourceNodeId: 'input',
      targetNodeId: '${captured:surfaceNodeId}',
      sourcePin: 'In',
      targetPin: 'In',
      save: false
    },
    expected
  },
  {
    scenario: 'CONNECT: surface sampler to transform points using reflected Out/In pins',
    toolName: 'manage_pcg',
    arguments: {
      action: 'connect_pcg_pins',
      graphPath,
      sourceNodeId: '${captured:surfaceNodeId}',
      targetNodeId: transformNode,
      sourcePin: 'Out',
      targetPin: 'In',
      save: false
    },
    expected
  },
  {
    scenario: 'CONNECT: transform points to Static Mesh Spawner using reflected Out/In pins',
    toolName: 'manage_pcg',
    arguments: {
      action: 'connect_pcg_pins',
      graphPath,
      sourceNodeId: transformNode,
      targetNodeId: '${captured:spawnerNodeId}',
      sourcePin: 'Out',
      targetPin: 'In',
      save: false
    },
    expected
  },
  {
    scenario: 'CONNECT: Static Mesh Spawner to graph output using reflected Out pins',
    toolName: 'manage_pcg',
    arguments: {
      action: 'connect_pcg_pins',
      graphPath,
      sourceNodeId: '${captured:spawnerNodeId}',
      targetNodeId: 'output',
      sourcePin: 'Out',
      targetPin: 'Out',
      save: true
    },
    expected,
    assertions: [{ path: 'structuredContent.result.saved', equals: true, label: 'complete graph saved' }]
  },
  {
    scenario: 'EXECUTE: create PCG component on temporary volume',
    toolName: 'manage_pcg',
    arguments: {
      action: 'execute_pcg_graph',
      graphPath,
      actorName: volumeActor,
      componentName: 'MCP_PCG_MeshSpawnerComponent',
      createComponent: true,
      force: true,
      save: true,
      timeoutMs: 120000
    },
    expected,
    captureResult: { key: 'componentPath', fromField: 'result.componentPath' }
  },
  {
    scenario: 'GENERATE: regenerate component with explicit seed',
    toolName: 'manage_pcg',
    arguments: { action: 'regenerate_pcg_component', actorName: volumeActor, componentPath: '${captured:componentPath}', force: true, save: false },
    expected,
    assertions: [{ path: 'structuredContent.result.taskId', greaterThan: 0, label: 'regeneration task scheduled' }]
  },
  {
    scenario: 'VERIFY: read nonzero HISM instances and requested meshes',
    toolName: 'manage_pcg',
    arguments: { action: 'read_pcg_generated_instances', actorName: volumeActor, componentPath: '${captured:componentPath}' },
    expected,
    captureResult: { key: 'firstGeneratedInstances', fromField: 'result.instances' },
    assertions: [
      { path: 'structuredContent.result.instanceCount', greaterThan: 0, label: 'nonzero generated instance count' },
      { path: 'structuredContent.result.hismInstanceCount', greaterThan: 0, label: 'nonzero HISM instance count' },
      { path: 'structuredContent.result.instances', includesObject: { meshPath: treeMeshes[0] }, label: 'generated output uses requested first mesh' }
    ]
  },
  {
    scenario: 'GENERATE: regenerate twice with same seed',
    toolName: 'manage_pcg',
    arguments: { action: 'regenerate_pcg_component', actorName: volumeActor, componentPath: '${captured:componentPath}', force: true, save: false },
    expected
  },
  {
    scenario: 'VERIFY: deterministic generated output matches first generation',
    toolName: 'manage_pcg',
    arguments: { action: 'read_pcg_generated_instances', actorName: volumeActor, componentPath: '${captured:componentPath}' },
    expected,
    assertions: [{ path: 'structuredContent.result.instances', equalsCaptured: 'firstGeneratedInstances', label: 'instance paths/counts/signatures are deterministic' }]
  },
  {
    scenario: 'CLEAR: safely clear generated output',
    toolName: 'manage_pcg',
    arguments: { action: 'clear_pcg_generated_output', actorName: volumeActor, componentPath: '${captured:componentPath}', save: false },
    expected,
    assertions: [{ path: 'structuredContent.result.instanceCount', equals: 0, label: 'generated instances cleared' }]
  },
  {
    scenario: 'CLEANUP: delete temporary PCG volume',
    toolName: 'control_actor',
    arguments: { action: 'delete', actorName: volumeActor },
    expected: 'success|not found'
  },
  {
    scenario: 'CLEANUP: delete all temporary PCG assets',
    toolName: 'manage_asset',
    arguments: { action: 'delete', path: folder, force: true },
    expected: 'success|not found'
  }
]);
