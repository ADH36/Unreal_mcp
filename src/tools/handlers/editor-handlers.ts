import { cleanObject } from '../../utils/safe-json.js';
import { ITools } from '../../types/tool-interfaces.js';
import type { EditorArgs } from '../../types/handler-types.js';
import { executeAutomationRequest, normalizePathFields, requireNonEmptyString, validateExpectedParams, validateRequiredParams, validateArgsSecurity } from './common-handlers.js';
import { sanitizeCommandArgument } from '../../utils/validation.js';

/**
 * Action aliases for test compatibility
 * Maps test action names to handler action names
 */
const EDITOR_ACTION_ALIASES: Record<string, string> = {
  'start_pie': 'play',
  'focus_actor': 'focus',
  'set_game_view_target': 'set_view_target',
  'set_camera_position': 'set_camera',
  'set_viewport_camera': 'set_camera',
  'take_screenshot': 'screenshot',
  'close_asset': 'close_asset',
  'save_all': 'save_all',
  'undo': 'undo',
  'redo': 'redo',
  'set_editor_mode': 'set_editor_mode',
  'show_stats': 'show_stats',
  'hide_stats': 'hide_stats',
  'set_game_view': 'set_game_view',
  'set_immersive_mode': 'set_immersive_mode',
  'single_frame_step': 'step_frame',
  'set_fixed_delta_time': 'set_fixed_delta_time',
  'open_level': 'open_level',
};

/**
 * Idempotent actions that accept success even with invalid/missing params.
 * These are global commands that have sensible defaults or no-ops.
 * NOTE: Include both original and normalized action names for proper validation.
 */
const IDEMPOTENT_ACTIONS = new Set([
  'stop', 'stop_pie', 'pause', 'resume',
  'set_game_speed', 'set_fixed_delta_time',
  'set_immersive_mode', 'set_game_view',
  'show_stats', 'hide_stats',
  'undo', 'redo',
  'step_frame', 'single_frame_step'
]);

/**
 * Actions that require specific parameters.
 * Maps action name to array of required parameter names.
 * NOTE: Includes both original and normalized action names for proper validation.
 */
const ACTION_REQUIRED_PARAMS: Record<string, string[]> = {
  'focus_actor': ['actorName'],
  'focus': ['actorName'],  // Normalized alias for focus_actor
  'possess': ['actorName'],
  'set_camera': ['location', 'rotation'],
  'set_viewport_resolution': ['width', 'height'],
  'set_view_mode': ['viewMode'],
  'set_editor_mode': ['mode'],
  'set_camera_fov': ['fov'],
  'set_game_speed': ['speed'],
  'set_fixed_delta_time': ['deltaTime'],
  'set_preferences': ['category', 'preferences'],
  'execute_command': ['command'],
  'console_command': ['command'],
  'query_pie_actor': ['actorName'],
  'send_input': ['type'],
  'send_enhanced_input': ['key'],
};

/**
 * Actions that have specific allowed parameters.
 * Maps action name to array of allowed parameter names (excluding action/subAction/timeoutMs).
 * NOTE: Includes both original and normalized action names for proper validation.
 */
const ACTION_ALLOWED_PARAMS: Record<string, string[]> = {
  'play': ['pieMode', 'playerStart', 'pawnName'],
  'stop': [],
  'stop_pie': [],
  'pause': [],
  'resume': [],
  'eject': [],
  'possess': ['actorName'],
  'set_view_target': ['actorName', 'name', 'objectPath', 'location', 'rotation', 'blendTime'],
  'open_asset': ['assetPath', 'path'],
  'close_asset': ['assetPath', 'path'],
  'open_level': ['levelPath', 'path', 'assetPath'],
  'focus_actor': ['actorName', 'name'],
  'focus': ['actorName', 'name'],  // Normalized alias for focus_actor
  'set_camera': ['location', 'rotation', 'actorName'],
  'set_viewport_resolution': ['width', 'height'],
  'set_view_mode': ['viewMode'],
  'set_editor_mode': ['mode'],
  'set_camera_fov': ['fov'],
  'set_game_speed': ['speed'],
  'set_fixed_delta_time': ['deltaTime'],
  'screenshot': ['filename', 'path', 'outputPath', 'resolution', 'mode', 'returnBase64', 'includeMetadata', 'metadata', 'warmupFrames', 'screenshotDelayMs', 'captureMode'],
  'set_preferences': ['category', 'preferences'],
  'execute_command': ['command'],
  'console_command': ['command'],
  'undo': [],
  'redo': [],
  'save_all': [],
  'show_stats': ['stat'],
  'hide_stats': ['stat'],
  'set_game_view': ['enabled'],
  'set_immersive_mode': ['enabled'],
  'step_frame': ['steps'],
  'single_frame_step': ['steps'],
  'create_bookmark': ['id', 'description', 'bookmarkName'],
  'jump_to_bookmark': ['id', 'bookmarkName'],
  'start_recording': ['filename', 'name', 'frameRate', 'durationSeconds', 'metadata'],
  'stop_recording': [],
  'set_viewport_realtime': ['enabled', 'realtime'],
  'simulate_input': ['key', 'type', 'inputType', 'inputAction', 'x', 'y', 'button', 'playerIndex', 'axisName', 'axisValue', 'relative'],
  'get_pie_state': [],
  'query_pie_actor': ['actorName'],
  'get_pie_metrics': [],
  'detect_pie_issues': ['actorName', 'previousLocation', 'minMovementCm', 'expectedMovement'],
  'send_input': ['key', 'type', 'x', 'y', 'button', 'durationMs', 'playerIndex', 'axisName', 'axisValue', 'relative'],
  'send_enhanced_input': ['key', 'enhancedAction', 'type', 'durationMs', 'value', 'playerIndex'],
  'move': ['key', 'axisX', 'axisY', 'durationMs', 'playerIndex'],
  'look': ['x', 'y', 'durationMs', 'playerIndex'],
  'jump': ['key', 'durationMs', 'playerIndex'],
  'sprint': ['key', 'durationMs', 'playerIndex'],
  'interact': ['key', 'durationMs', 'playerIndex', 'interfaceName'],
  'capture_pie_screenshot': ['filename', 'resolution', 'returnBase64', 'includeMetadata', 'metadata', 'warmupFrames', 'screenshotDelayMs', 'captureMode'],
  'read_pie_logs': ['standalone'],
  'run_playtest_sequence': ['sequence', 'autoStop', 'timeoutMs', 'pieMode', 'playerIndex', 'warmupFrames', 'screenshotDelayMs'],
};

const INPUT_TYPE_ALIASES: Record<string, string> = {
  press: 'key_down',
  pressed: 'key_down',
  down: 'key_down',
  release: 'key_up',
  released: 'key_up',
  up: 'key_up',
  click: 'mouse_click',
  move: 'mouse_move',
  analog: 'axis',
};

const SUPPORTED_INPUT_TYPES = new Set(['key_down', 'key_up', 'mouse_click', 'mouse_move', 'axis', 'axis_input']);
const EDITOR_ASSET_PATH_ACTIONS = new Set(['open_asset', 'close_asset', 'open_level']);
const EDITOR_PATH_FIELDS = ['assetPath', 'levelPath', 'path'] as const;
const SUPPORTED_SCREENSHOT_MODES = new Set(['editor_viewport', 'game_viewport', 'full_editor_window', 'standalone_window']);

/**
 * Normalize editor action names for test compatibility
 */
function normalizeEditorAction(action: string): string {
  return EDITOR_ACTION_ALIASES[action] ?? action;
}

/**
 * Validates arguments for editor actions.
 * For non-idempotent actions, validates that only expected parameters are present.
 * Always validates security patterns (path traversal, etc).
 */
function validateEditorActionArgs(
  action: string,
  args: Record<string, unknown>
): void {
  // Always validate security patterns first
  validateArgsSecurity({ action, ...args } as Record<string, unknown>);

  if (['simulate_input', 'send_input', 'send_enhanced_input', 'move', 'look', 'jump', 'sprint', 'interact', 'run_playtest_sequence'].includes(action) && args.actorName !== undefined) {
    throw new Error('actorName is obsolete for PIE input. Use playerIndex (controller/local-player index) instead.');
  }

  // Validate required parameters FIRST (applies to ALL actions including idempotent)
  // This ensures required param validation is not skipped for idempotent actions
  const requiredParams = ACTION_REQUIRED_PARAMS[action];
  if (requiredParams !== undefined) {
    validateRequiredParams(args, requiredParams, `control_editor:${action}`);
  }

  // Idempotent actions skip allowed params validation (they accept extras gracefully)
  // But they still require their required params to be present (validated above)
  if (IDEMPOTENT_ACTIONS.has(action)) {
    return;
  }

  // Validate that only expected parameters are present for non-idempotent actions
  const allowedParams = ACTION_ALLOWED_PARAMS[action];
  if (allowedParams !== undefined) {
    validateExpectedParams(args, allowedParams, `control_editor:${action}`);
  }
}

function getInputType(args: EditorArgs): string {
  const inputTypeValue = args.type ?? args.inputType ?? args.inputAction;
  if (typeof inputTypeValue !== 'string' || inputTypeValue.trim() === '') {
    throw new Error('Missing required parameters for control_editor:simulate_input: [type|inputType|inputAction]');
  }

  const normalized = inputTypeValue.trim().toLowerCase();
  if ((normalized === 'key' || normalized === 'keyboard') && typeof args.inputAction === 'string') {
    const normalizedInputAction = args.inputAction.trim().toLowerCase();
    const mappedActionType = INPUT_TYPE_ALIASES[normalizedInputAction] ?? normalizedInputAction;
    if (mappedActionType === 'key_down' || mappedActionType === 'key_up') {
      return mappedActionType;
    }
  }

  const mappedType = INPUT_TYPE_ALIASES[normalized] ?? normalized;
  if (!SUPPORTED_INPUT_TYPES.has(mappedType)) {
    throw new Error(`Unknown input type: ${inputTypeValue}. Supported: key_down, key_up, mouse_click, mouse_move, axis`);
  }

  return mappedType;
}

function getScreenshotMode(args: EditorArgs): { mode?: string; error?: string } {
  if (typeof args.mode !== 'string' || args.mode.trim() === '') {
    return {};
  }

  const mode = args.mode.trim().toLowerCase();
  if (!SUPPORTED_SCREENSHOT_MODES.has(mode)) {
    return { error: `Unknown screenshot mode: ${args.mode}. Supported: editor_viewport, game_viewport, full_editor_window` };
  }

  return { mode };
}

const MAX_PLAYTEST_TIMEOUT_MS = 300_000;

function getBoundedTimeoutMs(value: unknown): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) return 60_000;
  return Math.max(1, Math.min(Math.floor(value), MAX_PLAYTEST_TIMEOUT_MS));
}

function getBoundedDurationMs(value: unknown, fallback: number = 100): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) return fallback;
  return Math.max(1, Math.min(Math.floor(value), 120_000));
}

function delay(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function isSuccessful(response: unknown): boolean {
  return typeof response === 'object' && response !== null &&
    (response as Record<string, unknown>).success === true;
}

function getBooleanField(response: unknown, field: string): boolean | undefined {
  if (!response || typeof response !== 'object') return undefined;
  const record = response as Record<string, unknown>;
  if (typeof record[field] === 'boolean') return record[field];
  const result = record.result;
  return result && typeof result === 'object' && typeof (result as Record<string, unknown>)[field] === 'boolean'
    ? (result as Record<string, unknown>)[field] as boolean
    : undefined;
}

async function stopPieAndVerify(tools: ITools, timeoutMs: number): Promise<Record<string, unknown>> {
  const options = { timeoutMs };
  const stopResult = cleanObject(await executeAutomationRequest(tools, 'control_editor', { action: 'stop' }, undefined, options) as Record<string, unknown>);
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const state = await executeAutomationRequest(tools, 'control_editor', { action: 'get_pie_state' }, undefined, options) as Record<string, unknown>;
    if (getBooleanField(state, 'isInPIE') === false) return { ...stopResult, pieStopped: true };
    await delay(50);
  }
  return { ...stopResult, success: false, pieStopped: false, error: 'PIE_STOP_TIMEOUT', message: 'PIE did not stop before the cleanup deadline.' };
}

async function sendHeldPieKey(tools: ITools, key: string, durationMs: number, timeoutMs: number, playerIndex = 0, interfaceName?: string): Promise<Record<string, unknown>> {
  const options = { timeoutMs };
  let pressed = false;
  try {
    const down = await executeAutomationRequest(tools, 'control_editor', {
      action: 'simulate_input', type: 'key_down', key, playerIndex,
      ...(interfaceName && interfaceName.trim().length > 0 ? { interfaceName: interfaceName.trim() } : {})
    }, undefined, options) as Record<string, unknown>;
    pressed = isSuccessful(down);
    if (!pressed) return cleanObject(down);
    await delay(durationMs);
    return cleanObject(await executeAutomationRequest(tools, 'control_editor', {
      action: 'simulate_input', type: 'key_up', key, playerIndex
    }, undefined, options) as Record<string, unknown>);
  } finally {
    // A timeout/error after key-down must never leave an input latched in PIE.
    if (pressed) {
      try {
        await executeAutomationRequest(tools, 'control_editor', {
          action: 'simulate_input', type: 'key_up', key, playerIndex
        }, undefined, options);
      } catch {
        // The primary request error is more useful; PIE cleanup still runs in the caller.
      }
    }
  }
}

async function waitForPieReady(tools: ITools, timeoutMs: number): Promise<void> {
  const deadline = Date.now() + Math.min(timeoutMs, 15_000);
  while (Date.now() < deadline) {
    const state = await executeAutomationRequest(tools, 'control_editor', { action: 'get_pie_state' }, undefined, { timeoutMs: Math.min(timeoutMs, 5_000) }) as Record<string, unknown>;
    if (getBooleanField(state, 'isInPIE') === true) return;
    await delay(50);
  }
  throw new Error('PIE did not become ready before the timeout.');
}

function getMoveKey(args: EditorArgs): string {
  const x = typeof args.axisX === 'number' ? args.axisX : 0;
  const y = typeof args.axisY === 'number' ? args.axisY : 1;
  if (Math.abs(y) >= Math.abs(x)) return y < 0 ? 'S' : 'W';
  return x < 0 ? 'A' : 'D';
}

async function runPlaytestSequence(args: EditorArgs, tools: ITools): Promise<Record<string, unknown>> {
  const sequence = Array.isArray(args.sequence) ? args.sequence : [];
  const timeoutMs = getBoundedTimeoutMs(args.timeoutMs);
  const autoStop = args.autoStop !== false;
  const startedAt = Date.now();
  const steps: Array<Record<string, unknown>> = [];
  let failure: string | undefined;

  try {
    for (let index = 0; index < sequence.length; index += 1) {
      const step = sequence[index];
      const action = typeof step.action === 'string' ? step.action : '';
      if (!action) throw new Error(`Play-test step ${index + 1} is missing action`);
      const stepArgs: EditorArgs = { ...args, ...step, action, timeoutMs: getBoundedTimeoutMs(step.timeoutMs ?? timeoutMs) };
      delete stepArgs.sequence;
      delete stepArgs.autoStop;
      const startedStepAt = Date.now();
      const result = await handleEditorTools(action, stepArgs, tools) as Record<string, unknown>;
      const passed = isSuccessful(result);
      steps.push({ index, action, passed, durationMs: Date.now() - startedStepAt, result });
      if (!passed) throw new Error(`Play-test step ${index + 1} (${action}) failed`);
    }
  } catch (error) {
    failure = error instanceof Error ? error.message : String(error);
  } finally {
    if (autoStop) {
      try {
        const stopResult = await stopPieAndVerify(tools, timeoutMs);
        steps.push({ action: 'stop', cleanup: true, passed: isSuccessful(stopResult), result: stopResult });
      } catch (error) {
        failure = failure ?? `PIE cleanup failed: ${error instanceof Error ? error.message : String(error)}`;
      }
    }
  }

  const passed = failure === undefined;
  const report = {
    passed,
    startedAt: new Date(startedAt).toISOString(),
    durationMs: Date.now() - startedAt,
    autoStop,
    savedRuntimeChanges: false,
    steps,
    failure
  };
  return {
    success: passed,
    report,
    summary: passed
      ? `Play-test passed (${steps.filter(step => step.passed === true).length} steps; PIE stopped).`
      : `Play-test failed: ${failure}. PIE cleanup was requested.`,
    ...(passed ? {} : { error: 'PLAYTEST_FAILED', message: failure })
  };
}

export async function handleEditorTools(action: string, args: EditorArgs, tools: ITools) {
  // Normalize action name for test compatibility
  const normalizedAction = normalizeEditorAction(action);

  // Validate arguments for this action
  const argsRecord = EDITOR_ASSET_PATH_ACTIONS.has(normalizedAction)
    ? normalizePathFields(args as Record<string, unknown>, EDITOR_PATH_FIELDS)
    : args as Record<string, unknown>;
  validateEditorActionArgs(normalizedAction, argsRecord);
  const editorArgs = argsRecord as EditorArgs;

  switch (normalizedAction) {
    case 'play': {
      const res = await executeAutomationRequest(tools, 'control_editor', {
        action: 'play', pieMode: editorArgs.pieMode, playerStart: editorArgs.playerStart, pawnName: editorArgs.pawnName
      }, undefined, { timeoutMs: getBoundedTimeoutMs(args.timeoutMs) }) as Record<string, unknown>;
      if (isSuccessful(res) && editorArgs.pieMode !== 'standalone') {
        await waitForPieReady(tools, getBoundedTimeoutMs(args.timeoutMs));
      }
      return cleanObject(res);
    }
    case 'stop':
    case 'stop_pie': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'stop' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'eject': {
      // CRITICAL FIX: Removed redundant isInPIE() check that caused race condition.
      // PIE state is now validated atomically in the C++ handler (HandleControlEditorEject)
      // which checks GEditor->PlayWorld directly before executing the eject command.
      // This prevents the race where PIE stops between TS check and C++ execution.
      return await executeAutomationRequest(tools, 'control_editor', { action: 'eject' });
    }
    case 'possess': {
      // CRITICAL FIX: Removed redundant isInPIE() check that caused race condition.
      // PIE state is now validated atomically in the C++ handler (HandleControlEditorPossess)
      // which checks GEditor->PlayWorld directly before executing the possess command.
      // This prevents the race where PIE stops between TS check and C++ execution.
      return await executeAutomationRequest(tools, 'control_editor', args);
    }
    case 'set_view_target': {
      const actorName = requireNonEmptyString(editorArgs.actorName ?? editorArgs.name ?? editorArgs.objectPath, 'actorName');
      const res = await executeAutomationRequest(tools, 'control_editor', {
        action: 'set_view_target',
        actorName,
        objectPath: editorArgs.objectPath,
        location: editorArgs.location,
        rotation: editorArgs.rotation,
        blendTime: typeof editorArgs.blendTime === 'number' ? editorArgs.blendTime : undefined
      }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'pause': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'pause' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'resume': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'resume' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'screenshot': {
      const filename = args.filename ?? args.path;
      const modeResult = getScreenshotMode(args);
      if (modeResult.error) {
        return {
          success: false,
          type: 'INVALID_ARGUMENT',
          error: 'INVALID_ARGUMENT',
          message: modeResult.error,
          action: 'screenshot'
        };
      }

      const mode = modeResult.mode;
      const payload: Record<string, unknown> = { action: 'screenshot', filename, resolution: args.resolution };
      if (typeof args.outputPath === 'string' && args.outputPath.trim().length > 0) {
        payload.outputPath = args.outputPath.trim();
      }
      if (mode !== undefined) {
        payload.mode = mode;
      }
      if (typeof args.returnBase64 === 'boolean') {
        payload.returnBase64 = args.returnBase64;
      } else if (mode === 'full_editor_window' || mode === 'game_viewport') {
        payload.returnBase64 = true;
      }
      if (args.includeMetadata === true) {
        payload.includeMetadata = true;
      }
      if (args.includeMetadata === true && args.metadata !== undefined) {
        payload.metadata = args.metadata;
      }
      if (typeof args.warmupFrames === 'number') payload.warmupFrames = args.warmupFrames;
      if (typeof args.screenshotDelayMs === 'number') payload.screenshotDelayMs = args.screenshotDelayMs;
      if (typeof args.captureMode === 'string') payload.captureMode = args.captureMode;

      const targetAction = mode === 'game_viewport' ? 'system_control' : 'control_editor';
      const res = await executeAutomationRequest(tools, targetAction, payload) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'console_command': {
      const res = await executeAutomationRequest(tools, 'console_command', { command: args.command ?? '' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_camera': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_camera', location: args.location, rotation: args.rotation }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'start_recording': {
      // Use console command as fallback if bridge doesn't support it
      const name = typeof args.name === 'string' ? args.name : undefined;
      const filename = args.filename || name || 'TestRecording';
      const frameRate = typeof args.frameRate === 'number' ? args.frameRate : undefined;
      const durationSeconds = typeof args.durationSeconds === 'number' ? args.durationSeconds : undefined;
      const metadata = args.metadata;

      const safeFilename = sanitizeCommandArgument(filename);

      // Try automation bridge first with all params
      try {
        const res = await executeAutomationRequest(tools, 'control_editor', {
          action: 'start_recording',
          filename, // JSON path - use raw value (JSON.stringify handles escaping)
          frameRate,
          durationSeconds,
          metadata
        });
        return cleanObject(res);
      } catch {
        if (!safeFilename) {
          return { success: false, error: 'Filename is required after sanitization', action: 'start_recording' };
        }
        // Fallback to console command
        await executeAutomationRequest(tools, 'console_command', { command: `DemoRec ${safeFilename}` });
        return {
          success: true,
          message: `Started recording to ${safeFilename}`,
          action: 'start_recording',
          filename: safeFilename,
          frameRate,
          durationSeconds
        };
      }
    }
    case 'stop_recording': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'stop_recording' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'step_frame': {
      // Support stepping multiple frames
      const steps = typeof args.steps === 'number' && args.steps > 0 ? args.steps : 1;
      for (let i = 0; i < steps; i++) {
        await executeAutomationRequest(tools, 'control_editor', { action: 'step_frame' });
      }
      return { success: true, message: `Stepped ${steps} frame(s)`, action: 'step_frame', steps };
    }
    case 'create_bookmark': {
      const idx = typeof args.id === 'number' ? Math.trunc(args.id) : (parseInt(args.bookmarkName ?? '0') || 0);
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'create_bookmark', index: idx }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'jump_to_bookmark': {
      const idx = typeof args.id === 'number' ? Math.trunc(args.id) : (parseInt(args.bookmarkName ?? '0') || 0);
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'jump_to_bookmark', index: idx }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_preferences': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_preferences', category: args.category ?? '', preferences: args.preferences ?? {} }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'open_asset': {
      const assetPath = requireNonEmptyString(editorArgs.assetPath || editorArgs.path, 'assetPath');
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'open_asset', assetPath });
      return cleanObject(res);
    }
    case 'execute_command': {
      const command = requireNonEmptyString(args.command, 'command');
      const res = await executeAutomationRequest(tools, 'console_command', { command }) as Record<string, unknown>;
      return { ...cleanObject(res), action: 'execute_command' };
    }
    case 'set_camera_fov': {
      const safeFov = sanitizeCommandArgument(String(args.fov));
      if (!safeFov) return { success: false, error: 'FOV is required after sanitization', action: 'set_camera_fov' };
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_camera_fov', fov: Number(safeFov) }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_game_speed': {
      const safeSpeed = sanitizeCommandArgument(String(args.speed));
      if (!safeSpeed) return { success: false, error: 'Speed is required after sanitization', action: 'set_game_speed' };
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_game_speed', speed: Number(safeSpeed) }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_view_mode': {
      const viewMode = requireNonEmptyString(args.viewMode, 'viewMode');
      const validModes = [
        'Lit', 'Unlit', 'Wireframe', 'DetailLighting', 'LightingOnly', 'Reflections',
        'OptimizationViewmodes', 'ShaderComplexity', 'LightmapDensity', 'StationaryLightOverlap', 'LightComplexity',
        'PathTracing', 'Visualizer', 'LODColoration', 'CollisionPawn', 'CollisionVisibility'
      ];
      if (!validModes.includes(viewMode)) {
        throw new Error(`unknown_viewmode: ${viewMode}. Must be one of: ${validModes.join(', ')}`);
      }
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_view_mode', viewMode }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_viewport_resolution': {
      const width = Number(args.width);
      const height = Number(args.height);
      if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
        return {
          success: false,
          error: 'VALIDATION_ERROR',
          message: 'Width and height must be positive numbers',
          action: 'set_viewport_resolution'
        };
      }
      const res = await executeAutomationRequest(tools, 'console_command', { command: `r.SetRes ${width}x${height}` }) as Record<string, unknown>;
      return cleanObject({ ...res, action: 'set_viewport_resolution', width, height });
    }
    case 'set_viewport_realtime': {
      const enabled = args.enabled !== undefined ? args.enabled : (args.realtime !== false);
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_viewport_realtime', enabled, realtime: enabled }) as Record<string, unknown>;
      return cleanObject(res);
    }
    // Additional handlers for test compatibility
    case 'close_asset': {
      const assetPath = requireNonEmptyString(editorArgs.assetPath || editorArgs.path, 'assetPath');
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'close_asset', assetPath });
      return cleanObject(res);
    }
    case 'save_all': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'save_all' });
      return cleanObject(res);
    }
    case 'undo': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'undo' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'redo': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'redo' }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_editor_mode': {
      const mode = requireNonEmptyString(args.mode, 'mode');
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_editor_mode', mode });
      return cleanObject(res);
    }
    case 'show_stats': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'show_stats', stat: args.stat }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'hide_stats': {
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'hide_stats', stat: args.stat }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_game_view': {
      const enabled = args.enabled !== false;
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_game_view', enabled }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_immersive_mode': {
      const enabled = args.enabled !== false;
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_immersive_mode', enabled }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'set_fixed_delta_time': {
      const deltaTime = typeof args.deltaTime === 'number' ? args.deltaTime : 0.01667;
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'set_fixed_delta_time', deltaTime }) as Record<string, unknown>;
      return cleanObject(res);
    }
    case 'open_level': {
      // Accept 'assetPath' as alias since users commonly think of level paths as asset paths
      const levelPath = requireNonEmptyString(editorArgs.levelPath || editorArgs.path || editorArgs.assetPath, 'levelPath');
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'open_level', levelPath });
      return cleanObject(res);
    }
    case 'simulate_input': {
      const mappedType = getInputType(args);

      const res = await executeAutomationRequest(tools, 'control_editor', {
        action: 'simulate_input',
        type: mappedType,
        key: args.key,
        x: args.x,
        y: args.y,
        button: args.button,
        ...(args.playerIndex !== undefined ? { playerIndex: args.playerIndex } : {}),
        ...(args.axisName !== undefined ? { axisName: args.axisName } : {}),
        ...(args.axisValue !== undefined ? { axisValue: args.axisValue } : {}),
        ...(args.relative !== undefined ? { relative: args.relative } : {})
      });
      return cleanObject(res);
    }
    case 'get_pie_state':
    case 'query_pie_actor':
    case 'get_pie_metrics':
    case 'detect_pie_issues':
    case 'read_pie_logs': {
      return cleanObject(await executeAutomationRequest(tools, 'control_editor', editorArgs, undefined, {
        timeoutMs: getBoundedTimeoutMs(args.timeoutMs)
      }) as Record<string, unknown>);
    }
    case 'capture_pie_screenshot': {
      // Screenshot responses are emitted by the system-control capture path. Routing
      // through it preserves the consolidated action contract while still selecting
      // the PIE game viewport in the payload.
      return cleanObject(await executeAutomationRequest(tools, 'system_control', {
        action: 'screenshot', filename: editorArgs.filename, resolution: editorArgs.resolution,
        mode: 'game_viewport', returnBase64: editorArgs.returnBase64 ?? true,
        includeMetadata: editorArgs.includeMetadata, metadata: editorArgs.metadata,
        ...(typeof editorArgs.warmupFrames === 'number' ? { warmupFrames: editorArgs.warmupFrames } : {}),
        ...(typeof editorArgs.screenshotDelayMs === 'number' ? { screenshotDelayMs: editorArgs.screenshotDelayMs } : {}),
        ...(typeof editorArgs.captureMode === 'string' ? { captureMode: editorArgs.captureMode } : {})
      }, undefined, { timeoutMs: getBoundedTimeoutMs(args.timeoutMs) }) as Record<string, unknown>);
    }
    case 'send_input': {
      const mappedType = getInputType(editorArgs);
      if (mappedType === 'key_down' && typeof editorArgs.durationMs === 'number') {
        return sendHeldPieKey(tools, requireNonEmptyString(editorArgs.key, 'key'), getBoundedDurationMs(editorArgs.durationMs), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0);
      }
      return cleanObject(await executeAutomationRequest(tools, 'control_editor', {
        action: 'simulate_input', type: mappedType, key: editorArgs.key, x: editorArgs.x, y: editorArgs.y, button: editorArgs.button,
        ...(editorArgs.playerIndex !== undefined ? { playerIndex: editorArgs.playerIndex } : {}),
        ...(editorArgs.axisName !== undefined ? { axisName: editorArgs.axisName } : {}),
        ...(editorArgs.axisValue !== undefined ? { axisValue: editorArgs.axisValue } : {}),
        ...(editorArgs.relative !== undefined ? { relative: editorArgs.relative } : {})
      }, undefined, { timeoutMs: getBoundedTimeoutMs(args.timeoutMs) }) as Record<string, unknown>);
    }
    case 'send_enhanced_input': {
      // Enhanced Input receives simulated keys through the PIE viewport's normal input stack.
      // Callers supply a mapped key; enhancedAction is retained for reporting by the bridge.
      const result = await sendHeldPieKey(tools, requireNonEmptyString(editorArgs.key, 'key'), getBoundedDurationMs(editorArgs.durationMs), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0);
      return cleanObject({ ...result, enhancedAction: editorArgs.enhancedAction, inputPath: 'pie_viewport' });
    }
    case 'move':
      return sendHeldPieKey(tools, editorArgs.key ?? getMoveKey(editorArgs), getBoundedDurationMs(editorArgs.durationMs, 250), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0);
    case 'look':
      return cleanObject(await executeAutomationRequest(tools, 'control_editor', {
        action: 'simulate_input', type: 'mouse_move', x: editorArgs.x ?? 0, y: editorArgs.y ?? 0, relative: true, playerIndex: editorArgs.playerIndex ?? 0
      }, undefined, { timeoutMs: getBoundedTimeoutMs(args.timeoutMs) }) as Record<string, unknown>);
    case 'jump':
      return sendHeldPieKey(tools, editorArgs.key ?? 'SpaceBar', getBoundedDurationMs(editorArgs.durationMs), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0);
    case 'sprint':
      return sendHeldPieKey(tools, editorArgs.key ?? 'LeftShift', getBoundedDurationMs(editorArgs.durationMs, 250), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0);
    case 'interact': {
      const interfaceName = typeof editorArgs.interfaceName === 'string' ? editorArgs.interfaceName : undefined;
      return await sendHeldPieKey(tools, editorArgs.key ?? 'E', getBoundedDurationMs(editorArgs.durationMs), getBoundedTimeoutMs(args.timeoutMs), editorArgs.playerIndex ?? 0, interfaceName);
    }
    case 'run_playtest_sequence':
      return runPlaytestSequence(editorArgs, tools);
    case 'focus':
    case 'focus_actor': {
      const actorName = requireNonEmptyString(args.actorName || args.name, 'actorName');
      const res = await executeAutomationRequest(tools, 'control_editor', { action: 'focus_actor', actorName });
      return cleanObject(res);
    }
    default:
      return await executeAutomationRequest(tools, 'control_editor', args);
  }
}
