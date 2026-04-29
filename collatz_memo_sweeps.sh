#!/bin/bash

# Ensure we are built
make collatz_memo > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT 1: THREAD SCALABILITY (COLLATZ) ---
UPPER_LIMIT=1000000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY (COLLATZ)"
echo "Upper Limit: $UPPER_LIMIT | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./collatz_memo $UPPER_LIMIT $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $TBB |"
done

echo ""
echo "All Collatz Sweeps Complete."
