/**
 * UE 5.8.1 spline authoring integration workflow.
 *
 * Run with a UE 5.8.1 editor and bridge:
 *   node tests/mcp-tools/world/spline-ue58.test.mjs
 */

import { TestRunner } from '../../test-runner.mjs';

const suffix = Date.now();
const actorName = `MCP_UE58_Spline_${suffix}`;
const closedActorName = `MCP_UE58_Closed_${suffix}`;
const levelPath = `/Game/MCPTest/UE58Spline_${suffix}`;
const meshPath = '/Engine/EngineMeshes/Cube';
const route = [
  { x: 1000, y: -500, z: 200 },
  { x: 1325, y: -275, z: 245 },
  { x: 1610, y: 140, z: 310 },
  { x: 1890, y: -80, z: 275 },
  { x: 2240, y: 360, z: 330 }
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

function pointOf(value) {
  return value?.location ?? value?.worldLocation ?? value;
}

function assertRoute(actualPoints, expectedPoints, label) {
  if (!Array.isArray(actualPoints) || actualPoints.length !== expectedPoints.length) {
    throw new Error(`${label}: expected ${expectedPoints.length} points, got ${actualPoints?.length}`);
  }
  actualPoints.forEach((entry, index) => {
    const actual = pointOf(entry);
    const expected = expectedPoints[index];
    for (const axis of ['x', 'y', 'z']) {
      if (Math.abs(Number(actual?.[axis]) - expected[axis]) > 0.001) {
        throw new Error(`${label}: point ${index}.${axis} expected ${expected[axis]}, got ${actual?.[axis]}`);
      }
    }
  });
}

const runner = new TestRunner('UE 5.8.1 spline authoring');

runner.addStep('reject malformed and duplicate routes without creating defaults', async (tools) => {
  const malformedName = `${actorName}_Malformed`;
  const malformed = await tools.executeTool('build_environment', {
    action: 'create_spline_actor', actorName: malformedName,
    coordinateSpace: 'World', points: [{ x: 1, y: 2 }]
  });
  if (malformed?.success !== false && malformed?.isError !== true) {
    throw new Error(`malformed route was accepted: ${JSON.stringify(malformed)}`);
  }
  const duplicate = await tools.executeTool('build_environment', {
    action: 'create_spline_actor', actorName: `${actorName}_Duplicate`,
    coordinateSpace: 'World', points: [route[0], route[0]]
  });
  if (duplicate?.success !== false && duplicate?.isError !== true) {
    throw new Error(`duplicate route was accepted: ${JSON.stringify(duplicate)}`);
  }
  return true;
});

runner.addStep('create five-point non-linear world-space spline without defaults', async (tools) => {
  const response = await tools.executeTool('build_environment', {
    action: 'create_spline_actor',
    actorName,
    coordinateSpace: 'World',
    points: route,
    bClosedLoop: false,
    splineType: 'Linear'
  });
  const result = ensureSuccess(response, 'create spline');
  if (result.pointCount !== 5 || result.coordinateSpace !== 'World') {
    throw new Error(`create spline did not preserve route metadata: ${JSON.stringify(result)}`);
  }
  return true;
});

runner.addStep('read back every requested world-space coordinate', async (tools) => {
  const response = await tools.executeTool('build_environment', {
    action: 'inspect_spline_points', actorName, coordinateSpace: 'World'
  });
  const result = ensureSuccess(response, 'inspect created spline');
  assertRoute(result.points, route, 'initial route');
  return true;
});

runner.addStep('configure point tangents, rotation, scale, roll, and type', async (tools) => {
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_spline_point_tangents', actorName, pointIndex: 1,
    coordinateSpace: 'World', arriveTangent: { x: 120, y: 30, z: 0 },
    leaveTangent: { x: 180, y: 45, z: 0 }
  }), 'set tangents');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_spline_point_rotation', actorName, pointIndex: 1,
    coordinateSpace: 'World', pointRotation: { pitch: 0, yaw: 25, roll: 10 }
  }), 'set rotation');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_spline_point_scale', actorName, pointIndex: 1,
    pointScale: { x: 1.25, y: 0.9, z: 1 }
  }), 'set scale');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_spline_point_roll', actorName, pointIndex: 1, roll: 12
  }), 'set roll');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'set_spline_type', actorName, pointIndex: 1, splineType: 'CurveClamped'
  }), 'set point type');
  return true;
});

runner.addStep('insert and update points while preserving ordering', async (tools) => {
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'insert_spline_point', actorName, index: 2, coordinateSpace: 'World',
    position: { x: 1475, y: -40, z: 285 }
  }), 'insert point');
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'update_spline_point', actorName, pointIndex: 2, coordinateSpace: 'World',
    position: { x: 1500, y: 15, z: 290 }
  }), 'update point');
  const response = await tools.executeTool('build_environment', {
    action: 'inspect_spline_points', actorName, coordinateSpace: 'World'
  });
  const result = ensureSuccess(response, 'inspect edited spline');
  assertRoute(result.points, [route[0], route[1], { x: 1500, y: 15, z: 290 }, route[2], route[3], route[4]], 'edited route');
  return true;
});

runner.addStep('generate road spline-mesh segments with collision and route transforms', async (tools) => {
  const response = await tools.executeTool('build_environment', {
    action: 'generate_spline_mesh_segments', actorName, meshPath,
    forwardAxis: 'X', collisionEnabled: true
  });
  const result = ensureSuccess(response, 'generate road segments');
  if (result.segmentCount !== 5 || result.generatedComponents?.length !== 5) {
    throw new Error(`expected five open-route segments: ${JSON.stringify(result)}`);
  }
  if (result.generatedComponents.some((segment) => segment.collisionEnabled !== true)) {
    throw new Error('generated road segment collision setting was not preserved');
  }
  assertRoute(result.generatedComponents.map((segment) => segment.startPosition),
    [route[0], route[1], { x: 1500, y: 15, z: 290 }, route[2], route[3]], 'segment starts');
  return true;
});

runner.addStep('create and inspect a closed-loop route', async (tools) => {
  const response = await tools.executeTool('build_environment', {
    action: 'create_road_spline', actorName: closedActorName,
    coordinateSpace: 'World', points: route, bClosedLoop: true,
    meshPath, forwardAxis: 'X', collisionEnabled: false
  });
  const result = ensureSuccess(response, 'create closed road');
  if (result.pointCount !== 5 || result.segmentCount !== 5 || result.closedLoop !== true) {
    throw new Error(`closed route was not generated from supplied points: ${JSON.stringify(result)}`);
  }
  return true;
});

runner.addStep('save, reload, and verify route persistence', async (tools) => {
  ensureSuccess(await tools.executeTool('manage_level', {
    action: 'save_level_as', path: levelPath
  }), 'save level');
  ensureSuccess(await tools.executeTool('manage_level', {
    action: 'load_level', path: levelPath
  }), 'reload level');
  const response = await tools.executeTool('build_environment', {
    action: 'inspect_spline_points', actorName, coordinateSpace: 'World'
  });
  const result = ensureSuccess(response, 'inspect reloaded spline');
  assertRoute(result.points, [route[0], route[1], { x: 1500, y: 15, z: 290 }, route[2], route[3], route[4]], 'reloaded route');
  return true;
});

runner.addStep('clear generated segments and delete temporary actors', async (tools) => {
  ensureSuccess(await tools.executeTool('build_environment', {
    action: 'clear_generated_spline_segments', actorName
  }), 'clear generated segments');
  ensureSuccess(await tools.executeTool('control_actor', { action: 'delete', actorName }), 'delete open spline');
  ensureSuccess(await tools.executeTool('control_actor', { action: 'delete', actorName: closedActorName }), 'delete closed spline');
  return true;
});

await runner.run();
