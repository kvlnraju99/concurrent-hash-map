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
UNIQUE=1000
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY"
echo "Ops: $OPS | Unique: $UNIQUE | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./resource_cache $OPS $T $BUCKETS $UNIQUE)
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
UNIQUE=1000
BUCKET_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY"
echo "Ops: $OPS | Unique: $UNIQUE | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./resource_cache $OPS $THREADS $B $UNIQUE)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $B | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 3: CONTENTION INTENSITY ---
OPS=5000000
THREADS=64
BUCKETS=131071
UNIQUE_LIST=(100 500 1000 5000 10000 50000)

echo "### EXPERIMENT 3: CONTENTION INTENSITY"
echo "Ops: $OPS | Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Unique URLs | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for U in "${UNIQUE_LIST[@]}"
do
    RESULTS=$(./resource_cache $OPS $THREADS $BUCKETS $U)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $U | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 4: PROBLEM SIZE SCALING ---
THREADS=64
BUCKETS=131071
UNIQUE=1000
SIZE_LIST=(1000000 5000000 10000000 20000000 50000000)

echo "### EXPERIMENT 4: PROBLEM SIZE SCALING"
echo "Threads: $THREADS | Unique: $UNIQUE | Buckets: $BUCKETS"
echo ""
echo "| Total Ops | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for S in "${SIZE_LIST[@]}"
do
    RESULTS=$(./resource_cache $S $THREADS $BUCKETS $UNIQUE)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    V6=$(extract_time "$RESULTS" "Library V6")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $S | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo ""
echo "Cache Sweeps Complete."
