import net from 'node:net';
import http from 'node:http';
import { Logger } from '../utils/logger.js';

/**
 * Transport classification for a candidate automation bridge port.
 *
 * The Unreal plugin can expose two distinct transports:
 *  - A raw WebSocket automation bridge (plugin settings `ListenPorts`, e.g. 8090/8091 or 8092/8093).
 *  - A native HTTP/SSE MCP endpoint (e.g. http://127.0.0.1:3000/mcp).
 *
 * WebSocket clients must never dial the HTTP MCP endpoint, and connection errors
 * must clearly explain protocol mismatches instead of surfacing generic
 * "Unexpected server response" failures.
 */
export type PortProbeStatus =
    | 'ws-candidate'
    | 'http-endpoint'
    | 'closed'
    | 'unreachable';

export interface PortProbeResult {
    port: number;
    status: PortProbeStatus;
    /** HTTP status code when the port answered an HTTP probe. */
    httpStatus?: number;
    /** Content-Type header from the HTTP probe, when present. */
    contentType?: string;
    /** Extra human-readable detail for diagnostics. */
    detail?: string;
}

export interface PortDiscoveryReport {
    results: PortProbeResult[];
    /** First port that looks like a WebSocket bridge candidate. */
    selectedPort?: number;
    /** True when at least one probed port is an HTTP MCP endpoint. */
    httpEndpoints: PortProbeResult[];
    closedPorts: number[];
}

const HTTP_PROBE_TIMEOUT_MS = 1200;
const TCP_PROBE_TIMEOUT_MS = 800;

function probeTcpConnect(host: string, port: number, timeoutMs: number): Promise<boolean> {
    return new Promise((resolve) => {
        const socket = new net.Socket();
        let settled = false;
        const finish = (ok: boolean): void => {
            if (settled) return;
            settled = true;
            socket.destroy();
            resolve(ok);
        };
        socket.setTimeout(timeoutMs);
        socket.once('connect', () => finish(true));
        socket.once('timeout', () => finish(false));
        socket.once('error', () => finish(false));
        try {
            socket.connect(port, host);
        } catch {
            finish(false);
        }
    });
}

interface HttpProbeOutcome {
    responded: boolean;
    status?: number;
    contentType?: string;
}

function probeHttpHead(host: string, port: number, timeoutMs: number): Promise<HttpProbeOutcome> {
    return new Promise((resolve) => {
        let settled = false;
        const finish = (outcome: HttpProbeOutcome): void => {
            if (settled) return;
            settled = true;
            resolve(outcome);
        };
        const timer = setTimeout(() => finish({ responded: false }), timeoutMs);

        let request: http.ClientRequest;
        try {
            request = http.get(
                { host, port, path: '/', method: 'GET', timeout: timeoutMs, headers: { 'MCP-Probe': 'transport-detection' } },
                (response) => {
                    const status = response.statusCode ?? 0;
                    const contentType = String(response.headers['content-type'] ?? '');
                    response.resume();
                    response.once('end', () => {
                        clearTimeout(timer);
                        finish({ responded: true, status, contentType });
                    });
                    // Do not wait for the full body on SSE streams.
                    if (String(contentType).includes('text/event-stream')) {
                        response.destroy();
                        clearTimeout(timer);
                        finish({ responded: true, status, contentType });
                    }
                }
            );
        } catch {
            clearTimeout(timer);
            finish({ responded: false });
            return;
        }

        request.on('timeout', () => {
            request.destroy();
            clearTimeout(timer);
            finish({ responded: false });
        });
        request.on('error', () => {
            clearTimeout(timer);
            finish({ responded: false });
        });
    });
}

function classifyHttpOutcome(outcome: HttpProbeOutcome): PortProbeStatus | undefined {
    if (!outcome.responded) return undefined;
    const contentType = outcome.contentType ?? '';
    // WebSocket servers reject plain HTTP GETs without an Upgrade header.
    if (outcome.status === 400 || outcome.status === 426) {
        if (!contentType.includes('json') && !contentType.includes('event-stream')) {
            return 'ws-candidate';
        }
    }
    return 'http-endpoint';
}

/**
 * Probe candidate ports and classify the transport each one speaks.
 *
 * Ports that answer with a valid HTTP response other than a WebSocket
 * upgrade rejection are classified as `http-endpoint` and must never be
 * dialed with a WebSocket client.
 */
export async function discoverAutomationPorts(
    host: string,
    ports: number[],
    log?: Logger
): Promise<PortDiscoveryReport> {
    const results: PortProbeResult[] = [];
    const httpEndpoints: PortProbeResult[] = [];
    const closedPorts: number[] = [];
    let selectedPort: number | undefined;

    for (const port of ports) {
        const open = await probeTcpConnect(host, port, TCP_PROBE_TIMEOUT_MS);
        if (!open) {
            results.push({ port, status: 'closed' });
            closedPorts.push(port);
            log?.debug(`Port ${port} on ${host} is closed`);
            continue;
        }

        const httpOutcome = await probeHttpHead(host, port, HTTP_PROBE_TIMEOUT_MS);
        const classified = classifyHttpOutcome(httpOutcome);
        if (classified === 'http-endpoint') {
            const entry: PortProbeResult = {
                port,
                status: 'http-endpoint',
                httpStatus: httpOutcome.status,
                contentType: httpOutcome.contentType,
                detail:
                    `Port ${port} answered HTTP ${httpOutcome.status}` +
                    (httpOutcome.contentType ? ` (${httpOutcome.contentType})` : '') +
                    '. This looks like the native MCP HTTP/SSE endpoint, not the WebSocket automation bridge.'
            };
            results.push(entry);
            httpEndpoints.push(entry);
            log?.warn(entry.detail);
            continue;
        }

        // Either a WS upgrade rejection (400/426) or no HTTP response at all:
        // both are consistent with a raw WebSocket bridge listener.
        const entry: PortProbeResult = { port, status: 'ws-candidate' };
        if (classified === 'ws-candidate') {
            entry.httpStatus = httpOutcome.status;
        }
        results.push(entry);
        if (selectedPort === undefined) {
            selectedPort = port;
        }
    }

    return { results, selectedPort, httpEndpoints, closedPorts };
}

export function describePortDiscoveryReport(report: PortDiscoveryReport): string {
    const parts = report.results.map((entry) => {
        switch (entry.status) {
            case 'http-endpoint':
                return `${entry.port}:http-mcp-endpoint`;
            case 'closed':
                return `${entry.port}:closed`;
            case 'unreachable':
                return `${entry.port}:unreachable`;
            default:
                return `${entry.port}:ws`;
        }
    });
    return parts.join(', ');
}
