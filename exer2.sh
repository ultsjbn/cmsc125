#!/bin/bash


echo "Enter the file name:"
read filename

echo "Your file is being created... Please bear with us."
sleep 5

touch $filename.txt
echo "File is created. Thank you."

echo "Enter the content of the file: "
read content

echo $content > $filename.txt
