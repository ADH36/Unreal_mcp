import { beforeEach, describe, expect, it, vi } from 'vitest';

const { executeAutomationRequestMock } = vi.hoisted(() => ({
  executeAutomationRequestMock: vi.fn(async () => ({ success: true, result: {} }))
}));

vi.mock('./common-handlers.js', async () => {
  const actual = await vi.importActual<typeof import('./common-handlers.js')>('./common-handlers.js');
  return {
    ...actual,
    executeAutomationRequest: executeAutomationRequestMock
  };
});

import { handleGraphTools } from './graph-handlers.js';
import { consolidatedToolDefinitions } from '../consolidated-tool-definitions.js';

describe('handleGraphTools behavior tree payload mapping', () => {
  beforeEach(() => {
    executeAutomationRequestMock.mockClear();
  });

  it('normalizes behavior tree creation savePath aliases before dispatch', async () => {
    await handleGraphTools('manage_behavior_tree', 'create', {
      action: 'create',
      name: 'BT_Test',
      savePath: 'Game/MCPTest/BT'
    }, {} as never);

    expect(executeAutomationRequestMock).toHaveBeenCalledWith(
      {},
      'manage_behavior_tree',
      expect.objectContaining({
        subAction: 'create',
        savePath: '/Game/MCPTest/BT'
      }),
      'Automation bridge not available'
    );
  });

  it('normalizes behavior tree assetPath aliases while preserving node aliases', async () => {
    await handleGraphTools('manage_behavior_tree', 'add_node', {
      action: 'add_node',
      assetPath: 'Game/MCPTest/BT/BT_Test',
      nodeType: 'Wait'
    }, {} as never);

    expect(executeAutomationRequestMock).toHaveBeenCalledWith(
      {},
      'manage_behavior_tree',
      expect.objectContaining({
        subAction: 'add_node',
        assetPath: '/Game/MCPTest/BT/BT_Test',
        nodeType: 'BTTask_Wait',
        nodeCategory: 'task'
      }),
      'Automation bridge not available'
    );
  });

  it('forwards Enhanced Input action paths for blueprint node creation', async () => {
    await handleGraphTools('manage_blueprint', 'create_node', {
      action: 'create_node',
      blueprintPath: '/Game/BP_Player',
      nodeType: 'K2Node_EnhancedInputAction',
      actionPath: '/Game/Input/IA_Throttle'
    }, {} as never);

    expect(executeAutomationRequestMock).toHaveBeenCalledWith(
      {},
      'manage_blueprint',
      expect.objectContaining({
        subAction: 'create_node',
        blueprintPath: '/Game/BP_Player',
        nodeType: 'K2Node_EnhancedInputAction',
        actionPath: '/Game/Input/IA_Throttle'
      }),
      'Automation bridge not available'
    );
  });

  it('publishes the UE 5.8 Blueprint graph actions in the canonical schema', () => {
    const blueprintTool = consolidatedToolDefinitions.find((tool) => tool.name === 'manage_blueprint');
    const actionSchema = (blueprintTool?.inputSchema.properties as Record<string, { enum?: string[] }>).action;

    expect(actionSchema.enum).toEqual(expect.arrayContaining([
      'create_event_graph', 'create_function_graph', 'add_begin_play',
      'add_variable_get', 'add_function_call', 'disconnect_pins', 'inspect_graph',
      'register_mapping_context_begin_play', 'add_enhanced_input_event', 'inspect_input_bindings'
    ]));
  });

  it('routes Blueprint graph-authoring aliases through the graph dispatcher', async () => {
    await handleGraphTools('manage_blueprint', 'add_function_call', {
      action: 'add_function_call',
      blueprintPath: '/Game/BP_GraphTest',
      classPath: '/Script/Engine.KismetSystemLibrary',
      functionName: 'PrintString'
    }, {} as never);

    expect(executeAutomationRequestMock).toHaveBeenCalledWith(
      {},
      'manage_blueprint',
      expect.objectContaining({
        subAction: 'add_function_call',
        blueprintPath: '/Game/BP_GraphTest',
        classPath: '/Script/Engine.KismetSystemLibrary',
        functionName: 'PrintString'
      }),
      'Automation bridge not available'
    );
  });

  it('routes automatic Enhanced Input registration and event binding actions', async () => {
    await handleGraphTools('manage_blueprint', 'register_mapping_context_begin_play', {
      action: 'register_mapping_context_begin_play', blueprintPath: '/Game/BP_Input',
      mappingContextPath: '/Game/Input/IMC_Test', priority: 0
    }, {} as never);
    await handleGraphTools('manage_blueprint', 'add_enhanced_input_event', {
      action: 'add_enhanced_input_event', blueprintPath: '/Game/BP_Input',
      inputActionPath: '/Game/Input/IA_Move', inputTriggerEvent: 'Triggered'
    }, {} as never);

    expect(executeAutomationRequestMock).toHaveBeenNthCalledWith(
      1, {}, 'manage_blueprint', expect.objectContaining({
        subAction: 'register_mapping_context_begin_play', mappingContextPath: '/Game/Input/IMC_Test'
      }), 'Automation bridge not available'
    );
    expect(executeAutomationRequestMock).toHaveBeenNthCalledWith(
      2, {}, 'manage_blueprint', expect.objectContaining({
        subAction: 'add_enhanced_input_event', inputActionPath: '/Game/Input/IA_Move', inputTriggerEvent: 'Triggered'
      }), 'Automation bridge not available'
    );
  });
});
