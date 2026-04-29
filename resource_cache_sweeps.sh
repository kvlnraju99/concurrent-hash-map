#!/bin/bash

# Ensure we are built
make resource_cache > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | head -1 | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT 1: THREAD SCALABILITY ---
OPS=5000000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY"
echo "Total Operations: $OPS | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./resource_cache $OPS $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY ---
OPS=5000000
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY"
echo "Total Operations: $OPS | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./resource_cache $OPS $THREADS $B)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $B | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 3: WORKLOAD MIX (CONTENTION) ---
THREADS=64
BUCKETS=131071
OPS_LIST=(1000000 5000000 10000000)

echo "### EXPERIMENT 3: WORKLOAD INTENSITY"
echo "Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Total Ops | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for O in "${OPS_LIST[@]}"
do
    RESULTS=$(./resource_cache $O $THREADS $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $O | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo ""
echo "Cache Sweeps Complete."
