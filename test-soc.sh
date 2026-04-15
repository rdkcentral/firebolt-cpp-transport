#!/usr/bin/env bash
# test-soc.sh — test firebolt-cpp-transport changes against a live device gateway
#
# Opens SSH tunnels to the device, creates a LaunchDelegate session, then runs
# the C++ api_test_app using the locally-built transport (build-dev/) so new
# transport code is exercised against a real gateway without a Yocto build.
#
# Usage:
#   ./test-soc.sh                           # uses SOC_HOST from env or .env
#   SOC_HOST=192.168.201.170 ./test-soc.sh
#   ./test-soc.sh --auto                    # run all methods non-interactively
#   ./test-soc.sh -- Device.id Device.distributor  # specific methods to call
#
# Prerequisites:
#   - ssh (ssh-keygen + ssh-copy-id root@<device> -p <port> recommended)
#   - websocat  (for session creation: apt install / cargo install websocat)
#   - api_test_app built:  cd ../firebolt-cpp-client/test/api_test_app && ./build.sh --no-run
#   - transport built:     cd build-dev && make -j$(nproc)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── config ────────────────────────────────────────────────────────────────────
SOC_HOST="${SOC_HOST:-}"
SOC_PORT="${SOC_PORT:-10022}"
SOC_USER="${SOC_USER:-root}"
GW_PORT="${GW_PORT:-3473}"         # AppGateway (Firebolt API)
LD_PORT="${LD_PORT:-3474}"         # LaunchDelegate (session issuer)
APP_ID="${APP_ID:-test-transport}"
AUTO_RUN="${AUTO_RUN:-0}"

TEST_APP="${WORKSPACE_DIR}/firebolt-cpp-client/test/api_test_app/build/api-test-app"
TRANSPORT_LIB_DIR="${SCRIPT_DIR}/build-clean/src"

# ── parse args ────────────────────────────────────────────────────────────────
EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --auto)   AUTO_RUN=1; shift ;;
        --)       shift; EXTRA_ARGS+=("$@"); break ;;
        *)        EXTRA_ARGS+=("$1"); shift ;;
    esac
done

# ── load .env from app-gateway-cpc if present ─────────────────────────────────
_ENV_FILE="${WORKSPACE_DIR}/app-gateway-cpc/.env"
if [[ -f "${_ENV_FILE}" && -z "${SOC_HOST}" ]]; then
    SOC_HOST="$(grep -E '^SOC_HOST=' "${_ENV_FILE}" | head -1 | cut -d= -f2-)" || true
fi

[[ -n "${SOC_HOST}" ]] || {
    echo "ERROR: SOC_HOST not set."
    echo "  Export it: export SOC_HOST=192.168.x.x"
    echo "  Or create app-gateway-cpc/.env with SOC_HOST=..."
    exit 1
}

# ── validate prerequisites ────────────────────────────────────────────────────
[[ -x "${TEST_APP}" ]] || {
    echo "ERROR: api_test_app not built at ${TEST_APP}"
    echo "  Build it first:"
    echo "    cd ${WORKSPACE_DIR}/firebolt-cpp-client/test/api_test_app && ./build.sh --no-run"
    exit 1
}

[[ -f "${TRANSPORT_LIB_DIR}/libFireboltTransport.so.1" ]] || {
    echo "ERROR: transport not built at ${TRANSPORT_LIB_DIR}"
    echo "  Build it first:"
    echo "    cd ${SCRIPT_DIR} && cmake -B build-clean -DCMAKE_BUILD_TYPE=Debug && cmake --build build-clean -j\$(nproc)"
    exit 1
}

command -v websocat >/dev/null 2>&1 || {
    echo "ERROR: websocat not found (needed for session creation)"
    echo "  Install: cargo install websocat  or  apt install websocat"
    exit 1
}

# ── show which transport lib will be used ────────────────────────────────────
_lib_real="$(readlink -f "${TRANSPORT_LIB_DIR}/libFireboltTransport.so.1" 2>/dev/null || echo unknown)"
echo "[test-soc] Transport lib : ${_lib_real}"
echo "[test-soc] Test app      : ${TEST_APP}"
echo "[test-soc] Device        : ${SOC_USER}@${SOC_HOST}:${SOC_PORT}"
echo "[test-soc] Gateway ports : GW=${GW_PORT}  LD=${LD_PORT}"
echo

# ── open SSH tunnels ──────────────────────────────────────────────────────────
echo "[test-soc] Opening SSH tunnels ..."
pkill -f "ssh.*${LD_PORT}:127.0.0.1:${LD_PORT}.*${SOC_HOST}" 2>/dev/null || true
sleep 1

ssh -p "${SOC_PORT}" \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -o ConnectTimeout=10 \
    -o ExitOnForwardFailure=yes \
    -L "127.0.0.1:${LD_PORT}:127.0.0.1:${LD_PORT}" \
    -L "127.0.0.1:${GW_PORT}:127.0.0.1:${GW_PORT}" \
    "${SOC_USER}@${SOC_HOST}" -N &
TUNNEL_PID=$!
trap 'echo "[test-soc] Closing tunnels"; kill "${TUNNEL_PID}" 2>/dev/null; true' EXIT
sleep 2

kill -0 "${TUNNEL_PID}" 2>/dev/null || {
    echo "ERROR: SSH tunnel exited immediately — is ${SOC_HOST}:${SOC_PORT} reachable?"
    exit 1
}
echo "[test-soc] Tunnels up (PID ${TUNNEL_PID})"

# ── create LaunchDelegate session ─────────────────────────────────────────────
echo "[test-soc] Creating session on LaunchDelegate (port ${LD_PORT}) ..."
SESSION_JSON=$(echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"lifecyclemanagement.session\",\"params\":{\"session\":{\"app\":{\"id\":\"${APP_ID}\",\"title\":\"Transport Test\"},\"launch\":{\"inactive\":true,\"intent\":{\"action\":\"launch\",\"context\":{\"source\":\"device\"}}},\"runtime\":{\"id\":\"com.sky.rdkbrowser\",\"transport\":\"websocket\"}}}}" | \
    timeout 5 websocat --no-close "ws://127.0.0.1:${LD_PORT}?appId=${APP_ID}" 2>/dev/null) || true

SESSION_JSON=$(echo "${SESSION_JSON}" | grep -m1 '"result"' || true)
SESSION_ID=$(python3 -c "import sys,json; print(json.load(sys.stdin)['result']['sessionId'])" <<< "${SESSION_JSON}" 2>/dev/null || true)

GW_URL="ws://127.0.0.1:${GW_PORT}/jsonrpc"
if [[ -n "${SESSION_ID}" ]]; then
    echo "[test-soc] Got sessionId: ${SESSION_ID}"
    GW_URL="${GW_URL}?session=${SESSION_ID}"
else
    echo "[test-soc] WARNING: no sessionId (LaunchDelegate may not be running). Connecting directly to AppGateway."
fi
echo "[test-soc] Gateway URL: ${GW_URL}"
echo

# ── run test app with new transport ──────────────────────────────────────────
SYSROOT="${WORKSPACE_DIR}/sysroot-artifacts"
export LD_LIBRARY_PATH="${TRANSPORT_LIB_DIR}:${SYSROOT}/lib:${SYSROOT}/usr/lib:${LD_LIBRARY_PATH:-}"

echo "[test-soc] LD_LIBRARY_PATH (first entry): ${TRANSPORT_LIB_DIR}"
echo "[test-soc] Launching api_test_app ..."
echo

_app_args=("--url" "${GW_URL}")
[[ "${AUTO_RUN}" -eq 1 ]] && _app_args+=("--auto")
[[ ${#EXTRA_ARGS[@]} -gt 0 ]] && _app_args+=("${EXTRA_ARGS[@]}")

exec "${TEST_APP}" "${_app_args[@]}"
