#!/bin/bash

# Configuration
TOTAL_WORDS=5000000
THREADS=64
BUCKETS=131071
UNIQUE_LIST=(1000 5000 10000 50000 100000 500000)

# Ensure we are built
make word_counter > /dev/null

echo "=========================================================="
echo " CONTENTION SENSITIVITY SWEEP: WORD FREQUENCY COUNTER"
echo " Words: $TOTAL_WORDS | Threads: $THREADS | Buckets: $BUCKETS"
echo "=========================================================="
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
echo "Sweep Complete."
