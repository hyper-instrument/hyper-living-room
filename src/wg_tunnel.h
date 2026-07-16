#pragma once

// Optional WireGuard tunnel: the Stick dials out to a WireGuard server (VPS)
// so phones/laptops on the same VPN can reach it from anywhere at WG_LOCAL_IP.
// Disabled entirely when WG_PRIVATE_KEY in .env is empty.
//
// Call service() from loop(): it brings the tunnel up once WiFi is connected
// and NTP time is valid (the WireGuard handshake needs a real clock), and then
// sends a small keepalive packet through the tunnel every ~20 s — the Stick
// sits behind home NAT, and without keepalives the mapping expires and inbound
// traffic stops.
void wg_tunnel_service();
