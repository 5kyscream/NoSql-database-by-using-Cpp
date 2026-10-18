#!/usr/bin/env bash
# benchmarks/run_benchmarks.sh
# Runs redis-benchmark against MiniRed and (optionally) real Redis,
# saves results to benchmarks/results.md.
# This script is the authoritative source of benchmark figures in the README.
#
# Usage:
#   ./benchmarks/run_benchmarks.sh            # MiniRed only
#   REDIS_PORT=6380 ./benchmarks/run_benchmarks.sh  # also bench real Redis
#
set -euo pipefail

MINIRED_PORT=${MINIRED_PORT:-6379}
REDIS_PORT=${REDIS_PORT:-}
N_REQUESTS=100000
PAYLOAD=32
CLIENTS_LIST=(1 10 50 100)
PIPELINE_LIST=(1 8 32)
OUT_FILE="benchmarks/results.md"

require() { command -v "$1" &>/dev/null || { echo "ERROR: $1 not found"; exit 1; }; }
require redis-benchmark
require redis-cli

bench_one() {
    local host=$1 port=$2 label=$3
    echo "## $label (port $port)" >> "$OUT_FILE"
    echo "" >> "$OUT_FILE"
    echo "| Clients | Pipeline | SET ops/s | GET ops/s |" >> "$OUT_FILE"
    echo "|---------|----------|-----------|-----------|" >> "$OUT_FILE"

    for c in "${CLIENTS_LIST[@]}"; do
        for p in "${PIPELINE_LIST[@]}"; do
            set_ops=$(redis-benchmark -h "$host" -p "$port" \
                -n "$N_REQUESTS" -c "$c" -P "$p" -d "$PAYLOAD" \
                -t SET --csv 2>/dev/null | grep '"SET"' | cut -d',' -f2 | tr -d '"')
            get_ops=$(redis-benchmark -h "$host" -p "$port" \
                -n "$N_REQUESTS" -c "$c" -P "$p" -d "$PAYLOAD" \
                -t GET --csv 2>/dev/null | grep '"GET"' | cut -d',' -f2 | tr -d '"')
            echo "| $c | $p | ${set_ops:-N/A} | ${get_ops:-N/A} |" >> "$OUT_FILE"
        done
    done
    echo "" >> "$OUT_FILE"
}

# Machine info
{
    echo "# Benchmark Results"
    echo ""
    echo "**Generated:** $(date -u +'%Y-%m-%d %H:%M UTC')"
    echo ""
    echo "**Machine:**"
    echo "\`\`\`"
    uname -a
    lscpu 2>/dev/null | grep -E 'Model name|CPU\(s\)|MHz' || true
    echo "\`\`\`"
    echo ""
    echo "**Parameters:** requests=$N_REQUESTS payload=${PAYLOAD}B"
    echo ""
} > "$OUT_FILE"

bench_one "127.0.0.1" "$MINIRED_PORT" "MiniRed"

if [[ -n "$REDIS_PORT" ]]; then
    bench_one "127.0.0.1" "$REDIS_PORT" "Real Redis"
fi

echo "Results written to $OUT_FILE"
cat "$OUT_FILE"
