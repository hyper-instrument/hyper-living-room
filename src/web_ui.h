#pragma once

// Starts the HTTP server (port 80): phone UI at "/" plus a small JSON API.
// Plug commands are queued into g_state and executed by the main loop —
// never call blocking Tapo HTTP from the async_tcp task.
void web_ui_start();
