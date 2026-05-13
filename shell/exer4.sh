#!/bin/bash


echo "Enter a directory in the current location"
read direct

if [ -d "$direct" ]
then
   echo "Enter the file name to check: "
   read fileName

   if [ -f "$direct/$fileName" ]
   then
	echo "$fileName exists"
   else
	echo "$fileName does not exist in $direct"
   fi
else
   echo "$direct does not exist."
fi 
