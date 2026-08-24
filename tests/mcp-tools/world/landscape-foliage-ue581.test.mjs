/**
 * UE 5.8.1 landscape and foliage authoring integration workflow.
 * Run with an interactive UE 5.8.1 editor and bridge:
 *   node tests/mcp-tools/world/landscape-foliage-ue581.test.mjs
 */

import { TestRunner } from '../../test-runner.mjs';

const suffix = Date.now();
const folder = `/Game/MCPTest/UE581Landscape_${suffix}`;
const levelPath = `${folder}/L_UE581Landscape_${suffix}`;
const landscape = `MCP_UE581_Landscape_${suffix}`;
const foliageName = `MCP_UE581_Foliage_${suffix}`;
const materialName = `MCP_UE581_LandscapeMaterial_${suffix}`;
const materialPath = `${folder}/${materialName}`;
const layers = ['Grass', 'Dirt', 'Rock'];
const roadZone = { min: { x: -600, y: -150, z: -10000 }, max: { x: 600, y: 150, z: 10000 } };
const buildingZone = { min: { x: 700, y: 700, z: -10000 }, max: { x: 1400, y: 1400, z: 10000 } };
const foliageTypes = [
  { meshPath: '/Engine/BasicShapes/Sphere', count: 12, minScale: 0.4, maxScale: 0.8 },
  { meshPath: '/Engine/BasicShapes/Cylinder', count: 10, minScale: 0.5, maxScale: 1.0 },
  { meshPath: '/Engine/BasicShapes/Cone', count: 8, minScale: 0.6, maxScale: 1.1 }
];

function resultOf(response) {
  return response?.result ?? response?.data?.result ?? response?.data ?? response ?? {};
}

function ensureSuccess(response, label) {
  if (response?.success === false || response?.isError === true) {
    throw new Error(`${label}: ${JSON.stringify(response)}`);
  }
  return resultOf(response);
}

const runner = new TestRunner('UE 5.8.1 landscape and foliage authoring');

runner.addStep('create a World Partition map before creating a temporary landscape', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_asset', { action: 'create_folder', path: folder }), 'create test folder');
  ensureSuccess(await tools.executeTool('manage_level', {
    action: 'create_level', levelName: `L_UE581Landscape_${suffix}`, levelPath: folder,
    template: '/Engine/Maps/Templates/OpenWorld', useWorldPartition: true, saveDirtyPackages: true
  }), 'create World Partition test map');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'create_landscape', name: landscape, location: { x: 0, y: 0, z: 0 },
    quadsPerSection: 31, sectionsPerComponent: 1, componentCount: { x: 2, y: 2 }
  }), 'create landscape');
  for (const [tool, location] of [['Raise', { x: -550, y: 0, z: 0 }], ['Lower', { x: 0, y: 0, z: -100 }], ['Flatten', { x: 1000, y: 1000, z: 0 }]]) {
    ensureSuccess(await tools.executeTool('build_environment', {
      action: 'sculpt_landscape', landscapeName: landscape, tool, location, radius: 350, strength: 0.35
    }), `sculpt ${tool}`);
  }
  return true;
});

runner.addStep('create layer infos, apply a real layer-blend material, and paint grass/dirt/rock', async (tools) => {
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'create_landscape_material', name: materialName, path: folder, save: true
  }), 'create landscape material');
  const blend = ensureSuccess(await tools.executeTool('build_environment', {
    action: 'configure_landscape_layer_blend', materialPath,
    layers: layers.map((layerName) => ({ layerName, blendType: 'LB_WeightBlend' }))
  }), 'configure layer blend');
  if (blend.layerCount !== 3 || blend.connectedToBaseColor !== true) {
    throw new Error(`layer blend was not created from material expressions: ${JSON.stringify(blend)}`);
  }
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_landscape_material', landscapeName: landscape, materialPath
  }), 'apply landscape material');
  for (const layerName of layers) {
    const layerInfoPath = `${folder}/${layerName}_${suffix}`;
    ensureSuccess(await tools.executeTool('build_environment', {
      action: 'create_landscape_layer_info', name: `${layerName}_${suffix}`, path: folder, layerName
    }), `create ${layerName} layer info`);
    ensureSuccess(await tools.executeTool('build_environment', {
      action: 'paint_landscape_layer', landscapeName: landscape, layerName, layerInfoPath,
      region: { minX: 0, minY: 0, maxX: 62, maxY: 62 }, strength: layerName === 'Grass' ? 1 : 0.35
    }), `paint ${layerName}`);
  }
  return true;
});

runner.addStep('scatter deterministic HISM foliage with road and building exclusions', async (tools) => {
  const response = await tools.executeTool('build_environment', {
    action: 'scatter_landscape_foliage', landscapeName: landscape, foliageName,
    seed: 58101, placementMode: 'hism', foliageTypes,
    bounds: { min: { x: -1500, y: -1500, z: -1000 }, max: { x: 1500, y: 1500, z: 1000 } },
    exclusionZones: [roadZone, buildingZone], minSlope: 0, maxSlope: 55,
    minHeight: -5000, maxHeight: 5000, collisionEnabled: true
  });
  const result = ensureSuccess(response, 'scatter foliage');
  if (result.usesHISM !== true || result.instancesPlaced < 3 || result.excludedAreaViolations !== 0) {
    throw new Error(`invalid generated foliage result: ${JSON.stringify(result)}`);
  }
  return true;
});

runner.addStep('save, reload, and verify material, landscape, and generated instance persistence', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level', { action: 'save' }), 'save level');
  ensureSuccess(await tools.executeTool('manage_level', { action: 'load_level', levelPath, saveDirtyPackages: true }), 'reload level');
  const landscapeResult = ensureSuccess(await tools.executeTool('build_environment', { action: 'inspect_landscape', landscapeName: landscape }), 'inspect reloaded landscape');
  if (landscapeResult.materialPath !== `${materialPath}.${materialName}`) throw new Error(`material was not retained: ${JSON.stringify(landscapeResult)}`);
  if (landscapeResult.componentCount <= 0 || landscapeResult.layerCount !== 3) throw new Error(`landscape components or material layers were not retained: ${JSON.stringify(landscapeResult)}`);
  if ((landscapeResult.layerAssignments ?? []).filter((layer) => layer.nonZeroWeightCount > 0).length !== 3) throw new Error(`painted layer data was not retained: ${JSON.stringify(landscapeResult)}`);
  const foliageResult = ensureSuccess(await tools.executeTool('build_environment', { action: 'inspect_generated_foliage', foliageName }), 'inspect reloaded foliage');
  if (foliageResult.instanceCount < 3) throw new Error(`foliage was not retained: ${JSON.stringify(foliageResult)}`);
  return true;
});

runner.addStep('regenerate with the same seed and clear only MCP-generated foliage', async (tools) => {
  const regenerated = ensureSuccess(await tools.executeTool('build_environment', {
    action: 'regenerate_generated_foliage', landscapeName: landscape, foliageName,
    seed: 58101, placementMode: 'hism', foliageTypes,
    bounds: { min: { x: -1500, y: -1500, z: -1000 }, max: { x: 1500, y: 1500, z: 1000 } }, exclusionZones: [roadZone, buildingZone]
  }), 'regenerate foliage');
  if (regenerated.seed !== 58101 || regenerated.excludedAreaViolations !== 0) throw new Error(`regeneration was not deterministic: ${JSON.stringify(regenerated)}`);
  ensureSuccess(await tools.executeTool('build_environment', { action: 'clear_generated_foliage', foliageName, generatedOnly: true }), 'clear generated foliage');
  const afterClear = ensureSuccess(await tools.executeTool('build_environment', { action: 'inspect_generated_foliage', foliageName }), 'verify clear');
  if (afterClear.instanceCount !== 0) throw new Error(`generated foliage remains after clear: ${JSON.stringify(afterClear)}`);
  return true;
});

runner.addStep('delete all temporary test content', async (tools) => {
  ensureSuccess(await tools.executeTool('build_environment', { action: 'delete_landscape', landscapeName: landscape }), 'delete temporary landscape');
  ensureSuccess(await tools.executeTool('manage_asset', { action: 'delete_asset', path: materialPath }), 'delete temporary material');
  for (const layerName of layers) ensureSuccess(await tools.executeTool('manage_asset', { action: 'delete_asset', path: `${folder}/${layerName}_${suffix}` }), `delete ${layerName} layer`);
  return true;
});

await runner.run();
