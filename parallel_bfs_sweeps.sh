#!/bin/bash

# Ensure we are built
make parallel_bfs > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT 1: THREAD SCALABILITY (BFS) ---
NODES=500000
EDGES=10
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY (BFS)"
echo "Nodes: $NODES | Edges: $EDGES | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $EDGES $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY (BFS) ---
NODES=500000
EDGES=10
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000 1000000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY (BFS)"
echo "Nodes: $NODES | Edges: $EDGES | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $EDGES $THREADS $B)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $B | $SEQ | $V2 | $V3 | $TBB |"
done

echo ""
echo "All BFS Sweeps Complete."
