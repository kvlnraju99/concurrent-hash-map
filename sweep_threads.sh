#!/bin/bash

# Configuration
TOTAL_WORDS=5000000
UNIQUE_WORDS=100000
BUCKETS=131071
THREADS=(1 2 4 8 16 32 64)

# Ensure we are built
make word_counter > /dev/null

echo "=========================================================="
echo " SCALABILITY SWEEP: WORD FREQUENCY COUNTER"
echo " Words: $TOTAL_WORDS | Unique: $UNIQUE_WORDS | Buckets: $BUCKETS"
echo "=========================================================="
echo ""
echo "| Threads | Sequential | Library V2 | Library V3 | Intel TBB |"
echo "| :--- | :--- | :--- | :--- | :--- |"

for T in "${THREADS[@]}"
do
    # Run and capture output
    RESULTS=$(./word_counter $TOTAL_WORDS $UNIQUE_WORDS $T $BUCKETS)
    
    # Extract times using awk. We assume the time is the 7th field in our output format
    SEQ=$(echo "$RESULTS" | grep "Sequential" | awk '{print $7}')
    V2=$(echo "$RESULTS" | grep "Library V2" | awk '{print $7}')
    V3=$(echo "$RESULTS" | grep "Library V3" | awk '{print $7}')
    TBB=$(echo "$RESULTS" | grep "Intel TBB" | awk '{print $7}')
    
    # Handle optional TBB (if not compiled, it will be empty)
    if [ -z "$TBB" ]; then TBB="N/A"; fi

    echo "| $T | $SEQ | $V2 | $V3 | $TBB |"
done
echo ""
echo "Sweep Complete."
