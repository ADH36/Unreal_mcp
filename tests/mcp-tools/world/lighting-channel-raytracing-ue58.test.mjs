#!/usr/bin/env node
/**
 * Advanced lighting channel + ray-tracing configuration integration tests.
 * Covers the UE 5.8 phase-29.1 build_environment actions: per-light ray-traced
 * feature toggles, path tracing, global ray-tracing quality knobs, and light
 * channel assignment/inspection across one or all components.
 */

import { runToolTests } from '../../test-runner.mjs';

const ts = Date.now();
const DEMO_LIGHT_NAME = `MCP_DirectionalLight_${ts}`;

const testCases = [
  // === SETUP ===
  { scenario: 'Setup: spawn directional demo light for channel tests', toolName: 'build_environment', arguments: { action: 'create_dynamic_light', name: DEMO_LIGHT_NAME, lightType: 'Directional', intensity: 5 }, expected: 'success|already exists' },

  // === RAY-TRACED FEATURE CONFIGURATION ===
  { scenario: 'RT: configure_ray_traced_shadows toggles shadows and culling radius', toolName: 'build_environment', arguments: { action: 'configure_ray_traced_shadows', lightName: DEMO_LIGHT_NAME, rayTracedShadows: true, cullingRadius: 10000 }, expected: 'success' },
  { scenario: 'RT: configure_ray_traced_gi with bounce budget', toolName: 'build_environment', arguments: { action: 'configure_ray_traced_gi', rayTracedGI: true, maxBounces: 2 }, expected: 'success' },
  { scenario: 'RT: configure_ray_traced_reflections roughness cutoff', toolName: 'build_environment', arguments: { action: 'configure_ray_traced_reflections', rayTracedReflections: true, maxRoughness: 0.6 }, expected: 'success' },
  { scenario: 'RT: configure_ray_traced_ao with ambient occlusion tuning', toolName: 'build_environment', arguments: { action: 'configure_ray_traced_ao', rayTracedAO: true, aoRadius: 50, aoIntensity: 1 }, expected: 'success' },
  { scenario: 'RT: configure_path_tracing sample budget and denoiser', toolName: 'build_environment', arguments: { action: 'configure_path_tracing', pathTracing: true, samplesPerPixel: 1024, denoiser: true }, expected: 'success' },
  { scenario: 'RT: configure_ray_traced_translucency refraction and denoiser channels', toolName: 'build_environment', arguments: { action: 'configure_ray_traced_translucency', rayTracedTranslucency: true, includeTranslucentObjects: true, refraction: true, refractionRays: 2, spatialDenoiserType: 1, maxBounces: 3 }, expected: 'success' },

  // === GLOBAL QUALITY / RESIDENCY ===
  { scenario: 'RT: configure_ray_tracing_quality residency and feedback knobs', toolName: 'build_environment', arguments: { action: 'configure_ray_tracing_quality', geometry: { mode: 'dynamic' }, useReferenceBasedResidency: true, residentGeometryMemoryPoolSizeInMB: 2048, compactInstances: true, priorityBasedUpdate: true, useTracingFeedback: true, reflectionCaptures: true, includeTranslucentObjects: true, maxUpdatePrimitivesPerFrame: 64, cullingMode: 1, cullingAngle: 30, cullingRadius: 25000 }, expected: 'success' },

  // === LIGHT CHANNELS ===
  { scenario: 'CHANNEL: set_light_channel assigns a numeric channel to a named light', toolName: 'build_environment', arguments: { action: 'set_light_channel', lightName: DEMO_LIGHT_NAME, channel: 1 }, expected: 'success' },
  { scenario: 'CHANNEL: set_actor_light_channel applies the channel map to actor components', toolName: 'build_environment', arguments: { action: 'set_actor_light_channel', actorName: DEMO_LIGHT_NAME, channels: { 0: true, 1: false, 2: false }, applyToAllComponents: true }, expected: 'success' },
  { scenario: 'CHANNEL: get_light_channels reports per-actor channel state via lightPath', toolName: 'build_environment', arguments: { action: 'get_light_channels', lightPath: `${DEMO_LIGHT_NAME}` }, expected: 'success|not found' },

  // === CLEANUP ===
  { scenario: 'Cleanup: delete demo light actor', toolName: 'control_actor', arguments: { action: 'delete', actorName: DEMO_LIGHT_NAME }, expected: 'success|not found' },
];

runToolTests('lighting-channel-raytracing-ue58', testCases);
