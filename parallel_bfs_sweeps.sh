#!/bin/bash

# Ensure we are built
make parallel_bfs > /dev/null

# Helper function to extract time and verification status
extract_result() {
    LINE=$(echo "$1" | grep "$2" | head -1)
    TIME=$(echo "$LINE" | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//')
    VERIFY=$(echo "$LINE" | awk -F'Verification: ' '{print $2}' | awk '{print $1}')
    if [ "$VERIFY" == "PASSED" ]; then
        echo "$TIME (P)"
    else
        echo "$TIME (F)"
    fi
}

# --- EXPERIMENT 1: THREAD SCALABILITY ---
NODES=500000
EDGES=20
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY"
echo "Nodes: $NODES | Edges/Node: $EDGES | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $EDGES $T $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $T | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY ---
NODES=500000
EDGES=20
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY"
echo "Nodes: $NODES | Edges/Node: $EDGES | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $EDGES $THREADS $B)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $B | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 3: GRAPH DENSITY (CONTENTION) ---
NODES=500000
THREADS=64
BUCKETS=131071
DENSITY_LIST=(5 10 20 50)

echo "### EXPERIMENT 3: GRAPH DENSITY (CONTENTION)"
echo "Nodes: $NODES | Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Edges per Node | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for D in "${DENSITY_LIST[@]}"
do
    RESULTS=$(./parallel_bfs $NODES $D $THREADS $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $D | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 4: PROBLEM SIZE SCALING ---
THREADS=64
EDGES=20
BUCKETS=131071
NODE_LIST=(100000 500000 1000000 2000000 5000000)

echo "### EXPERIMENT 4: PROBLEM SIZE SCALING"
echo "Threads: $THREADS | Edges/Node: $EDGES | Buckets: $BUCKETS"
echo ""
echo "| Total Nodes | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for N in "${NODE_LIST[@]}"
do
    RESULTS=$(./parallel_bfs $N $EDGES $THREADS $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $N | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo ""
echo "BFS Sweeps Complete."
