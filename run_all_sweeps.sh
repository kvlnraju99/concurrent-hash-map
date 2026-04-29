#!/bin/bash

# Ensure we are built
make word_counter > /dev/null

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
    SEQ=$(echo "$RESULTS" | grep "Sequential" | awk '{print $7}')
    V2=$(echo "$RESULTS" | grep "Library V2" | awk '{print $7}')
    V3=$(echo "$RESULTS" | grep "Library V3" | awk '{print $7}')
    TBB=$(echo "$RESULTS" | grep "Intel TBB" | awk '{print $7}')
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
    SEQ=$(echo "$RESULTS" | grep "Sequential" | awk '{print $7}')
    V2=$(echo "$RESULTS" | grep "Library V2" | awk '{print $7}')
    V3=$(echo "$RESULTS" | grep "Library V3" | awk '{print $7}')
    TBB=$(echo "$RESULTS" | grep "Intel TBB" | awk '{print $7}')
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
    SEQ=$(echo "$RESULTS" | grep "Sequential" | awk '{print $7}')
    V2=$(echo "$RESULTS" | grep "Library V2" | awk '{print $7}')
    V3=$(echo "$RESULTS" | grep "Library V3" | awk '{print $7}')
    TBB=$(echo "$RESULTS" | grep "Intel TBB" | awk '{print $7}')
    if [ -z "$TBB" ]; then TBB="N/A"; fi
    echo "| $U | $SEQ | $V2 | $V3 | $TBB |"
done

echo ""
echo "All Sweeps Complete."
