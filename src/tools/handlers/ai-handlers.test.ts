import { describe, expect, it, vi } from 'vitest';
import type { AutomationBridge } from '../../automation/index.js';
import type { ITools } from '../../types/tool-interfaces.js';
import { handleAITools } from './ai-handlers.js';

function createTools() {
  const sendAutomationRequest = vi.fn(async () => ({ success: true }));
  const tools: ITools = {
    systemTools: {
      executeConsoleCommand: vi.fn(async () => ({ success: true })),
      getProjectSettings: vi.fn(async () => ({}))
    },
    assetResources: { list: vi.fn(async () => ({})) },
    automationBridge: {
      isConnected: () => true,
      sendAutomationRequest
    } as unknown as AutomationBridge
  };
  return { tools, sendAutomationRequest };
}

describe('runtime AI validation actions', () => {
  it('routes runtime inspection without inventing editor-world state', async () => {
    const { tools, sendAutomationRequest } = createTools();
    await handleAITools('inspect_runtime_ai', { action: 'inspect_runtime_ai', world: 'PIE' }, tools);
    expect(sendAutomationRequest).toHaveBeenCalledWith('manage_ai', {
      action: 'inspect_runtime_ai', subAction: 'inspect_runtime_ai', world: 'PIE'
    }, { timeoutMs: 120000 });
  });

  it('requires a query asset for runtime EQS', async () => {
    const { tools, sendAutomationRequest } = createTools();
    await expect(handleAITools('run_env_query', { action: 'run_env_query' }, tools)).rejects.toThrow('queryPath');
    expect(sendAutomationRequest).not.toHaveBeenCalled();
  });

  it('requires compatible runtime pawn and controller inputs', async () => {
    const { tools, sendAutomationRequest } = createTools();
    await expect(handleAITools('spawn_runtime_ai', { action: 'spawn_runtime_ai' }, tools)).rejects.toThrow('pawnClassPath');
    expect(sendAutomationRequest).not.toHaveBeenCalled();
  });
});
