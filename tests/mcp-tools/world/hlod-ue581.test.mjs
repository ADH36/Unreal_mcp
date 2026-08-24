/**
 * UE 5.8.1 World Partition HLOD integration workflow.
 *
 * Requires an editor with the bridge installed. The builder runs in an
 * isolated UnrealEditor-Cmd process; this test polls its explicit status API
 * rather than blocking the active editor.
 */

import { TestRunner } from '../../test-runner.mjs';

const suffix = Date.now();
const folder = `/Game/MCPTest/HLOD_${suffix}`;
const mapPath = `${folder}/HLODWorld`;
const unsavedMapPath = `${folder}/UnsavedWPWorld`;
const nonWpMapPath = `${folder}/NonWPWorld`;
const layerName = `HLODLayer_${suffix}`;
const layerPath = `${folder}/HLODLayers`;
const actors = [0, 1, 2].map(index => `HLODSource_${suffix}_${index}`);
const runner = new TestRunner('UE 5.8.1 World Partition HLOD automation');

function resultOf(response) {
  return response?.result ?? response?.data?.result ?? response?.data ?? response ?? {};
}

function ensureSuccess(response, label) {
  if (response?.success === false || response?.isError === true) {
    throw new Error(`${label}: ${JSON.stringify(response)}`);
  }
  return resultOf(response);
}

async function waitForHlodCommandlet(tools) {
  const deadline = Date.now() + 20 * 60 * 1000;
  let last = {};
  while (Date.now() < deadline) {
    last = resultOf(ensureSuccess(await tools.executeTool('manage_level_structure', {
      action: 'get_hlod_build_status'
    }), 'read HLOD build status'));
    if (last.running === false && last.status !== 'idle') {
      if (last.exitCode !== 0) throw new Error(`HLOD commandlet failed: ${JSON.stringify(last)}`);
      return last;
    }
    await new Promise(resolve => setTimeout(resolve, 2000));
  }
  throw new Error(`Timed out waiting for HLOD commandlet: ${JSON.stringify(last)}`);
}

runner.addStep('create a saved World Partition map and HLOD source actors', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_asset', { action: 'create_folder', path: folder }), 'create test folder');
  ensureSuccess(await tools.executeTool('manage_level', {
    action: 'create_level', levelName: 'HLODWorld', levelPath: folder,
    template: '/Engine/Maps/Templates/OpenWorld', useWorldPartition: true, saveDirtyPackages: true
  }), 'create World Partition map');
  for (const [index, actorName] of actors.entries()) {
    ensureSuccess(await tools.executeTool('control_actor', {
      action: 'spawn', classPath: '/Engine/BasicShapes/Cube', actorName,
      location: { x: index * 600, y: 0, z: 100 }
    }), `spawn ${actorName}`);
  }
  return true;
});

runner.addStep('create, inspect, and assign an HLOD Layer', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'create_hlod_layer', hlodLayerName: layerName, hlodLayerPath: layerPath,
    layerType: 'Instancing', cellSize: 12800, loadingDistance: 25600, bIsSpatiallyLoaded: true
  }), 'create HLOD Layer');
  const listed = resultOf(ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'list_hlod_layers'
  }), 'list HLOD Layers'));
  if (!listed.hlodLayers?.some(layer => layer.name === layerName)) throw new Error('created HLOD Layer was not listed');
  for (const actorName of actors) {
    ensureSuccess(await tools.executeTool('manage_level_structure', {
      action: 'assign_hlod_layer', actorName, hlodLayerName: layerName, hlodLayerPath: layerPath
    }), `assign ${actorName}`);
  }
  ensureSuccess(await tools.executeTool('manage_level', { action: 'save' }), 'save HLOD map and external actors');
  return true;
});

runner.addStep('build HLODs, poll streamed status, reload, and validate generated actors', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level_structure', { action: 'build_hlods' }), 'start HLOD build');
  const finalStatus = await waitForHlodCommandlet(tools);
  if (!finalStatus.logTail || finalStatus.exitCode !== 0) throw new Error(`missing HLOD final status/log: ${JSON.stringify(finalStatus)}`);
  ensureSuccess(await tools.executeTool('manage_level', { action: 'load_level', levelPath: mapPath, saveDirtyPackages: true }), 'reload HLOD map');
  const validated = resultOf(ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'validate_hlods'
  }), 'validate HLODs after reload'));
  if (!validated.generatedHlodActors?.some(actor => Number(actor.sourceActorCount) > 0)) {
    throw new Error(`generated HLOD source actor counts were not verified: ${JSON.stringify(validated)}`);
  }
  return true;
});

runner.addStep('report a missing assignment and safely reject unsaved and non-World-Partition builds', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'remove_hlod_layer', actorName: actors[0]
  }), 'remove one HLOD assignment');
  const missing = resultOf(ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'report_missing_hlod_assignments'
  }), 'report missing HLOD assignment'));
  if (!missing.missingHlodActors?.some(actor => actor.name === actors[0])) throw new Error('missing HLOD assignment was not reported');
  ensureSuccess(await tools.executeTool('manage_level_structure', {
    action: 'create_level', levelName: 'UnsavedWPWorld', levelPath: folder,
    bCreateWorldPartition: true, bUseExternalActors: true, save: false
  }), 'create unsaved World Partition map');
  const unsaved = await tools.executeTool('manage_level_structure', { action: 'build_hlods' });
  if (unsaved?.success !== false && unsaved?.isError !== true) throw new Error('unsaved map was accepted for HLOD build');
  ensureSuccess(await tools.executeTool('manage_level', {
    action: 'create_level', levelName: 'NonWPWorld', levelPath: folder,
    template: '/Engine/Maps/Templates/Basic', useWorldPartition: false, saveDirtyPackages: true
  }), 'create non-World-Partition map');
  const nonWp = await tools.executeTool('manage_level_structure', { action: 'build_hlods' });
  if (nonWp?.success !== false && nonWp?.isError !== true) throw new Error('non-World-Partition map was accepted for HLOD build');
  return true;
});

runner.addStep('delete generated HLOD output only with explicit confirmation and clean test content', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level', { action: 'load_level', levelPath: mapPath, saveDirtyPackages: true }), 'return to HLOD map');
  const unconfirmed = await tools.executeTool('manage_level_structure', { action: 'delete_hlod_output' });
  if (unconfirmed?.success !== false && unconfirmed?.isError !== true) throw new Error('HLOD delete was accepted without confirm=true');
  ensureSuccess(await tools.executeTool('manage_level_structure', { action: 'delete_hlod_output', confirm: true }), 'start confirmed HLOD output deletion');
  await waitForHlodCommandlet(tools);
  ensureSuccess(await tools.executeTool('manage_level', { action: 'delete', levelPaths: [mapPath, unsavedMapPath, nonWpMapPath] }), 'delete test maps');
  ensureSuccess(await tools.executeTool('manage_asset', { action: 'delete', path: folder, force: true }), 'delete test content');
  return true;
});

await runner.run();
