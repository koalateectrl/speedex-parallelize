echo "trying to run $2"

init_command='
echo "started bash";
set -x;
cd xfs; 
/opt/init.sh; 
cd edce/implementation;
git pull;
make -j; 
cd src; 
make xdrpy_module;
echo "starting input command!";
'$2'
'

echo ${init_command}

ssh -t ramseyer@$1 '
echo "starting connection";
set -x;
file='${init_command}'

bash --init-file <(echo "${file}")
'