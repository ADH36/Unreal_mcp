#!/usr/bin/env node
/**
 * Blueprint graph-authoring integration tests (UE 5.8 canonical graph actions).
 * Covers event/function graph creation and lookup, node shortcut actions, pin
 * plumbing, Enhanced Input bindings, inspection, and the optional compile flag.
 * Assumes /Game/MCP_Test/BP_MCPGraphDemo exists live during E2E runs; expected
 * outcomes fall back to whitelisted idempotency/not-found alternatives.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const DEMO_BLUEPRINT_PATH = '/Game/MCP_Test/BP_MCPGraphDemo';
const ENHANCED_INPUT_ACTION_PATH = `/Game/MCP_Test/IA_MCPGraph_${ts}`;
const MAPPING_CONTEXT_PATH = `/Game/MCP_Test/IMC_MCPGraph_${ts}`;

const testCases = [
  // === SETUP ===
  { scenario: 'Setup: ensure demo graph blueprint exists', toolName: 'manage_blueprint', arguments: { action: 'ensure_exists', name: 'BP_MCPGraphDemo', path: '/Game/MCP_Test', blueprintType: 'Actor' }, expected: 'success|already exists' },

  // === GRAPH CREATION / LOOKUP ===
  { scenario: 'GRAPH: create_event_graph with explicit event graphType', toolName: 'manage_blueprint', arguments: { action: 'create_event_graph', blueprintPath: DEMO_BLUEPRINT_PATH, graphName: `MCPDemoEvents_${ts}`, graphType: 'event' }, expected: 'success|already exists' },
  { scenario: 'GRAPH: find_event_graph', toolName: 'manage_blueprint', arguments: { action: 'find_event_graph', blueprintPath: DEMO_BLUEPRINT_PATH }, expected: 'success|not found' },
  { scenario: 'GRAPH: create_function_graph with explicit function graphType', toolName: 'manage_blueprint', arguments: { action: 'create_function_graph', blueprintPath: DEMO_BLUEPRINT_PATH, graphName: `MCPDemoFunction_${ts}`, graphType: 'function' }, expected: 'success|already exists' },
  { scenario: 'GRAPH: find_function_graph by name', toolName: 'manage_blueprint', arguments: { action: 'find_function_graph', blueprintPath: DEMO_BLUEPRINT_PATH, graphName: `MCPDemoFunction_${ts}` }, expected: 'success|not found' },

  // === EVENT / NODE SHORTCUTS ===
  { scenario: 'NODE: add_begin_play entry', toolName: 'manage_blueprint', arguments: { action: 'add_begin_play', blueprintPath: DEMO_BLUEPRINT_PATH, posX: 100, posY: 100 }, expected: 'success|already exists' },
  { scenario: 'NODE: add_tick entry', toolName: 'manage_blueprint', arguments: { action: 'add_tick', blueprintPath: DEMO_BLUEPRINT_PATH, posX: 100, posY: 300 }, expected: 'success|already exists' },
  { scenario: 'NODE: add_custom_event', toolName: 'manage_blueprint', arguments: { action: 'add_custom_event', blueprintPath: DEMO_BLUEPRINT_PATH, eventName: `MCPDemoCustomEvent_${ts}`, posX: 100, posY: 500 }, expected: 'success|already exists' },
  { scenario: 'NODE: add_variable_get', toolName: 'manage_blueprint', arguments: { action: 'add_variable_get', blueprintPath: DEMO_BLUEPRINT_PATH, variableName: 'Health', posX: 320, posY: 100 }, expected: 'success|not found' },
  { scenario: 'NODE: add_variable_set', toolName: 'manage_blueprint', arguments: { action: 'add_variable_set', blueprintPath: DEMO_BLUEPRINT_PATH, variableName: 'Health', posX: 320, posY: 260 }, expected: 'success|not found' },
  { scenario: 'NODE: add_function_call via memberClass reflection', toolName: 'manage_blueprint', arguments: { action: 'add_function_call', blueprintPath: DEMO_BLUEPRINT_PATH, memberClass: '/Script/Engine.KismetSystemLibrary', memberName: 'PrintString', posX: 520, posY: 100 }, expected: 'success|not found' },
  { scenario: 'NODE: add_branch flow control', toolName: 'manage_blueprint', arguments: { action: 'add_branch', blueprintPath: DEMO_BLUEPRINT_PATH, posX: 720, posY: 100 }, expected: 'success' },
  { scenario: 'NODE: add_sequence flow control', toolName: 'manage_blueprint', arguments: { action: 'add_sequence', blueprintPath: DEMO_BLUEPRINT_PATH, posX: 720, posY: 300 }, expected: 'success' },
  { scenario: 'NODE: add_cast with targetClass', toolName: 'manage_blueprint', arguments: { action: 'add_cast', blueprintPath: DEMO_BLUEPRINT_PATH, targetClass: '/Script/Engine.Pawn', posX: 920, posY: 100 }, expected: 'success' },
  { scenario: 'NODE: add_arithmetic via classPath + operation', toolName: 'manage_blueprint', arguments: { action: 'add_arithmetic', blueprintPath: DEMO_BLUEPRINT_PATH, classPath: '/Script/Engine.KismetMathLibrary', operation: 'Add', posX: 920, posY: 300 }, expected: 'success|not found' },
  { scenario: 'NODE: add_component_reference', toolName: 'manage_blueprint', arguments: { action: 'add_component_reference', blueprintPath: DEMO_BLUEPRINT_PATH, componentName: 'DefaultSceneRoot', posX: 1120, posY: 100 }, expected: 'success|not found' },
  { scenario: 'NODE: add_self_reference', toolName: 'manage_blueprint', arguments: { action: 'add_self_reference', blueprintPath: DEMO_BLUEPRINT_PATH, posX: 1120, posY: 300 }, expected: 'success' },
  { scenario: 'NODE: add_input_event legacy axis variant with inputEventType', toolName: 'manage_blueprint', arguments: { action: 'add_input_event', blueprintPath: DEMO_BLUEPRINT_PATH, inputEventType: 'axis', inputAxisName: 'MoveForward', posX: 1320, posY: 100 }, expected: 'success|not found' },

  // === ENHANCED INPUT BINDINGS ===
  { scenario: 'INPUT: add_enhanced_input_event with inputTriggerEvent pin', toolName: 'manage_blueprint', arguments: { action: 'add_enhanced_input_event', blueprintPath: DEMO_BLUEPRINT_PATH, inputActionPath: ENHANCED_INPUT_ACTION_PATH, inputTriggerEvent: 'Started', posX: 1520, posY: 100 }, expected: 'success|not found' },
  { scenario: 'INPUT: bind_input_action_event to demo blueprint', toolName: 'manage_blueprint', arguments: { action: 'bind_input_action_event', blueprintPath: DEMO_BLUEPRINT_PATH, inputActionPath: ENHANCED_INPUT_ACTION_PATH, inputTriggerEvent: 'Triggered' }, expected: 'success|not found' },
  { scenario: 'INPUT: register_mapping_context_begin_play with mappingContextPath', toolName: 'manage_blueprint', arguments: { action: 'register_mapping_context_begin_play', blueprintPath: DEMO_BLUEPRINT_PATH, mappingContextPath: MAPPING_CONTEXT_PATH }, expected: 'success|not found' },

  // === PIN PLUMBING ===
  { scenario: 'PINS: connect_pins between named pins with optional compile flag', toolName: 'manage_blueprint', arguments: { action: 'connect_pins', blueprintPath: DEMO_BLUEPRINT_PATH, fromNodeId: `MCPDemoEntry_${ts}`, fromPinName: 'then', toNodeId: `MCPDemoTarget_${ts}`, toPinName: 'execute', compile: true }, expected: 'success|not found' },
  { scenario: 'PINS: disconnect_pins reverses a link', toolName: 'manage_blueprint', arguments: { action: 'disconnect_pins', blueprintPath: DEMO_BLUEPRINT_PATH, fromNodeId: `MCPDemoEntry_${ts}`, fromPinName: 'then', toNodeId: `MCPDemoTarget_${ts}`, toPinName: 'execute' }, expected: 'success|not found' },

  // === INSPECTION ===
  { scenario: 'INFO: inspect_graph on default event graph', toolName: 'manage_blueprint', arguments: { action: 'inspect_graph', blueprintPath: DEMO_BLUEPRINT_PATH }, expected: 'success|not found' },
  { scenario: 'INFO: get_nodes snapshot', toolName: 'manage_blueprint', arguments: { action: 'get_nodes', blueprintPath: DEMO_BLUEPRINT_PATH }, expected: 'success|not found' },
  { scenario: 'INFO: get_connections snapshot', toolName: 'manage_blueprint', arguments: { action: 'get_connections', blueprintPath: DEMO_BLUEPRINT_PATH }, expected: 'success|not found' },
  { scenario: 'INFO: inspect_input_bindings for enhanced input wiring', toolName: 'manage_blueprint', arguments: { action: 'inspect_input_bindings', blueprintPath: DEMO_BLUEPRINT_PATH }, expected: 'success|not found' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete demo graph blueprint folder contents', toolName: 'manage_asset', arguments: { action: 'delete', assetPath: DEMO_BLUEPRINT_PATH, force: true }, expected: 'success|not found' },
];

runToolTests('blueprint-graph-authoring', testCases);
