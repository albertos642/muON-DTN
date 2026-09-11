#!/usr/bin/env bash
# ==============================================================================
# @file stop_gateway.sh
# @brief IONe-uartcl-cobs Gateway Shutdown Script.
#
# @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
# @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
# @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
# ==============================================================================

echo "[*] Gracefully stopping BPv7 engine and CLA daemons..."
bpadmin . 2>/dev/null || true

echo "[*] Stopping ION core node..."
ionadmin . 2>/dev/null || true

echo "[*] Cleaning IPC and shared memory allocations..."
killm 2>/dev/null || true

echo "[+] IONe Gateway stopped cleanly."
