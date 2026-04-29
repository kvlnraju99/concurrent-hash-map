#!/bin/bash

# Ensure we are built
make collatz_memo > /dev/null

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
LIMIT=1000000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY"
echo "Range: 1 to $LIMIT | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./collatz_memo $LIMIT $T $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $T | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY ---
LIMIT=1000000
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY"
echo "Range: 1 to $LIMIT | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./collatz_memo $LIMIT $THREADS $B)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $B | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 3: CHARACTERISTIC (Small Scale Overhead) ---
THREADS=64
BUCKETS=131071
LIMIT_LIST=(10000 50000 100000 500000)

echo "### EXPERIMENT 3: SMALL SCALE OVERHEAD"
echo "Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Limit | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for L in "${LIMIT_LIST[@]}"
do
    RESULTS=$(./collatz_memo $L $THREADS $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $L | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 4: PROBLEM SIZE SCALING ---
THREADS=64
BUCKETS=131071
LIMIT_LIST=(1000000 2000000 5000000 10000000)

echo "### EXPERIMENT 4: PROBLEM SIZE SCALING"
echo "Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Upper Limit | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- | :--- |"

for L in "${LIMIT_LIST[@]}"
do
    RESULTS=$(./collatz_memo $L $THREADS $BUCKETS)
    SEQ=$(extract_result "$RESULTS" "Sequential")
    V2=$(extract_result "$RESULTS" "Library V2")
    V3=$(extract_result "$RESULTS" "Library V3")
    V6=$(extract_result "$RESULTS" "Library V6")
    TBB=$(extract_result "$RESULTS" "Intel TBB")
    echo "| $L | $SEQ | $V2 | $V3 | $V6 | $TBB |"
done

echo ""
echo "Collatz Sweeps Complete."
