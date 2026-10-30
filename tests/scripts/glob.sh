T=/tmp/mysh_glob_$$
mkdir -p $T && cd $T
touch a.txt b.txt c.log 'sp ace.txt'
echo *.txt
echo *.log
echo *.none
echo "*.txt"
echo '*.txt'
echo ?.txt
echo [ab].txt
G='*.txt'
echo $G
cd / && rm -rf $T
