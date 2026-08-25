#!/usr/bin/env node

import { TestRunner } from '../../test-runner.mjs';

const folder = '/Game/MCPRegression/PCGRealMesh';
const graphPath = `${folder}/PCG_RealMeshTest`;
const mapPath = `${folder}/PCG_RealMeshValidationMap`;
const volumeActor = 'MCP_PCG_RealMeshValidationVolume';
const volumeComponent = 'MCP_PCG_RealMeshValidationComponent';

const runner = new TestRunner('manage-pcg-static-mesh-spawner-restart');

runner.addStep('Reload saved PCG map after editor restart', async ({ executeTool }) => {
  const response = await executeTool('manage_level', { action: 'load_level', levelPath: mapPath, saveDirtyPackages: true });
  if (response.success !== true) throw new Error(response.error || response.message || 'Map reload failed');
  return response;
});

runner.addStep('Verify saved graph mesh entries after editor restart', async ({ executeTool }) => {
  const response = await executeTool('manage_pcg', {
    action: 'inspect_static_mesh_spawner',
    graphPath,
    nodeId: 'StaticMeshSpawner_0'
  });
  const entries = response.result?.meshEntries;
  if (response.success !== true || !Array.isArray(entries) || entries.length !== 3) {
    throw new Error('Saved weighted mesh entries did not survive editor restart');
  }
  if (entries.some((entry) => String(entry.meshPath).toLowerCase().includes('cube'))) {
    throw new Error('Cube fallback appeared in saved mesh entries after restart');
  }
  return response;
});

runner.addStep('Verify materialized HISM output after editor restart', async ({ executeTool }) => {
  const response = await executeTool('manage_pcg', {
    action: 'read_pcg_generated_instances',
    actorName: volumeActor,
    componentName: volumeComponent
  });
  const result = response.result ?? {};
  if (response.success !== true || result.materialized !== true || Number(result.hismInstanceCount) <= 0) {
    throw new Error('Materialized HISM output did not survive editor restart');
  }
  if (result.hasFallbackMesh === true || result.transformsWithinBounds !== true) {
    throw new Error('Restarted output failed non-fallback or bounds verification');
  }
  return response;
});

runner.addStep('Delete temporary restart-validation actor', async ({ executeTool }) => {
  return executeTool('control_actor', { action: 'delete', actorName: volumeActor });
});

runner.addStep('Delete temporary restart-validation content', async ({ executeTool }) => {
  return executeTool('manage_asset', { action: 'delete', path: folder, force: true });
});

await runner.run();
