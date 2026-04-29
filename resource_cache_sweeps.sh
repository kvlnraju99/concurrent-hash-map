#!/bin/bash

# Ensure we are built
make resource_cache > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT 1: THREAD SCALABILITY ---
TOTAL_OPS=2000000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY (RESOURCE CACHE)"
echo "Total Ops: $TOTAL_OPS | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./resource_cache $TOTAL_OPS $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY ---
TOTAL_OPS=2000000
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000 1000000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY (RESOURCE CACHE)"
echo "Total Ops: $TOTAL_OPS | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./resource_cache $TOTAL_OPS $THREADS $B)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $B | $SEQ | $V2 | $V3 | $TBB |"
done

echo ""
echo "All Cache Sweeps Complete."
