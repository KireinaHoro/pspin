#!/bin/bash

type=$1

$HUYGENS_HOME/src/apps/datatypes/typebuilder/typebuilder "$type" 1 todel > log 2>/dev/null;

offset=$(cat log | grep "Offsets" | cut -d " " -f 2)

IFS='
';

vectors=$(cat log | grep VECTOR)


vecout=""

if [ "$vectors" == "" ]; then
   ctgs=$(cat log | grep CONTIG)
   count=$(echo $ctgs | sed 's/.*CONTIG: count=\([0-9]\+\).*/\1/g')
   vecout="1,$count,$count"
else
for vector in $vectors; do
    curvec=$(echo $vector | sed 's/.*VECTOR: count=\([0-9]\+\), blksz=\([0-9]\+\), stride=\([0-9]\+\), datatype=.*/\1,\2,\3/g')
    vecout="$vecout$curvec "
done
fi



echo $offset
echo $vecout
