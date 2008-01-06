#!/bin/bash
inc=1
for ((size=2;size<=50;size+=$inc))
do
  header=1
  filename=selectionsortmem$size".smt"
  ./selectionsortmem $size | boolector -rwl0 -ds | while read line
  do
    if [[ $header -eq 1 ]]; then
      echo "(benchmark $filename" > $filename
      echo ":source {" >> $filename
      echo "We verify that selection sort sorts an array" >> $filename
      echo "of length $size in memory. Additionally, we read an element" >> $filename
      
      echo "at an arbitrary index of the initial array and show that this" >> $filename
      echo "element can not be unequal to an element in the sorted array.">> $filename
      echo "" >> $filename
      echo -n "Contributed by Robert Brummayer " >> $filename
      echo "(robert.brummayer@gmail.com)." >> $filename
      echo "}" >> $filename
      echo ":status unsat" >> $filename
      echo ":category { crafted }" >> $filename
      header=0
    else
      echo $line >> $filename
    fi
  done
  if [[ $size -eq 10 ]]; then
    inc=2
  elif [[ $size -eq 20 ]]; then
    inc=5
  fi
done
