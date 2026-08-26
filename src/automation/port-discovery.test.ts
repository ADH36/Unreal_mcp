import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import net from 'node:net';
import http from 'node:http';
import { discoverAutomationPorts } from './port-discovery.js';
import { AutomationBridge } from './bridge.js';

describe('port-discovery', () => {
    describe('discoverAutomationPorts classification', () => {
        let wsLikeServer: net.Server;
        let httpServer: http.Server;
        let wsLikePort = 0;
        let httpPort = 0;

        beforeAll(async () => {
            // A raw WebSocket-style listener: speaks HTTP upgrade only; a plain GET
            // should get rejected (400) or the socket dies without a valid HTTP response.
            wsLikeServer = net.createServer((socket) => {
                socket.on('data', () => {
                    socket.end('HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n');
                });
            });
            await new Promise<void>((resolve) => wsLikeServer.listen(0, '127.0.0.1', resolve));
            wsLikePort = (wsLikeServer.address() as net.AddressInfo).port;

            // An HTTP MCP-style endpoint: answers GET with 200 + JSON.
            httpServer = http.createServer((_req, res) => {
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end('{}');
            });
            await new Promise<void>((resolve) => httpServer.listen(0, '127.0.0.1', resolve));
            httpPort = (httpServer.address() as net.AddressInfo).port;
        });

        afterAll(async () => {
            await new Promise<void>((resolve) => wsLikeServer.close(() => resolve()));
            await new Promise<void>((resolve) => httpServer.close(() => resolve()));
        });

        it('classifies a WebSocket-bridge-like listener as ws-candidate', async () => {
            const report = await discoverAutomationPorts('127.0.0.1', [wsLikePort]);
            expect(report.results[0].status).toBe('ws-candidate');
            expect(report.selectedPort).toBe(wsLikePort);
            expect(report.httpEndpoints).toHaveLength(0);
        });

        it('classifies an HTTP JSON endpoint as http-endpoint and never selects it', async () => {
            const report = await discoverAutomationPorts('127.0.0.1', [httpPort]);
            expect(report.results[0].status).toBe('http-endpoint');
            expect(report.selectedPort).toBeUndefined();
            expect(report.httpEndpoints).toHaveLength(1);
            expect(report.httpEndpoints[0].detail).toContain('native MCP');
        });

        it('prefers the first ws candidate when both kinds are present', async () => {
            const report = await discoverAutomationPorts('127.0.0.1', [httpPort, wsLikePort]);
            expect(report.selectedPort).toBe(wsLikePort);
            expect(report.httpEndpoints.map((entry) => entry.port)).toEqual([httpPort]);
        });

        it('reports closed ports as closed', async () => {
            const report = await discoverAutomationPorts('127.0.0.1', [1]);
            expect(report.results[0].status).toBe('closed');
            expect(report.selectedPort).toBeUndefined();
            expect(report.closedPorts).toEqual([1]);
        });
    });

    describe('AutomationBridge transport configuration', () => {
        it('includes default fallback candidates beyond configured ports', () => {
            const bridge = new AutomationBridge({ host: '127.0.0.1', port: 8092, ports: [8092, 8093] });
            const status = bridge.getStatus();
            expect(status.configuredPorts).toEqual([8092, 8093, 8090, 8091]);
        });

        it('restricts probing to an explicitly configured client port', () => {
            const bridge = new AutomationBridge({ host: '127.0.0.1', clientPort: 8517 });
            expect(bridge.getStatus().configuredPorts).toEqual([8517]);
        });

        it('exposes transport discovery diagnostics slots in status', () => {
            const bridge = new AutomationBridge({ host: '127.0.0.1', port: 8092 });
            const status = bridge.getStatus();
            expect(status.transportDiscovery).toBeNull();
            expect(status.httpMcpEndpointsDetected).toEqual([]);
        });
    });
});
