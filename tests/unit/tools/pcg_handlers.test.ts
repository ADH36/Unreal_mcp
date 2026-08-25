import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { ITools } from '../../../src/types/tool-interfaces.js';

const { mockSendRequest } = vi.hoisted(() => ({ mockSendRequest: vi.fn() }));

vi.mock('../../../src/tools/handlers/common-handlers.js', () => ({
  createSubActionDispatcher: vi.fn(() => ({ sendRequest: mockSendRequest })),
  executeAutomationRequest: vi.fn()
}));

import { consolidatedToolDefinitions, PCG_ACTIONS } from '../../../src/tools/consolidated-tool-definitions.js';
import { handlePCGTools } from '../../../src/tools/handlers/pcg-handlers.js';

describe('PCG Static Mesh Spawner contract', () => {
  const tools = {
    automationBridge: {
      isConnected: vi.fn().mockReturnValue(true),
      sendAutomationRequest: vi.fn()
    }
  } as unknown as ITools;

  beforeEach(() => {
    vi.clearAllMocks();
    mockSendRequest.mockResolvedValue({ success: true, result: { nodeType: 'PCGStaticMeshSpawnerSettings' } });
  });

  it('exposes the complete mesh-spawner action set in the consolidated schema', () => {
    const definition = consolidatedToolDefinitions.find(tool => tool.name === 'manage_pcg');
    expect(definition).toBeDefined();
    const actionSchema = definition?.inputSchema.properties?.action as { enum?: string[] };
    for (const action of [
      'search_static_mesh_assets', 'validate_static_mesh_assets',
      'find_static_mesh_spawner', 'configure_static_mesh_spawner', 'add_static_mesh_entry',
      'update_static_mesh_entry', 'remove_static_mesh_entry', 'inspect_static_mesh_spawner',
      'regenerate_pcg_component', 'read_pcg_generated_instances', 'clear_pcg_generated_output'
    ]) {
      expect(PCG_ACTIONS).toContain(action);
      expect(actionSchema.enum).toContain(action);
    }
  });

  it('dispatches strict real-mesh discovery and validation actions', async () => {
    await handlePCGTools('search_static_mesh_assets', {
      action: 'search_static_mesh_assets',
      searchPaths: ['/Game/Environment'],
      suitableOnly: true,
      allowFallbackMesh: false,
      limit: 3
    }, tools);
    await handlePCGTools('validate_static_mesh_assets', {
      action: 'validate_static_mesh_assets',
      meshPaths: ['/Game/Environment/Tree_A.Tree_A'],
      allowFallbackMesh: false
    }, tools);

    expect(mockSendRequest).toHaveBeenNthCalledWith(1, 'search_static_mesh_assets');
    expect(mockSendRequest).toHaveBeenNthCalledWith(2, 'validate_static_mesh_assets');
  });

  it('dispatches entry authoring while preserving UE asset path normalization', async () => {
    await handlePCGTools('add_static_mesh_entry', {
      action: 'add_static_mesh_entry',
      graphPath: 'Game/MCPTests/PCG/PCG_MeshSpawnerTest',
      nodeId: 'Spawner',
      meshPath: 'Engine/BasicShapes/Cube.Cube',
      weight: 3,
      save: false
    }, tools);

    expect(mockSendRequest).toHaveBeenCalledWith('add_static_mesh_entry');
  });

  it('dispatches generated output readback and safe clear actions', async () => {
    await handlePCGTools('read_pcg_generated_instances', {
      action: 'read_pcg_generated_instances',
      actorName: 'PCG_MeshSpawnerTestActor',
      componentName: 'PCGComponent'
    }, tools);
    await handlePCGTools('clear_pcg_generated_output', {
      action: 'clear_pcg_generated_output',
      actorName: 'PCG_MeshSpawnerTestActor',
      componentName: 'PCGComponent'
    }, tools);

    expect(mockSendRequest).toHaveBeenNthCalledWith(1, 'read_pcg_generated_instances');
    expect(mockSendRequest).toHaveBeenNthCalledWith(2, 'clear_pcg_generated_output');
  });
});
