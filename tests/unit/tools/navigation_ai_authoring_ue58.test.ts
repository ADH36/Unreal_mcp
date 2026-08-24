import { describe, expect, it } from 'vitest';
import {
  NAVIGATION_ACTIONS,
  consolidatedToolDefinitions,
} from '../../../src/tools/consolidated-tool-definitions.js';

describe('UE 5.8 navigation AI authoring contract', () => {
  const actions = [
    'create_nav_mesh_bounds',
    'build_navigation',
    'query_navigation_path',
    'validate_navigation',
  ];

  it('exposes the navigation-authoring actions through manage_ai', () => {
    const manageAi = consolidatedToolDefinitions.find((tool) => tool.name === 'manage_ai');
    const actionEnum = (manageAi as { inputSchema?: { properties?: { action?: { enum?: string[] } } } })
      ?.inputSchema?.properties?.action?.enum ?? [];
    for (const action of actions) {
      expect(NAVIGATION_ACTIONS).toContain(action);
      expect(actionEnum).toContain(action);
    }
  });

  it('defines bounds and path-query inputs', () => {
    const manageAi = consolidatedToolDefinitions.find((tool) => tool.name === 'manage_ai');
    const properties = (manageAi as { inputSchema?: { properties?: Record<string, unknown> } })
      ?.inputSchema?.properties ?? {};
    for (const property of ['boundsActorName', 'extent', 'start', 'end']) {
      expect(properties[property]).toBeTruthy();
    }
  });
});
