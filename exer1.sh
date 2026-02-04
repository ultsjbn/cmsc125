#!/bin/bash


echo "Enter the file name:"
read filename

echo "Your file is being created... Please bear with us."
sleep 5

touch $filename.sh
echo '#!/bin/bash' > $filename.sh

echo "File is created. Thank you."

