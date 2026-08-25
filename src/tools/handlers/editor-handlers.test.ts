import { describe, expect, it, vi } from 'vitest';
import type { AutomationBridge } from '../../automation/index.js';
import type { ITools } from '../../types/tool-interfaces.js';
import { handleEditorTools } from './editor-handlers.js';

function createConnectedTools() {
  const sendAutomationRequest = vi.fn(async () => ({ success: true }));
  const tools: ITools = {
    systemTools: {
      executeConsoleCommand: vi.fn(async () => ({ success: true })),
      getProjectSettings: vi.fn(async () => ({}))
    },
    assetResources: {
      list: vi.fn(async () => ({}))
    },
    automationBridge: {
      isConnected: () => true,
      sendAutomationRequest
    } as unknown as AutomationBridge
  };

  return { tools, sendAutomationRequest };
}

describe('handleEditorTools', () => {
  it('keeps control_editor schemas strict-function compatible', async () => {
    const { consolidatedToolDefinitions } = await import('../consolidated-tool-definitions.js');
    const { coreToolDefinitions } = await import('../schemas/core-tools.js');
    const tools = [
      consolidatedToolDefinitions.find((tool) => tool.name === 'control_editor'),
      coreToolDefinitions.find((tool) => tool.name === 'control_editor')
    ];
    const forbiddenTopLevelKeywords = ['oneOf', 'anyOf', 'allOf', 'enum', 'not'];

    for (const tool of tools) {
      const inputSchema = tool?.inputSchema as Record<string, unknown> | undefined;

      expect(inputSchema?.type).toBe('object');
      for (const keyword of forbiddenTopLevelKeywords) {
        expect(inputSchema).not.toHaveProperty(keyword);
      }
    }
  });

  it('exposes all supported simulate_input parameters in the public schemas', async () => {
    const { consolidatedToolDefinitions } = await import('../consolidated-tool-definitions.js');
    const { coreToolDefinitions } = await import('../schemas/core-tools.js');
    const tools = [
      consolidatedToolDefinitions.find((tool) => tool.name === 'control_editor'),
      coreToolDefinitions.find((tool) => tool.name === 'control_editor')
    ];

    for (const tool of tools) {
      const properties = (tool?.inputSchema as Record<string, unknown> & {
        properties: Record<string, unknown>;
      }).properties;

      expect(properties).toHaveProperty('type');
      expect(properties).toHaveProperty('inputType');
      expect(properties).toHaveProperty('inputAction');
      expect(properties).toHaveProperty('x');
      expect(properties).toHaveProperty('y');
      expect(properties).toHaveProperty('button');
      expect(properties).toHaveProperty('mode');
      expect(properties).toHaveProperty('returnBase64');
      expect(properties).toHaveProperty('pieMode');
      expect(properties).toHaveProperty('sequence');
      expect(properties).toHaveProperty('durationMs');
      expect(properties).toHaveProperty('playerIndex');
      expect(properties).toHaveProperty('warmupFrames');
      expect(properties).toHaveProperty('screenshotDelayMs');
    }
  });

  it('allows screenshot without a filename so Unreal can generate one', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('screenshot', { action: 'screenshot' }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', {
      action: 'screenshot',
      filename: undefined,
      resolution: undefined
    }, {});
  });

  it('normalizes editor asset and level paths before dispatch', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('open_asset', { action: 'open_asset', path: 'Content\\UI\\WBP_Menu' }, tools);
    await handleEditorTools('open_level', { action: 'open_level', assetPath: '/Content/Maps/Demo' }, tools);

    expect(sendAutomationRequest).toHaveBeenNthCalledWith(1, 'control_editor', {
      action: 'open_asset',
      assetPath: '/Game/UI/WBP_Menu'
    }, {});
    expect(sendAutomationRequest).toHaveBeenNthCalledWith(2, 'control_editor', {
      action: 'open_level',
      levelPath: '/Game/Maps/Demo'
    }, {});
  });

  it('requests image data for full editor window screenshots', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('screenshot', { action: 'screenshot', filename: 'FullEditor', mode: 'full_editor_window' }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', {
      action: 'screenshot',
      filename: 'FullEditor',
      resolution: undefined,
      mode: 'full_editor_window',
      returnBase64: true
    }, {});
  });

  it('routes game viewport screenshots to the game viewport capture path', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('screenshot', { action: 'screenshot', filename: 'GameViewport', mode: 'game_viewport' }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('system_control', {
      action: 'screenshot',
      filename: 'GameViewport',
      resolution: undefined,
      mode: 'game_viewport',
      returnBase64: true
    }, {});
  });

  it('routes capture_pie_screenshot through the consolidated image path', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('capture_pie_screenshot', {
      action: 'capture_pie_screenshot', filename: 'PIE.png', warmupFrames: 4, screenshotDelayMs: 120
    }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('system_control', expect.objectContaining({
      action: 'screenshot', mode: 'game_viewport', filename: 'PIE.png', warmupFrames: 4, screenshotDelayMs: 120
    }), { timeoutMs: 60000 });
  });

  it('returns string action for invalid screenshot modes', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    const result = await handleEditorTools('screenshot', { action: 'screenshot', mode: 'bad_mode' }, tools);

    expect(result).toMatchObject({
      success: false,
      type: 'INVALID_ARGUMENT',
      error: 'INVALID_ARGUMENT',
      action: 'screenshot'
    });
    expect(sendAutomationRequest).not.toHaveBeenCalled();
  });

  it('maps simulate_input from inputAction without reading the routing action', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('simulate_input', { action: 'simulate_input', inputAction: 'pressed', key: 'K' }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', {
      action: 'simulate_input',
      type: 'key_down',
      key: 'K',
      x: undefined,
      y: undefined,
      button: undefined
    }, {});
  });

  it('maps simulate_input from inputType key plus inputAction', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await handleEditorTools('simulate_input', { action: 'simulate_input', inputType: 'key', inputAction: 'release', key: 'K' }, tools);

    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', {
      action: 'simulate_input',
      type: 'key_up',
      key: 'K',
      x: undefined,
      y: undefined,
      button: undefined
    }, {});
  });

  it('rejects simulate_input when only the routing action is present', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();

    await expect(handleEditorTools('simulate_input', { action: 'simulate_input', key: 'K' }, tools))
      .rejects.toThrow('type|inputType|inputAction');
    expect(sendAutomationRequest).not.toHaveBeenCalled();
  });

  it('rejects obsolete actorName with the playerIndex migration', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();
    await expect(handleEditorTools('move', { action: 'move', actorName: 'DefaultPawn_0' }, tools))
      .rejects.toThrow('actorName is obsolete for PIE input');
    expect(sendAutomationRequest).not.toHaveBeenCalled();
  });

  it('forwards explicit playerIndex for input dispatch', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();
    await handleEditorTools('send_input', { action: 'send_input', key: 'W', type: 'key_down', playerIndex: 1 }, tools);
    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', expect.objectContaining({
      action: 'simulate_input', type: 'key_down', key: 'W', playerIndex: 1
    }), { timeoutMs: 60000 });
  });

  it('accepts axis send_input without a keyboard key', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();
    await handleEditorTools('send_input', {
      action: 'send_input', type: 'axis', axisName: 'Gamepad_LeftX', axisValue: 0.5, playerIndex: 0
    }, tools);
    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', expect.objectContaining({
      action: 'simulate_input', type: 'axis', axisName: 'Gamepad_LeftX', axisValue: 0.5, playerIndex: 0
    }), { timeoutMs: 60000 });
  });

  it('exposes PIE play-test actions in the public schemas', async () => {
    const { consolidatedToolDefinitions } = await import('../consolidated-tool-definitions.js');
    const editorTool = consolidatedToolDefinitions.find((tool) => tool.name === 'control_editor');
    const actions = ((editorTool?.inputSchema as { properties?: { action?: { enum?: string[] } } })
      .properties?.action?.enum) ?? [];

    expect(actions).toEqual(expect.arrayContaining([
      'start_pie', 'get_pie_state', 'query_pie_actor', 'get_pie_metrics',
      'detect_pie_issues', 'send_enhanced_input', 'capture_pie_screenshot',
      'read_pie_logs', 'run_playtest_sequence'
    ]));
  });

  it('stops PIE after a failed play-test step', async () => {
    const { tools, sendAutomationRequest } = createConnectedTools();
    sendAutomationRequest
      .mockResolvedValueOnce({ success: true, isInPIE: true })
      .mockResolvedValueOnce({ success: false, error: 'FORCED_TIMEOUT' })
      .mockResolvedValueOnce({ success: true, alreadyStopped: false })
      .mockResolvedValueOnce({ success: true, isInPIE: false });

    const result = await handleEditorTools('run_playtest_sequence', {
      action: 'run_playtest_sequence',
      timeoutMs: 100,
      sequence: [
        { action: 'get_pie_state' },
        { action: 'get_pie_metrics' }
      ]
    }, tools) as { success: boolean; report: { steps: Array<{ action: string; cleanup?: boolean }> } };

    expect(result.success).toBe(false);
    expect(result.report.steps.at(-1)).toMatchObject({ action: 'stop', cleanup: true });
    expect(sendAutomationRequest).toHaveBeenCalledWith('control_editor', { action: 'stop' }, { timeoutMs: 100 });
    expect(sendAutomationRequest).toHaveBeenLastCalledWith('control_editor', { action: 'get_pie_state' }, { timeoutMs: 100 });
  });
});
