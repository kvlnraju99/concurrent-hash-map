#!/bin/bash

# Ensure we are built
make word_counter > /dev/null

# Helper function to extract time
extract_time() {
    echo "$1" | grep "$2" | awk -F'Time: ' '{print $2}' | awk '{print $1}' | sed 's/s//'
}

# --- EXPERIMENT 1: THREAD SCALABILITY ---
TOTAL_WORDS=5000000
UNIQUE_WORDS=100000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

echo "### EXPERIMENT 1: THREAD SCALABILITY"
echo "Words: $TOTAL_WORDS | Unique: $UNIQUE_WORDS | Buckets: $BUCKETS"
echo ""
echo "| Threads | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    RESULTS=$(./word_counter $TOTAL_WORDS $UNIQUE_WORDS $T $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $T | $SEQ | $V2 | $V3 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 2: BUCKET SENSITIVITY ---
TOTAL_WORDS=5000000
UNIQUE_WORDS=100000
THREADS=64
BUCKET_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 2: BUCKET SENSITIVITY"
echo "Words: $TOTAL_WORDS | Unique: $UNIQUE_WORDS | Threads: $THREADS"
echo ""
echo "| Initial Buckets | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for B in "${BUCKET_LIST[@]}"
do
    RESULTS=$(./word_counter $TOTAL_WORDS $UNIQUE_WORDS $THREADS $B)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $B | $SEQ | $V2 | $V3 | $TBB |"
done

echo -e "\n---\n"

# --- EXPERIMENT 3: CONTENTION INTENSITY ---
TOTAL_WORDS=5000000
THREADS=64
BUCKETS=131071
UNIQUE_LIST=(1000 5000 10000 50000 100000 500000)

echo "### EXPERIMENT 3: CONTENTION INTENSITY"
echo "Words: $TOTAL_WORDS | Threads: $THREADS | Buckets: $BUCKETS"
echo ""
echo "| Unique Words | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for U in "${UNIQUE_LIST[@]}"
do
    RESULTS=$(./word_counter $TOTAL_WORDS $U $THREADS $BUCKETS)
    SEQ=$(extract_time "$RESULTS" "Sequential")
    V2=$(extract_time "$RESULTS" "Library V2")
    V3=$(extract_time "$RESULTS" "Library V3")
    TBB=$(extract_time "$RESULTS" "Intel TBB")
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $U | $SEQ | $V2 | $V3 | $TBB |"
done

echo ""
echo "All Sweeps Complete."
