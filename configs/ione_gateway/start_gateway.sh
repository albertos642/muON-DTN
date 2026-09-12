#!/usr/bin/env bash
# ==============================================================================
# @file start_gateway.sh
# @brief IONe-uartcl-cobs Gateway Startup Script for muON-DTN Interoperability.
#
# @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
# @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
# @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERIAL_PORT="${1:-/dev/ttyUSB0}"
BAUD_RATE="${2:-115200}"

echo "========================================================"
echo " Starting IONe Gateway for muON-DTN Interoperability    "
echo " Target Serial Device: ${SERIAL_PORT} @ ${BAUD_RATE} bps"
echo " Node ID: 3 (Gateway) -> Connecting to Node 2 (muON Gateway) & Node 1 (Sensor)"
echo "========================================================"

if [ ! -e "${SERIAL_PORT}" ]; then
    echo "[!] Warning: Serial port ${SERIAL_PORT} does not exist."
    echo "    Please verify MCU USB connection or pass device as argument:"
    echo "    Usage: ./start_gateway.sh [/dev/ttyUSB0|/dev/ttyACM0] [115200]"
fi

# Stop any currently running ION instances
echo "[*] Cleaning up existing ION processes..."
ionstop 2>/dev/null || true
killm 2>/dev/null || true

# Prepare temporary working directory
mkdir -p /tmp/ion_gateway
cd "${SCRIPT_DIR}"

# Adapt port/baud in temporary config copies if non-default passed
TMP_BPRC="/tmp/ion_gateway/node_gateway.bprc"
TMP_IPNRC="/tmp/ion_gateway/node_gateway.ipnrc"
sed "s|/dev/ttyUSB0:115200|${SERIAL_PORT}:${BAUD_RATE}|g" node_gateway.bprc > "${TMP_BPRC}"
sed "s|/dev/ttyUSB0:115200|${SERIAL_PORT}:${BAUD_RATE}|g" node_gateway.ipnrc > "${TMP_IPNRC}"

echo "[1/3] Initializing ION SDR and Contact Plan..."
ionadmin node_gateway.ionrc

echo "[2/3] Initializing BPv7 Engine and UART-COBS CLA..."
bpadmin "${TMP_BPRC}"

echo "[3/3] Initializing IPN Routing Table..."
ipnadmin "${TMP_IPNRC}"

echo "========================================================"
echo "[+] IONe Gateway Node 3 successfully started!"
echo "    Induct & Outduct running on ${SERIAL_PORT}:${BAUD_RATE}"
echo "    Real-time log:        tail -f ${SCRIPT_DIR}/ion.log"
echo "    Watch indicators:    a=acquired, d=delivered, e=expired, z=discarded, y=refused"
echo "    Receive sink:         bpsink ipn:3.1"
echo "    Send test bundle:     bpsendfile ipn:3.1 ipn:1.10 <file>"
echo "    Bundle statistics:    bpstats"
echo "    Stop gateway:         ./stop_gateway.sh"
echo "========================================================"
