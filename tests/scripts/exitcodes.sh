sh -c 'exit 3'
echo $?
sh -c 'kill -9 $$'
echo $?
/bin/does-not-exist 2>/dev/null
echo $?
/etc 2>/dev/null
echo $?
exit 7
