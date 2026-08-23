import { beforeEach, describe, expect, it, vi } from 'vitest';

import { handleSplineTools } from './spline-handlers.js';
import { consolidatedToolDefinitions } from '../consolidated-tool-definitions.js';

const executeAutomationRequestMock = vi.fn(async (_tool: string, payload: Record<string, unknown>) => ({
  success: true,
  result: payload
}));
const tools = {
  automationBridge: {
    isConnected: () => true,
    sendAutomationRequest: executeAutomationRequestMock
  }
} as never;

describe('UE 5.8 spline routing contract', () => {
  beforeEach(() => executeAutomationRequestMock.mockClear());

  it('keeps ordered route points and coordinate space intact when routed through manage_splines', async () => {
    const points = [
      { location: { x: 0, y: 0, z: 0 } },
      { location: { x: 100, y: 50, z: 10 } },
      { location: { x: 230, y: -25, z: 40 } }
    ];
    await handleSplineTools('create_spline_actor', {
      action: 'create_spline_actor',
      actorName: 'UE58SplineRoute',
      coordinateSpace: 'World',
      points,
      bClosedLoop: false
    }, tools);

    expect(executeAutomationRequestMock).toHaveBeenCalledWith(
      'manage_splines',
      expect.objectContaining({
        subAction: 'create_spline_actor',
        coordinateSpace: 'World',
        points
      }),
      expect.any(Object)
    );
  });

  it('routes insert/update and generated-segment lifecycle actions', async () => {
    for (const action of [
      'insert_spline_point',
      'update_spline_point',
      'generate_spline_mesh_segments',
      'rebuild_spline_mesh_segments',
      'clear_generated_spline_segments',
      'find_spline_actors',
      'find_spline_components',
      'inspect_spline_points'
    ]) {
      await handleSplineTools(action, { action, actorName: 'UE58SplineRoute' }, tools);
    }

    expect(executeAutomationRequestMock).toHaveBeenCalledTimes(8);
    expect(executeAutomationRequestMock.mock.calls.map(call => call[1].subAction)).toEqual([
      'insert_spline_point', 'update_spline_point', 'generate_spline_mesh_segments',
      'rebuild_spline_mesh_segments', 'clear_generated_spline_segments', 'find_spline_actors',
      'find_spline_components', 'inspect_spline_points'
    ]);
  });

  it('exposes the UE 5.8 spline fields and actions in build_environment', () => {
    const definition = consolidatedToolDefinitions.find(tool => tool.name === 'build_environment');
    const schema = definition?.inputSchema as {
      properties?: Record<string, { enum?: string[] }>;
    } | undefined;
    const actions = schema?.properties?.action?.enum ?? [];
    expect(actions).toEqual(expect.arrayContaining([
      'insert_spline_point', 'update_spline_point', 'set_spline_point_roll',
      'generate_spline_mesh_segments', 'rebuild_spline_mesh_segments',
      'clear_generated_spline_segments', 'find_spline_actors', 'find_spline_components',
      'inspect_spline_points'
    ]));
    expect(schema?.properties).toHaveProperty('points');
    expect(schema?.properties).toHaveProperty('routePoints');
    expect(schema?.properties).toHaveProperty('coordinateSpace');
    expect(schema?.properties).toHaveProperty('collisionEnabled');
  });
});
