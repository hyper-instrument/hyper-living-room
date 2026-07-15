#pragma once

#include <ESPAsyncWebServer.h>

// Registers an MCP (Model Context Protocol) endpoint at POST /mcp on the given
// server. Implements the Streamable-HTTP transport as plain JSON-RPC 2.0
// request/response (no SSE), which is enough for stateless tool calls.
//
// Exposed tools:
//   get_status    -> temperature, humidity, AC plug state, power, energy
//   set_ac_power  -> {on: bool} turn the AC (Tapo P110M) on/off
//
// Clients (Claude Code, etc.) connect to http://<host>.local/mcp
void mcp_register(AsyncWebServer& server);
