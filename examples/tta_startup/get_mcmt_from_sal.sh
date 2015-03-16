#!/bin/bash

INPUT=$1
PROPERTY=$2

$HOME/Software/sal-3.3/bin/salenv-safe <<EOF | tail -n +12 | head -n -1 > $INPUT.flat 
 (sal/set-pp-max-width! 10000)
 (sal/set-pp-max-depth! 10000)
 (sal/set-pp-max-num-lines! 100000)
 (sal/set-sal-pp-proc! sal-ast->lsal-doc) 
 (sal/set-trace-info-enabled! #f)
 (make-flat-assertion "$INPUT!$PROPERTY")
EOF
