#!/bin/bash
# Verify reproducible search

error() {
    echo "reprosearch testing failed on line $1"
    exit 1
}
trap 'error ${LINENO}' ERR

echo "reprosearch testing started"

# Repeat two short games, separated by ucinewgame.
# with go nodes $nodes they should result in exactly
# the same node count for each iteration.
cat <<EOF > repeat.exp
set timeout 10
spawn ./DON
lassign \$argv NODES

send "uci\n"
expect "uciok"

send "ucinewgame\n"
send "position startpos\n"
send "go nodes \$NODES\n"
expect "bestmove"

send "position startpos moves e2e4 e7e6\n"
send "go nodes \$NODES\n"
expect "bestmove"

send "ucinewgame\n"
send "position startpos\n"
send "go nodes \$NODES\n"
expect "bestmove"

send "position startpos moves e2e4 e7e6\n"
send "go nodes \$NODES\n"
expect "bestmove"

send "quit\n"
expect eof
EOF

# To increase the likelihood of finding a non-reproducible case,
# the allowed number of nodes are varied systematically
for i in {1..20}
do
    NODES=$((100*3**i/2**i))
    echo "reprosearch testing with $NODES nodes"
    # Each line should appear exactly an even number of times
    expect repeat.exp $NODES 2>&1 |
        sed 's/\x1b\[[0-9;]*m//g' |
        grep -o "nodes [0-9]*" |
        sort |
        uniq -c |
        awk '{if ($1%2!=0) exit(1)}'
done

rm repeat.exp

echo "reprosearch testing OK"
