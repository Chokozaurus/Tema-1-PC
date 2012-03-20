#!/bin/bash

SPEED=10
DELAY=2000
LOSS=10
CORRUPT=10
WINDOW=250

echo "Killing already opened processes"
killall -q link
killall -q recv
killall -q send

rm -f recv_$1

./link speed=$SPEED delay=$DELAY loss=$LOSS corrupt=$CORRUPT &> link.out &
sleep 1
./recv window=$WINDOW & #> primit.out &
sleep 1

echo "Starting transfer"
time ./send speed=$SPEED delay=$DELAY loss=$LOSS corrupt=$CORRUPT $1 
sleep 2

echo "Finished transfer, checking files"
diff $1 recv_$1
