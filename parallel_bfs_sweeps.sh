#!/bin/bash

# Ensure we are built
make parallel_bfs > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | head -1 | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT: BFS THREAD SCALABILITY ---
NODES=500000
EDGES=20
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### PARALLEL BFS: THREAD SCALABILITY"
echo "Nodes: $NODES | Edges/Node: $EDGES | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $EDGES $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo ""
echo "Sweep Complete."
