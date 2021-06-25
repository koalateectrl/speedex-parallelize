echo "reminder: usage: ./blah \"command\" hostname desired_files"

set -ex



if [ "$2" != "localhost" ]; then 
	echo $1
	echo "running remote at $2"

	ssh  $2 /bin/bash <<EOF
		cd edce/implementation/src
		source set_env.sh
		pwd
		$1
EOF

	for ((i=3;i<=$#;i++));
	do
		filename=${!i}
		echo "copying file to ./experiment_results/${filename}"
		scp $2:~/edce/implementation/src/experiment_results/${filename} ./experiment_results/${filename}
	done
else
	echo "running locally"
	$1
fi



