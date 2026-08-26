#!/usr/bin/env node
/**
 * UE 5.8.1 navigation-authoring regression. Run with a live UE 5.8.1 editor:
 * node tests/mcp-tools/gameplay/ai-navigation-authoring-ue58.test.mjs
 *
 * This intentionally limits build scope to the test volume. It asserts the
 * query result rather than treating a successful request as a valid path.
 */
import { runToolTests } from '../../test-runner.mjs';

const stamp = Date.now();
const bounds = `MCP_AI_NavBounds_${stamp}`;
const obstacle = `MCP_AI_NavObstacle_${stamp}`;

runToolTests('ai-navigation-authoring-ue58', [
  { scenario: 'Setup: create scoped navigation bounds with real brush geometry', toolName: 'manage_ai', arguments: { action: 'create_nav_mesh_bounds', boundsActorName: bounds, location: { x: 0, y: 0, z: 0 }, extent: { x: 2500, y: 2500, z: 500 } }, expected: 'success', assertions: [{ path: 'structuredContent.created', equals: true, label: 'bounds was created' }, { path: 'structuredContent.boundsExtentX', predicate: value => Number(value) > 0, label: 'X brush bounds are nonzero' }, { path: 'structuredContent.boundsExtentY', predicate: value => Number(value) > 0, label: 'Y brush bounds are nonzero' }, { path: 'structuredContent.boundsExtentZ', predicate: value => Number(value) > 0, label: 'Z brush bounds are nonzero' }] },
  { scenario: 'Setup: create an obstacle in the test arena', toolName: 'manage_level_structure', arguments: { action: 'create_nav_modifier_volume', volumeName: obstacle, location: { x: 0, y: 0, z: 100 }, extent: { x: 250, y: 1200, z: 300 } }, expected: 'success' },
  { scenario: 'Build: queue only the test navigation bounds', toolName: 'manage_ai', arguments: { action: 'build_navigation', boundsActorName: bounds }, expected: 'success', assertions: [{ path: 'structuredContent.result.scoped', equals: true, label: 'build is scoped' }] },
  { scenario: 'Validate: query a reachable route around the obstacle', toolName: 'manage_ai', arguments: { action: 'validate_navigation', start: { x: -1800, y: -1800, z: 100 }, end: { x: 1800, y: -1800, z: 100 } }, expected: 'success', assertions: [{ path: 'structuredContent.result.startNavigable', equals: true, label: 'start projects to nav' }, { path: 'structuredContent.result.endNavigable', equals: true, label: 'end projects to nav' }, { path: 'structuredContent.result.pathValid', equals: true, label: 'route is complete' }] },
  { scenario: 'Cleanup: remove navigation obstacle', toolName: 'control_actor', arguments: { action: 'delete', actorName: obstacle }, expected: 'success|not found' },
  { scenario: 'Cleanup: remove navigation bounds', toolName: 'control_actor', arguments: { action: 'delete', actorName: bounds }, expected: 'success|not found' },
]);
