#!/bin/bash
make 
sudo ./aesdchar_unload 
sudo ./aesdchar_load 
sudo chmod a+w /dev/aesdchar
echo "hi 1"  > /dev/aesdchar
echo "hi 2"  > /dev/aesdchar
echo "hi 3"  > /dev/aesdchar
echo "hi 4"  > /dev/aesdchar
# echo "hi 5"  > /dev/aesdchar
# echo "hi 6"  > /dev/aesdchar
# echo "hi 7"  > /dev/aesdchar
# echo "hi 8"  > /dev/aesdchar
# echo "hi 9"  > /dev/aesdchar
# echo "hi 10" > /dev/aesdchar

# echo "round1: let's see what we got printed"
# cat /dev/aesdchar



# echo -n "hi" > /dev/aesdchar
# echo -n " " > /dev/aesdchar
# echo "11" > /dev/aesdchar
# echo "hi 12" > /dev/aesdchar
# echo "hi 13" > /dev/aesdchar
# echo "hi 14" > /dev/aesdchar
# echo "hi 15" > /dev/aesdchar
# echo "hi 16" > /dev/aesdchar
# echo "hi 17" > /dev/aesdchar
# echo "hi 18" > /dev/aesdchar
# echo "hi 19" > /dev/aesdchar
# echo "hi 20" > /dev/aesdchar

# echo "round2: let's see what we got printed"
# cat /dev/aesdchar